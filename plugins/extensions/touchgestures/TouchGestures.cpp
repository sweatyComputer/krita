/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "TouchGestures.h"
#include "KisTouchGestureSettings.h"

#include <kpluginfactory.h>
#include <kis_preference_set_registry.h>
#include <input/KisAndroidGestureBridge.h>

K_PLUGIN_FACTORY_WITH_JSON(TouchGesturesFactory, "kritatouchgestures.json", registerPlugin<TouchGestures>();)

TouchGestures::TouchGestures(QObject *parent, const QVariantList &)
    : KisActionPlugin(parent)
{
    KisPreferenceSetRegistry *registry = KisPreferenceSetRegistry::instance();
    KisTouchGestureSettingsFactory *settingsFactory = new KisTouchGestureSettingsFactory();
    registry->add("KisTouchGestureSettingsFactory", settingsFactory);

    connect(&(settingsFactory->repeater), SIGNAL(settingsUpdated()),
            this, SLOT(slotSettingsChanged()), Qt::UniqueConnection);

    slotSettingsChanged();
}

TouchGestures::~TouchGestures()
{
}

void TouchGestures::slotSettingsChanged()
{
    KisAndroidGestureBridge *bridge = KisAndroidGestureBridge::instance();
    if (bridge) {
        bridge->reloadSettings();
    }
}

#include "TouchGestures.moc"
