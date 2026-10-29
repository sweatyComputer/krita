/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_WET_PAINT_OPTION_WIDGET_H
#define KIS_WET_PAINT_OPTION_WIDGET_H

#include <QScopedPointer>

#include <kis_paintop_option.h>
#include <lager/cursor.hpp>

#include "KisWetPaintOptionData.h"

class KisWetPaintOptionWidget : public KisPaintOpOption
{
public:
    using data_type = KisWetPaintOptionData;

    KisWetPaintOptionWidget(lager::cursor<KisWetPaintOptionData> optionData);
    ~KisWetPaintOptionWidget() override;

    void writeOptionSetting(KisPropertiesConfigurationSP setting) const override;
    void readOptionSetting(const KisPropertiesConfigurationSP setting) override;

private:
    struct Private;
    const QScopedPointer<Private> m_d;
};

#endif // KIS_WET_PAINT_OPTION_WIDGET_H
