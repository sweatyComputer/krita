/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisWetPaintOp.h"

#include <QMutex>
#include <QMutexLocker>
#include <QRandomGenerator>
#include <QThread>
#include <QtConcurrent>
#include <QtMath>

#include <KoColor.h>
#include <KoColorSpace.h>
#include <KoColorSpaceRegistry.h>

#include <kis_brush.h>
#include <kis_node.h>
#include <brushengine/kis_paintop_settings.h>
#include <kis_fixed_paint_device.h>
#include <kis_lod_transform.h>
#include <kis_paint_device.h>
#include <kis_painter.h>
#include <kis_selection.h>
#include <kis_pixel_selection.h>
#include <kis_spacing_information.h>

#include "KisInterstrokeDataFactory.h"
#include "KisWetPaintInterstrokeData.h"

namespace {

struct WetPaintInterstrokeDataFactory : public KisInterstrokeDataFactory
{
    bool isCompatible(KisInterstrokeData *data) override {
        return dynamic_cast<KisWetPaintInterstrokeData *>(data);
    }

    KisInterstrokeData *create(KisPaintDeviceSP device) override {
        return new KisWetPaintInterstrokeData(device);
    }
};

/// paint left on the brush by the previous stroke, for "dirty brush" mode
struct LeftoverPaint {
    QMutex mutex;
    bool valid {false};
    float rgb[3] {0.5f, 0.5f, 0.5f};
};
Q_GLOBAL_STATIC(LeftoverPaint, s_leftover)

void colorToLinear(const KoColor &color, float out[3])
{
    const KoColor c16 = color.convertedTo(KoColorSpaceRegistry::instance()->rgb16());
    const quint16 *bgra = reinterpret_cast<const quint16 *>(c16.data());
    const KoPigment::SrgbTables &t = KoPigment::SrgbTables::instance();
    out[0] = t.decode16[bgra[2]];
    out[1] = t.decode16[bgra[1]];
    out[2] = t.decode16[bgra[0]];
}

/// dabs smaller than this are painted on the calling thread
// Dabs smaller than this are painted on the calling thread; above it the rows
// are split across cores. Thread hand-off costs tens of microseconds, a 48px
// dab of KM mixing costs ~0.2-0.5 ms on a tablet, so split early.
constexpr int ParallelPixelThreshold = 48 * 48;
constexpr int MinRowsPerChunk = 8;

/// how the layer's pixels reach 16-bit sRGB (what the engine works in)
enum class LayerFormat { Rgb8Srgb, Rgb16Srgb, Other };

LayerFormat layerFormat(const KoColorSpace *cs)
{
    KoColorSpaceRegistry *reg = KoColorSpaceRegistry::instance();
    if (*cs == *reg->rgb8()) return LayerFormat::Rgb8Srgb;     // Krita's default RGBA8 sRGB
    if (*cs == *reg->rgb16()) return LayerFormat::Rgb16Srgb;
    return LayerFormat::Other;                                  // anything else goes via LCMS
}

} // namespace

KisWetPaintOp::KisWetPaintOp(const KisPaintOpSettingsSP settings, KisPainter *painter, KisNodeSP node, KisImageSP image)
    : KisBrushBasedPaintOp(settings, painter)
    , m_sizeOption(settings.data())
    , m_ratioOption(settings.data())
    , m_opacityOption(settings.data(), node)
    , m_spacingOption(settings.data())
    , m_rotationOption(settings.data())
    , m_scatterOption(settings.data())
    , m_pickupOption(settings.data())
    , m_smearOption(settings.data())
    , m_reloadOption(settings.data())
{
    Q_UNUSED(image);
    Q_ASSERT(painter);

    m_wetData.read(settings.data());
    m_rotationOption.applyFanCornersInfo(this);

    // we read aligned areas of the layer; subpixel offsets would blur the smear
    m_dabCache->disableSubpixelPrecision();

    KisPaintDeviceSP device = painter->device();
    if (device) {
        m_dataHolder = device->interstrokeData();
        m_data = dynamic_cast<KisWetPaintInterstrokeData *>(m_dataHolder.data());

        KIS_SAFE_ASSERT_RECOVER(m_data) {
            // no persistent wet state on this device (shouldn't happen for
            // normal layers): paint with a private one, it just won't survive
            // past this stroke
            m_dataHolder = toQShared(new KisWetPaintInterstrokeData(device));
            m_data = static_cast<KisWetPaintInterstrokeData *>(m_dataHolder.data());
        }

        m_data->applyDrying(m_wetData.dryingTime);
    }

    float fg[3];
    colorToLinear(painter->paintColor(), fg);

    {
        QMutexLocker l(&s_leftover->mutex);
        if (!m_wetData.cleanBrush && s_leftover->valid) {
            m_engine.setReservoir(s_leftover->rgb);
        }
    }
    m_engine.beginStroke(fg, m_wetData.cleanBrush, QRandomGenerator::global()->generate());
}

