/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_WET_PAINT_CURVE_OPTION_DATA_H
#define KIS_WET_PAINT_CURVE_OPTION_DATA_H

#include <klocalizedstring.h>
#include <KisCurveOptionData.h>

/**
 * Pressure/tilt/speed-driven controls of the wet paint engine. They use
 * Krita's standard curve option machinery, so every sensor works.
 */

struct KisWetPickupOptionData : KisCurveOptionData
{
    KisWetPickupOptionData()
        : KisCurveOptionData(
              KoID("WetPaintPickup", i18nc("wet paint brush option", "Pickup")),
              Checkability::Checkable, true)
    {
        strengthValue = 0.5;
    }
};

struct KisWetSmearOptionData : KisCurveOptionData
{
    KisWetSmearOptionData()
        : KisCurveOptionData(
              KoID("WetPaintSmear", i18nc("wet paint brush option", "Smear")),
              Checkability::Checkable, true)
    {
        strengthValue = 0.5;
    }
};

struct KisWetReloadOptionData : KisCurveOptionData
{
    KisWetReloadOptionData()
        : KisCurveOptionData(
              KoID("WetPaintReload", i18nc("wet paint brush option, how much fresh paint the brush gets", "Reload")),
              Checkability::Checkable, true)
    {
        strengthValue = 0.15;
    }
};

#endif // KIS_WET_PAINT_CURVE_OPTION_DATA_H
