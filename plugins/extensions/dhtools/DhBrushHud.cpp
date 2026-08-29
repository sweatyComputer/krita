/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "DhBrushHud.h"

#include <QCursor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>
#include <QWidget>
#include <QtMath>

#include <klocalizedstring.h>
#include <kconfiggroup.h>
#include <ksharedconfig.h>

#include <KoCanvasResourceProvider.h>
#include <KoCanvasResourcesIds.h>
#include <KoColor.h>

#include <kis_canvas2.h>
#include <kis_coordinates_converter.h>
#include <KisView.h>
#include <kis_debug.h>

#include <brushengine/kis_paintop_preset.h>
#include <brushengine/kis_paintop_settings.h>
#include <brushengine/kis_paint_information.h>

const QString DhBrushHud::ID = QStringLiteral("dh_brush_hud");

namespace {
const char *CONFIG_GROUP = "DhBrushHud";

// Repaint cadence while fading. 60fps is pointless for a fading disc.
const int TICK_MS = 33;
}

DhBrushHud::DhBrushHud(QPointer<KisView> view)
    : KisCanvasDecoration(ID, view)
{
    reloadSettings();

    m_repaint.setInterval(TICK_MS);
    connect(&m_repaint, SIGNAL(timeout()), this, SLOT(slotFadeTick()));

    if (view && view->canvasBase() && view->canvasBase()->resourceManager()) {
        connect(view->canvasBase()->resourceManager(),
                SIGNAL(canvasResourceChanged(int, const QVariant &)),
                this, SLOT(slotCanvasResourceChanged(int, const QVariant &)));
    }

    setVisible(true);
}

DhBrushHud::~DhBrushHud()
{
}

void DhBrushHud::reloadSettings()
{
    KConfigGroup cfg = KSharedConfig::openConfig()->group(CONFIG_GROUP);
    m_enabled        = cfg.readEntry("enabled", true);
    m_showForSize    = cfg.readEntry("showForSize", true);
    m_showForOpacity = cfg.readEntry("showForOpacity", true);
    m_holdMs         = qBound(0,   cfg.readEntry("holdMs", 450),  5000);
    m_fadeMs         = qBound(50,  cfg.readEntry("fadeMs", 350),  5000);

    // Turn this off to fall back to a plain circle, which is what you want if a
    // paintop ever misbehaves when asked for its outline outside a tool.
    m_useBrushOutline = cfg.readEntry("useBrushOutline", true);
}

void DhBrushHud::slotCanvasResourceChanged(int key, const QVariant &value)
{
    if (key == KoCanvasResource::Size) {
        const qreal size = value.toReal();
        const bool changed = !qFuzzyCompare(size, m_size);
        m_size = size;
        if (changed && !m_priming && m_showForSize) trigger(SizeReadout);
    } else if (key == KoCanvasResource::Opacity) {
        const qreal opacity = value.toReal();
        const bool changed = !qFuzzyCompare(opacity, m_opacity);
        m_opacity = opacity;
        if (changed && !m_priming && m_showForOpacity) trigger(OpacityReadout);
    } else {
        return;
    }

    m_priming = false;
}

void DhBrushHud::recomputeOutline()
{
    m_outline = KisOptimizedBrushOutline();

    if (!m_useBrushOutline) return;

    KisCanvas2 *canvas = qobject_cast<KisCanvas2 *>(view() ? view()->canvasBase() : nullptr);
    if (!canvas || !canvas->resourceManager()) return;

    KisPaintOpPresetSP preset =
        canvas->resourceManager()->resource(KoCanvasResource::CurrentPaintOpPreset)
              .value<KisPaintOpPresetSP>();
    if (!preset || !preset->settings()) return;

    // Ask the paintop for its real outline -- the actual stamp shape, angle and
    // all -- rather than assuming every brush is a circle. Built at the origin
    // so the same path can be re-anchored wherever the cursor happens to be.
    //
    // Tools register a KisDistanceInformation before calling this; we have no
    // stroke to take one from. KisPaintInformation::drawingAngle() warns and
    // returns zero in that case rather than asserting, which for a static
    // preview is exactly the behaviour we want.
    KisPaintInformation info(QPointF(0.0, 0.0), 1.0);

    KisPaintOpSettings::OutlineMode mode;
    mode.isVisible = true;
    mode.forceFullSize = true;

    m_outline = preset->settings()->brushOutline(info, mode, 1.0);
}

void DhBrushHud::trigger(Readout what)
{
    if (!m_enabled) return;

    recomputeOutline();
    m_showing = what;
    m_since.restart();

    if (!m_repaint.isActive()) m_repaint.start();

    slotFadeTick();
}

void DhBrushHud::slotFadeTick()
{
    KisCanvas2 *canvas = qobject_cast<KisCanvas2 *>(view() ? view()->canvasBase() : nullptr);
    if (!canvas) {
        m_repaint.stop();
        return;
    }

    if (currentAlpha() <= 0.0) {
        m_showing = NoReadout;
        m_repaint.stop();
    }

    canvas->updateCanvas();
}

qreal DhBrushHud::currentAlpha() const
{
    if (m_showing == NoReadout || !m_since.isValid()) return 0.0;

    const qint64 elapsed = m_since.elapsed();
    if (elapsed <= m_holdMs) return 1.0;

    const qreal t = qreal(elapsed - m_holdMs) / qreal(m_fadeMs);
    return t >= 1.0 ? 0.0 : 1.0 - t;
}

