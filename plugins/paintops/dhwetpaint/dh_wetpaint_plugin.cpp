/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dh_wetpaint_plugin.h"

#include <klocalizedstring.h>
#include <kpluginfactory.h>

#include <brushengine/kis_paintop_registry.h>
#include "kis_simple_paintop_factory.h"

#include "KisWetPaintOp.h"
#include "KisWetPaintOpSettings.h"
#include "KisWetPaintOpSettingsWidget.h"

K_PLUGIN_FACTORY_WITH_JSON(DhWetPaintPluginFactory, "kritadhwetpaintop.json", registerPlugin<DhWetPaintPlugin>();)

DhWetPaintPlugin::DhWetPaintPlugin(QObject *parent, const QVariantList &)
    : QObject(parent)
{
    KisPaintOpRegistry::instance()->add(
        new KisSimplePaintOpFactory<KisWetPaintOp, KisWetPaintOpSettings, KisWetPaintOpSettingsWidget>(
            "dhwetpaint", i18n("Wet Paint (DH)"), KisPaintOpFactory::categoryStable(),
            "krita-colorsmudge.png", QString(), QStringList(), 3,
            /* lodSizeThresholdSupported = */ false));
}

DhWetPaintPlugin::~DhWetPaintPlugin()
{
}

#include "dh_wetpaint_plugin.moc"
