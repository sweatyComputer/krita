/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_WET_PAINT_OP_SETTINGS_WIDGET_H
#define KIS_WET_PAINT_OP_SETTINGS_WIDGET_H

#include <kis_brush_based_paintop_options_widget.h>

class KisWetPaintOpSettingsWidget : public KisBrushBasedPaintopOptionWidget
{
    Q_OBJECT

public:
    KisWetPaintOpSettingsWidget(QWidget *parent,
                                KisResourcesInterfaceSP resourcesInterface,
                                KoCanvasResourcesInterfaceSP canvasResourcesInterface);
    ~KisWetPaintOpSettingsWidget() override;

    KisPropertiesConfigurationSP configuration() const override;
};

#endif // KIS_WET_PAINT_OP_SETTINGS_WIDGET_H