QPointF DhBrushHud::anchorPoint(KisCanvas2 *canvas) const
{
    QWidget *widget = canvas->canvasWidget();
    if (!widget) return QPointF(0, 0);

    // Prefer wherever the pointer actually is. On a touch screen that is the
    // last place a finger was, which is close enough to feel anchored; if it
    // is nowhere sensible, fall back to the middle of the canvas.
    const QPoint local = widget->mapFromGlobal(QCursor::pos());
    if (widget->rect().contains(local)) {
        return QPointF(local);
    }
    return QPointF(widget->rect().center());
}

void DhBrushHud::drawDecoration(QPainter &gc, const QRectF &updateArea,
                                const KisCoordinatesConverter *converter,
                                KisCanvas2 *canvas)
{
    Q_UNUSED(updateArea);

    const qreal alpha = currentAlpha();
    if (alpha <= 0.0 || !canvas || !converter) return;

    // The painter arrives in document space; the HUD is a screen-space overlay
    // that must not scale, rotate or mirror with the canvas.
    gc.save();
    gc.setTransform(converter->documentToWidgetTransform(), true);
    gc.setRenderHint(QPainter::Antialiasing, true);

    qreal scaleX = 1.0, scaleY = 1.0;
    converter->imageScale(&scaleX, &scaleY);

    const QPointF centre = anchorPoint(canvas);

    // Brush size is in image pixels; on screen it is that times the zoom.
    qreal radius = (m_size * 0.5) * scaleX;
    radius = qBound(6.0, radius, qreal(qMin(canvas->canvasWidget()->width(),
                                            canvas->canvasWidget()->height())) * 0.45);

    KoColor fg;
    if (canvas->resourceManager()) {
        fg = canvas->resourceManager()->resource(KoCanvasResource::ForegroundColor).value<KoColor>();
    }
    const QColor brushColour = fg.toQColor();

    QColor fill = brushColour;
    fill.setAlphaF(qBound(0.0, m_opacity, 1.0) * alpha);

    // Two-tone outline so the shape survives both white paper and black ink.
    QColor dark(0, 0, 0);        dark.setAlphaF(0.75 * alpha);
    QColor light(255, 255, 255); light.setAlphaF(0.9 * alpha);

    qreal drawnRadius = radius;

    if (!m_outline.isEmpty()) {
        // Re-anchor the origin-centred outline under the cursor. Take only the
        // linear part of the canvas transform -- zoom, rotation and mirroring
        // should all apply to the brush shape, but its position must not, since
        // the HUD lives in screen space.
        const QTransform canvasTransform = converter->imageToWidgetTransform();
        const QTransform linear(canvasTransform.m11(), canvasTransform.m12(),
                                canvasTransform.m21(), canvasTransform.m22(),
                                0.0, 0.0);

        KisOptimizedBrushOutline shaped = m_outline;
        shaped.map(linear * QTransform::fromTranslate(centre.x(), centre.y()));

        if (m_showing == OpacityReadout) {
            QPainterPath filled;
            for (auto it = shaped.begin(); it != shaped.end(); ++it) {
                filled.addPolygon(*it);
            }
            filled.setFillRule(Qt::WindingFill);
            gc.setPen(Qt::NoPen);
            gc.setBrush(fill);
            gc.drawPath(filled);
        }

        gc.setBrush(Qt::NoBrush);
        for (int pass = 0; pass < 2; ++pass) {
            gc.setPen(pass == 0 ? QPen(dark, 3.0) : QPen(light, 1.4));
            for (auto it = shaped.begin(); it != shaped.end(); ++it) {
                gc.drawPolyline(*it);
            }
        }

        const QRectF bounds = shaped.boundingRect();
        if (bounds.isValid()) {
            drawnRadius = qMax(bounds.width(), bounds.height()) / 2.0;
        }
    } else {
        // No usable outline from the paintop: a circle still tells you the size.
        if (m_showing == OpacityReadout) {
            gc.setPen(Qt::NoPen);
            gc.setBrush(fill);
            gc.drawEllipse(centre, radius, radius);
        }
        gc.setBrush(Qt::NoBrush);
        gc.setPen(QPen(dark, 3.0));
        gc.drawEllipse(centre, radius, radius);
        gc.setPen(QPen(light, 1.4));
        gc.drawEllipse(centre, radius, radius);
    }

    // Crosshair so the anchor is unambiguous when the brush is large.
    const qreal tick = qMin(6.0, drawnRadius * 0.25);
    gc.setPen(QPen(light, 1.2));
    gc.drawLine(QPointF(centre.x() - tick, centre.y()), QPointF(centre.x() + tick, centre.y()));
    gc.drawLine(QPointF(centre.x(), centre.y() - tick), QPointF(centre.x(), centre.y() + tick));

    // Numeric readout under the ring, for when you want the exact figure.
    const QString label = (m_showing == OpacityReadout)
        ? i18n("%1%", QString::number(qBound(0.0, m_opacity, 1.0) * 100.0, 'f', 0))
        : i18n("%1 px", QString::number(m_size, 'f', m_size < 10.0 ? 1 : 0));

    QFont font = gc.font();
    font.setPointSizeF(qMax(9.0, font.pointSizeF()));
    font.setBold(true);
    gc.setFont(font);

    const QFontMetrics metrics(font);
    const QRectF textRect = QRectF(metrics.boundingRect(label))
        .adjusted(-6, -3, 6, 3)
        .translated(centre.x() - metrics.horizontalAdvance(label) / 2.0,
                    centre.y() + drawnRadius + 10.0 + metrics.ascent());

    QColor plate(0, 0, 0); plate.setAlphaF(0.62 * alpha);
    gc.setPen(Qt::NoPen);
    gc.setBrush(plate);
    gc.drawRoundedRect(textRect, 4, 4);

    gc.setPen(light);
    gc.drawText(textRect, Qt::AlignCenter, label);

    gc.restore();
}
