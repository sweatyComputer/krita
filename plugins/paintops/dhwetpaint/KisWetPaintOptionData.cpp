/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisWetPaintOptionData.h"

#include "kis_properties_configuration.h"

namespace {
const QString WETPAINT_DRYING_TIME = "WetPaint/dryingTime";
const QString WETPAINT_DEPLETION = "WetPaint/depletion";
const QString WETPAINT_STREAKS = "WetPaint/streaks";
const QString WETPAINT_WET_MIXING = "WetPaint/wetMixing";
const QString WETPAINT_PAINT_WETNESS = "WetPaint/paintWetness";
const QString WETPAINT_CLEAN_BRUSH = "WetPaint/cleanBrush";
const QString WETPAINT_REWET = "WetPaint/rewet";
const QString WETPAINT_UNDERTONE = "WetPaint/undertone";
const QString WETPAINT_HIDING = "WetPaint/hiding";
}

bool KisWetPaintOptionData::read(const KisPropertiesConfiguration *setting)
{
    dryingTime = setting->getDouble(WETPAINT_DRYING_TIME, 60.0);
    depletion = setting->getDouble(WETPAINT_DEPLETION, 50.0);
    streaks = setting->getDouble(WETPAINT_STREAKS, 60.0);
    wetMixing = setting->getDouble(WETPAINT_WET_MIXING, 60.0);
    paintWetness = setting->getDouble(WETPAINT_PAINT_WETNESS, 100.0);
    cleanBrush = setting->getBool(WETPAINT_CLEAN_BRUSH, true);
    rewet = setting->getDouble(WETPAINT_REWET, 0.0);
    // presets saved before this option existed keep their old look
    undertone = setting->getDouble(WETPAINT_UNDERTONE, 0.0);
    hiding = setting->getDouble(WETPAINT_HIDING, 50.0);
    return true;
}

void KisWetPaintOptionData::write(KisPropertiesConfiguration *setting) const
{
    setting->setProperty(WETPAINT_DRYING_TIME, dryingTime);
    setting->setProperty(WETPAINT_DEPLETION, depletion);
    setting->setProperty(WETPAINT_STREAKS, streaks);
    setting->setProperty(WETPAINT_WET_MIXING, wetMixing);
    setting->setProperty(WETPAINT_PAINT_WETNESS, paintWetness);
    setting->setProperty(WETPAINT_CLEAN_BRUSH, cleanBrush);
    setting->setProperty(WETPAINT_REWET, rewet);
    setting->setProperty(WETPAINT_UNDERTONE, undertone);
    setting->setProperty(WETPAINT_HIDING, hiding);
}
