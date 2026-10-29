/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_WET_PAINT_INTERSTROKE_DATA_H
#define KIS_WET_PAINT_INTERSTROKE_DATA_H

#include <memory>

#include <QScopedPointer>

#include "kis_types.h"
#include "KisInterstrokeData.h"

class KisTransaction;
class KUndo2Command;

/**
 * Per-layer wet paint state, carried between strokes by Krita's interstroke
 * data mechanism (the same one Color Smudge uses for its heightmap), so it is
 * undo-safe: every stroke's changes to these devices are part of its undo step.
 *
 *  - underDevice    dry paint below the wet film      (RGBA16 sRGB)
 *  - wetPaintDevice the wet film, alpha = paint amount  (RGBA16 sRGB)
 *  - wetnessDevice  how wet the film still is          (Alpha8, 0 = dry)
 *
 * Krita drops interstroke data whenever the layer is painted with a brush that
 * doesn't use it, or its colour space changes. For wet paint that simply
 * means: everything dries. That's the intended behaviour.
 *
 * Drying is time based: wetness decays exponentially between strokes, applied
 * at the start of each stroke by applyDrying().
 */
struct KisWetPaintInterstrokeData : public KisInterstrokeData
{
    KisPaintDeviceSP underDevice;
    KisPaintDeviceSP wetPaintDevice;
    KisPaintDeviceSP wetnessDevice;

    /// wall-clock time (ms since epoch) drying was last applied
    qint64 lastUpdateMSecs {0};

    KisWetPaintInterstrokeData(KisPaintDeviceSP source);
    ~KisWetPaintInterstrokeData() override;

    void beginTransaction() override;
    KUndo2Command *endTransaction() override;

    /**
     * Let the paint dry for the time elapsed since the last call.
     * \p dryingSeconds is the time for wetness to fall to ~37%; <= 0 means never dry.
     */
    void applyDrying(qreal dryingSeconds);

private:
    std::unique_ptr<KUndo2Command> m_parentCommand;
    QScopedPointer<KisTransaction> m_underTransaction;
    QScopedPointer<KisTransaction> m_wetPaintTransaction;
    QScopedPointer<KisTransaction> m_wetnessTransaction;
};

#endif // KIS_WET_PAINT_INTERSTROKE_DATA_H
