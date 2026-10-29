/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisWetPaintOpSettingsWidget.h"

#include <klocalizedstring.h>

#include <kis_brush_option_widget.h>
#include <kis_paintop_settings_widget.h>
#include <KisPaintOpOptionWidgetUtils.h>
#include <KisSizeOptionWidget.h>
#include <KisSpacingOptionWidget.h>
#include <KisScatterOptionWidget.h>
#include <KisTextureOptionWidget.h>
#include <KisStandardOptionData.h>

#include "KisWetPaintOpSettings.h"
#include "KisWetPaintOptionWidget.h"
#include "KisWetPaintCurveOptionData.h"

KisWetPaintOpSettingsWidget::KisWetPaintOpSettingsWidget(QWidget *parent,
                                                         KisResourcesInterfaceSP resourcesInterface,
                                                         KoCanvasResourcesInterfaceSP canvasResourcesInterface)
    : KisBrushBasedPaintopOptionWidget(KisBrushOptionWidgetFlag::SupportsPrecision, parent)
{
    Q_UNUSED(canvasResourcesInterface);
    namespace kpowu = KisPaintOpOptionWidgetUtils;

    setObjectName("wet paint option widget");

    // the engine page first: it's what makes this brush this brush
    addPaintOpOption(kpowu::createOptionWidget<KisWetPaintOptionWidget>());

    addPaintOpOption(kpowu::createOpacityOptionWidget());
    addPaintOpOption(kpowu::createOptionWidget<KisSizeOptionWidget>());
    addPaintOpOption(kpowu::createRatioOptionWidget());
    addPaintOpOption(kpowu::createOptionWidget<KisSpacingOptionWidget>());

    addPaintOpOption(kpowu::createCurveOptionWidget(KisWetPickupOptionData(), KisPaintOpOption::GENERAL));
    addPaintOpOption(kpowu::createCurveOptionWidget(KisWetSmearOptionData(), KisPaintOpOption::GENERAL));
    addPaintOpOption(kpowu::createCurveOptionWidget(KisWetReloadOptionData(), KisPaintOpOption::COLOR));

    addPaintOpOption(kpowu::createRotationOptionWidget());
    addPaintOpOption(kpowu::createOptionWidget<KisScatterOptionWidget>());

    // paper grain: the texture is baked into the dab mask by the dab cache
    addPaintOpOption(kpowu::createOptionWidget<KisTextureOptionWidget>(KisTextureOptionData(), resourcesInterface));
}

KisWetPaintOpSettingsWidget::~KisWetPaintOpSettingsWidget()
{
}

KisPropertiesConfigurationSP KisWetPaintOpSettingsWidget::configuration() const
{
    KisWetPaintOpSettingsSP config = new KisWetPaintOpSettings(resourcesInterface());
    config->setProperty("paintop", "dhwetpaint");
    writeConfiguration(config);
    // Instant Preview paints a low-res copy first; the wet state is per layer
    // at full resolution, so keep it off for this engine.
    KisPaintOpSettings::setLodUserAllowed(config, false);
    return config;
}
