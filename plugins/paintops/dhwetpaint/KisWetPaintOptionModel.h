/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_WET_PAINT_OPTION_MODEL_H
#define KIS_WET_PAINT_OPTION_MODEL_H

#include <QObject>

#include <lager/cursor.hpp>
#include <lager/extra/qt.hpp>

#include "KisWetPaintOptionData.h"

class KisWetPaintOptionModel : public QObject
{
    Q_OBJECT
public:
    KisWetPaintOptionModel(lager::cursor<KisWetPaintOptionData> optionData);

    lager::cursor<KisWetPaintOptionData> optionData;

    LAGER_QT_CURSOR(qreal, dryingTime);
    LAGER_QT_CURSOR(qreal, depletion);
    LAGER_QT_CURSOR(qreal, streaks);
    LAGER_QT_CURSOR(qreal, wetMixing);
    LAGER_QT_CURSOR(qreal, paintWetness);
    LAGER_QT_CURSOR(bool, cleanBrush);
    LAGER_QT_CURSOR(qreal, rewet);
    LAGER_QT_CURSOR(qreal, undertone);
    LAGER_QT_CURSOR(qreal, hiding);
};

#endif // KIS_WET_PAINT_OPTION_MODEL_H
