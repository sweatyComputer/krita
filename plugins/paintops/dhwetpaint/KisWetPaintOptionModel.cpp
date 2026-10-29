/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisWetPaintOptionModel.h"

KisWetPaintOptionModel::KisWetPaintOptionModel(lager::cursor<KisWetPaintOptionData> _optionData)
    : optionData(_optionData)
    , LAGER_QT(dryingTime) {_optionData[&KisWetPaintOptionData::dryingTime]}
    , LAGER_QT(depletion) {_optionData[&KisWetPaintOptionData::depletion]}
    , LAGER_QT(streaks) {_optionData[&KisWetPaintOptionData::streaks]}
    , LAGER_QT(wetMixing) {_optionData[&KisWetPaintOptionData::wetMixing]}
    , LAGER_QT(paintWetness) {_optionData[&KisWetPaintOptionData::paintWetness]}
    , LAGER_QT(cleanBrush) {_optionData[&KisWetPaintOptionData::cleanBrush]}
    , LAGER_QT(rewet) {_optionData[&KisWetPaintOptionData::rewet]}
    , LAGER_QT(undertone) {_optionData[&KisWetPaintOptionData::undertone]}
    , LAGER_QT(hiding) {_optionData[&KisWetPaintOptionData::hiding]}
{
}