KisWetPaintOp::~KisWetPaintOp()
{
    QMutexLocker l(&s_leftover->mutex);
    m_engine.reservoirLinear(s_leftover->rgb);
    s_leftover->valid = true;
}

KisSpacingInformation KisWetPaintOp::paintAt(const KisPaintInformation &info)
{
    KisPaintDeviceSP device = painter()->device();
    if (!device || !m_brush || !m_data || !m_brush->canPaintFor(info)) {
        return KisSpacingInformation(1.0);
    }

    qreal scale = m_sizeOption.apply(info);
    scale *= KisLodTransform::lodToScale(device);
    const qreal rotation = m_rotationOption.apply(info);
    if (checkSizeTooSmall(scale)) return KisSpacingInformation();

    const qreal ratio = m_ratioOption.apply(info);
    const KisDabShape shape(scale, ratio, rotation);

    const qreal maskW = m_brush->maskWidth(shape, 0, 0, info);
    const qreal maskH = m_brush->maskHeight(shape, 0, 0, info);
    const QPointF scatteredPos = m_scatterOption.apply(info, maskW, maskH);

    const KisSpacingInformation spacingInfo = effectiveSpacing(scale, rotation, m_spacingOption, info);

    // ---- the brush mask ----
    const KoColorSpace *alpha8 = KoColorSpaceRegistry::instance()->alpha8();
    QRect dabRect;
    KisFixedPaintDeviceSP maskDab = m_dabCache->fetchDab(alpha8, KoColor(Qt::black, alpha8),
                                                         scatteredPos, shape, info, 1.0, &dabRect);
    if (!maskDab || dabRect.isEmpty()) return spacingInfo;

    const int w = dabRect.width();
    const int h = dabRect.height();
    const int n = w * h;

    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(maskDab->bounds().size() == dabRect.size(), spacingInfo);

    m_mask.resize(n);
    memcpy(m_mask.data(), maskDab->data(), size_t(n));

    // respect the active selection
    KisSelectionSP selection = painter()->selection();
    if (selection) {
        m_selection.resize(n);
        selection->projection()->readBytes(m_selection.data(), dabRect);
        for (int i = 0; i < n; i++) {
            m_mask[i] = quint8((int(m_mask[i]) * int(m_selection[i]) + 127) / 255);
        }
    }

    // ---- stroke geometry ----
    const QPointF centre = QRectF(dabRect).center();
    QPointF dir = m_lastDir;
    qreal travelled = 0.0;
    if (!m_firstDab) {
        const QPointF d = centre - m_lastCentre;
        travelled = qSqrt(d.x() * d.x() + d.y() * d.y());
        if (travelled > 0.5) dir = d / travelled;
    }

    // ---- brush parameters for this dab ----
    KisWetPaintEngine::Params params;
    params.opacity = float(m_opacityOption.apply(info));
    params.pickup = m_pickupOption.isChecked() ? float(m_pickupOption.computeSizeLikeValue(info)) : 0.0f;
    params.smear = m_smearOption.isChecked() ? float(m_smearOption.computeSizeLikeValue(info)) : 0.0f;
    params.reload = m_reloadOption.isChecked() ? float(m_reloadOption.computeSizeLikeValue(info)) : 0.0f;
    params.depletion = float(m_wetData.depletion / 100.0);
    params.streaks = float(m_wetData.streaks / 100.0);
    params.wetMixing = float(m_wetData.wetMixing / 100.0);
    params.paintWetness = float(m_wetData.paintWetness / 100.0);
    params.rewet = float(m_wetData.rewet / 100.0);
    params.undertone = float(m_wetData.undertone / 100.0);
    params.hiding = float(m_wetData.hiding / 100.0);

    // ---- read the layer and the wet state ----
    const KoColorSpace *cs = device->colorSpace();
    const int pixelSize = cs->pixelSize();

    m_layerBytes.resize(n * pixelSize);
    m_convertedBytes.resize(n * pixelSize);
    m_pixels.resize(n * 4);
    m_under.resize(n * 4);
    m_wetPaint.resize(n * 4);
    m_wetness.resize(n);

    const LayerFormat fmt = layerFormat(cs);
    device->readBytes(m_layerBytes.data(), dabRect);
    if (fmt == LayerFormat::Rgb8Srgb) {
        // same profile and BGRA order: exact 8 -> 16 bit widening, no LCMS
        const quint8 *src = m_layerBytes.constData();
        quint16 *dst = m_pixels.data();
        for (int i = 0; i < n * 4; i++) dst[i] = quint16(src[i]) * 257;
    } else if (fmt == LayerFormat::Rgb16Srgb) {
        memcpy(m_pixels.data(), m_layerBytes.constData(), size_t(n) * 8);
    } else {
        cs->toRgbA16(m_layerBytes.data(), reinterpret_cast<quint8 *>(m_pixels.data()), quint32(n));
    }
    m_data->underDevice->readBytes(reinterpret_cast<quint8 *>(m_under.data()), dabRect);
    m_data->wetPaintDevice->readBytes(reinterpret_cast<quint8 *>(m_wetPaint.data()), dabRect);
    m_data->wetnessDevice->readBytes(m_wetness.data(), dabRect);

    KisWetPaintEngine::Dab dab;
    dab.width = w;
    dab.height = h;
    dab.pixels = m_pixels.data();
    dab.under = m_under.data();
    dab.wetPaint = m_wetPaint.data();
    dab.wetness = m_wetness.data();
    dab.mask = m_mask.data();
    dab.centreX = float(scatteredPos.x() - dabRect.x());
    dab.centreY = float(scatteredPos.y() - dabRect.y());
    dab.dirX = float(dir.x());
    dab.dirY = float(dir.y());
    dab.radius = float(0.5 * qMax(maskW, maskH));
    dab.travelled = float(travelled);

    if (!m_firstDab && params.smear > 0.0f) {
        // wet paint at the previous dab position, to be dragged along
        const QRect srcRect = dabRect.translated((m_lastCentre - centre).toPoint());
        m_smearWetPaint.resize(n * 4);
        m_smearWetness.resize(n);
        m_data->wetPaintDevice->readBytes(reinterpret_cast<quint8 *>(m_smearWetPaint.data()), srcRect);
        m_data->wetnessDevice->readBytes(m_smearWetness.data(), srcRect);
        dab.smearWetPaint = m_smearWetPaint.data();
        dab.smearWetness = m_smearWetness.data();
    }

    // ---- simulate ----
    const KisWetPaintEngine::DabContext ctx = m_engine.prepareDab(dab, params);

    float used = 0.0f;
    const int threads = qBound(1, qMin(QThread::idealThreadCount(), h / MinRowsPerChunk), 8);
    if (n < ParallelPixelThreshold || threads < 2) {
        used = m_engine.paintRows(dab, params, ctx, 0, h);
    } else {
        // rows are independent once the brush state is fixed for this dab
        QVector<int> chunks(threads);
        QVector<float> chunkUsed(threads, 0.0f);
        for (int i = 0; i < threads; i++) chunks[i] = i;
        const KisWetPaintEngine &engine = m_engine;
        QtConcurrent::blockingMap(chunks, [&](int &i) {
            const int y0 = h * i / threads;
            const int y1 = h * (i + 1) / threads;
            chunkUsed[i] = engine.paintRows(dab, params, ctx, y0, y1);
        });
        for (float u : chunkUsed) used += u;
    }

    m_engine.finishDab(dab, params, used);

    // ---- write back ----
    // Only pixels under the brush are replaced, so a colour space round trip
    // through 16-bit sRGB can never creep into pixels we didn't paint.
    if (fmt == LayerFormat::Rgb8Srgb) {
        const quint16 *src = m_pixels.constData();
        quint8 *dst = m_layerBytes.data();
        for (int i = 0; i < n; i++) {
            if (!m_mask[i]) continue;
            for (int c = 0; c < 4; c++) {
                dst[i * 4 + c] = quint8((quint32(src[i * 4 + c]) * 255u + 32767u) / 65535u);
            }
        }
    } else if (fmt == LayerFormat::Rgb16Srgb) {
        quint16 *dst = reinterpret_cast<quint16 *>(m_layerBytes.data());
        for (int i = 0; i < n; i++) {
            if (m_mask[i]) memcpy(dst + i * 4, m_pixels.constData() + i * 4, 8);
        }
    } else {
        cs->fromRgbA16(reinterpret_cast<const quint8 *>(m_pixels.data()), m_convertedBytes.data(), quint32(n));
        for (int i = 0; i < n; i++) {
            if (m_mask[i]) {
                memcpy(m_layerBytes.data() + i * pixelSize, m_convertedBytes.data() + i * pixelSize, size_t(pixelSize));
            }
        }
    }
    device->writeBytes(m_layerBytes.data(), dabRect);
    m_data->underDevice->writeBytes(reinterpret_cast<const quint8 *>(m_under.data()), dabRect);
    m_data->wetPaintDevice->writeBytes(reinterpret_cast<const quint8 *>(m_wetPaint.data()), dabRect);
    m_data->wetnessDevice->writeBytes(m_wetness.data(), dabRect);

    painter()->addDirtyRect(dabRect);

    m_lastCentre = centre;
    m_lastDir = dir;
    m_firstDab = false;

    return spacingInfo;
}

KisSpacingInformation KisWetPaintOp::updateSpacingImpl(const KisPaintInformation &info) const
{
    const qreal scale = m_sizeOption.apply(info) * KisLodTransform::lodToScale(painter()->device());
    const qreal rotation = m_rotationOption.apply(info);
    return effectiveSpacing(scale, rotation, m_spacingOption, info);
}

KisInterstrokeDataFactory *KisWetPaintOp::createInterstrokeDataFactory(const KisPaintOpSettingsSP settings,
                                                                       KisResourcesInterfaceSP resourcesInterface)
{
    Q_UNUSED(settings);
    Q_UNUSED(resourcesInterface);
    return new WetPaintInterstrokeDataFactory();
}
