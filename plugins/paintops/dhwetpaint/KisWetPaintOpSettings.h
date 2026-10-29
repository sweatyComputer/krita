/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_WET_PAINT_OP_SETTINGS_H
#define KIS_WET_PAINT_OP_SETTINGS_H

#include <QPointer>
#include <QScopedPointer>
#include <kis_brush_based_paintop_settings.h>

class KisWetPaintOpSettings : public KisBrushBasedPaintOpSettings
{
public:
    KisWetPaintOpSettings(KisResourcesInterfaceSP resourcesInterface);
    ~KisWetPaintOpSettings() override;

    /// quick sliders for the tool options docker / brush HUD
    QList<KisUniformPaintOpPropertySP> uniformProperties(KisPaintOpSettingsSP settings,
                                                         QPointer<KisPaintOpPresetUpdateProxy> updateProxy) override;

private:
    struct Private;
    const QScopedPointer<Private> m_d;
};

typedef KisSharedPtr<KisWetPaintOpSettings> KisWetPaintOpSettingsSP;

#endif // KIS_WET_PAINT_OP_SETTINGS_H
