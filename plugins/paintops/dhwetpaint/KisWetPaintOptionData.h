/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_WET_PAINT_OPTION_DATA_H
#define KIS_WET_PAINT_OPTION_DATA_H

#include "kis_types.h"
#include <boost/operators.hpp>
#include <QtGlobal>

class KisPropertiesConfiguration;

/**
 * The non-pressure-dependent settings of the wet paint engine.
 * Percentages are stored as 0..100 (what the UI shows).
 */
struct KisWetPaintOptionData : boost::equality_comparable<KisWetPaintOptionData>
{
    inline friend bool operator==(const KisWetPaintOptionData &lhs, const KisWetPaintOptionData &rhs) {
        return qFuzzyCompare(lhs.dryingTime, rhs.dryingTime)
            && qFuzzyCompare(lhs.depletion, rhs.depletion)
            && qFuzzyCompare(lhs.streaks, rhs.streaks)
            && qFuzzyCompare(lhs.wetMixing, rhs.wetMixing)
            && qFuzzyCompare(lhs.paintWetness, rhs.paintWetness)
            && qFuzzyCompare(1.0 + lhs.rewet, 1.0 + rhs.rewet)
            && qFuzzyCompare(1.0 + lhs.undertone, 1.0 + rhs.undertone)
            && qFuzzyCompare(1.0 + lhs.hiding, 1.0 + rhs.hiding)
            && lhs.cleanBrush == rhs.cleanBrush;
    }

    qreal dryingTime {60.0};     ///< seconds for wetness to fall to ~37%; 0 = never dries
    qreal depletion {50.0};      ///< % how fast the brush runs out of paint
    qreal streaks {60.0};        ///< % dry-brush bristle streaks
    qreal wetMixing {60.0};      ///< % how much wet paint dilutes new paint
    qreal paintWetness {100.0};  ///< % wetness of freshly laid paint
    qreal rewet {0.0};           ///< % how much dry paint the brush re-wets (blenders)
    qreal undertone {80.0};      ///< % thin paint shows its glazing undertone (KM layer)
    qreal hiding {50.0};         ///< % hiding power: how fast paint reaches full masstone
    bool cleanBrush {true};      ///< reload a clean brush at every stroke

    bool read(const KisPropertiesConfiguration *setting);
    void write(KisPropertiesConfiguration *setting) const;
};

#endif // KIS_WET_PAINT_OPTION_DATA_H
