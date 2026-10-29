/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_WET_PAINT_OP_H
#define KIS_WET_PAINT_OP_H

#include <QPointF>
#include <QRect>
#include <QVector>

#include <kis_brush_based_paintop.h>
#include <kis_types.h>

#include <KisOpacityOption.h>
#include <KisSpacingOption.h>
#include <KisScatterOption.h>
#include <KisRotationOption.h>
#include <KisStandardOptions.h>

#include "KisWetPaintEngine.h"
#include "KisWetPaintOptionData.h"
#include "KisWetPaintCurveOptionData.h"
#include "KisInterstrokeData.h"

class KisInterstrokeDataFactory;
struct KisWetPaintInterstrokeData;

/**
 * Wet paint brush engine: Kubelka-Munk pigment mixing with paint pickup,
 * smearing, depletion, dry-brush streaks and time-based drying.
 *
 * The simulation itself lives in KisWetPaintEngine (no Krita types); this
 * class reads the layer and the per-layer wet state around each dab, hands
 * the buffers to the engine, and writes the result back.
 */
class KisWetPaintOp : public KisBrushBasedPaintOp
{
public:
    KisWetPaintOp(const KisPaintOpSettingsSP settings, KisPainter *painter, KisNodeSP node, KisImageSP image);
    ~KisWetPaintOp() override;

    static KisInterstrokeDataFactory *createInterstrokeDataFactory(const KisPaintOpSettingsSP settings,
                                                                   KisResourcesInterfaceSP resourcesInterface);

protected:
    KisSpacingInformation paintAt(const KisPaintInformation &info) override;
    KisSpacingInformation updateSpacingImpl(const KisPaintInformation &info) const override;

private:
    KisSizeOption m_sizeOption;
    KisRatioOption m_ratioOption;
    KisOpacityOption m_opacityOption;
    KisSpacingOption m_spacingOption;
    KisRotationOption m_rotationOption;
    KisScatterOption m_scatterOption;
    KisStandardOptionNoApply<KisWetPickupOptionData> m_pickupOption;
    KisStandardOptionNoApply<KisWetSmearOptionData> m_smearOption;
    KisStandardOptionNoApply<KisWetReloadOptionData> m_reloadOption;
    KisWetPaintOptionData m_wetData;

    KisInterstrokeDataSP m_dataHolder;              ///< keeps the layer's data alive
    KisWetPaintInterstrokeData *m_data {nullptr};

    KisWetPaintEngine m_engine;

    bool m_firstDab {true};
    QPointF m_lastCentre;
    QPointF m_lastDir {1.0, 0.0};

    // per-dab scratch buffers, reused between dabs
    QVector<quint8> m_layerBytes;
    QVector<quint8> m_convertedBytes;
    QVector<quint16> m_pixels;
    QVector<quint16> m_under;
    QVector<quint16> m_wetPaint;
    QVector<quint8> m_wetness;
    QVector<quint16> m_smearWetPaint;
    QVector<quint8> m_smearWetness;
    QVector<quint8> m_mask;
    QVector<quint8> m_selection;
};

#endif // KIS_WET_PAINT_OP_H
