/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisWetPaintOpSettings.h"

#include <QPointer>

#include <klocalizedstring.h>
#include <kis_pointer_utils.h>
#include <brushengine/kis_uniform_paintop_property.h>
#include "KisPaintOpPresetUpdateProxy.h"
#include "KisCurveOptionDataUniformProperty.h"

#include "KisWetPaintCurveOptionData.h"
#include "KisWetPaintOptionData.h"

struct KisWetPaintOpSettings::Private
{
    QList<KisUniformPaintOpPropertyWSP> uniformProperties;
};

KisWetPaintOpSettings::KisWetPaintOpSettings(KisResourcesInterfaceSP resourcesInterface)
    : KisBrushBasedPaintOpSettings(resourcesInterface)
    , m_d(new Private)
{
}

KisWetPaintOpSettings::~KisWetPaintOpSettings()
{
}

QList<KisUniformPaintOpPropertySP> KisWetPaintOpSettings::uniformProperties(KisPaintOpSettingsSP settings,
                                                                            QPointer<KisPaintOpPresetUpdateProxy> updateProxy)
{
    QList<KisUniformPaintOpPropertySP> props = listWeakToStrong(m_d->uniformProperties);

    if (props.isEmpty()) {
        auto addCurve = [&](const KisCurveOptionData &data, const QString &id) {
            KisCurveOptionDataUniformProperty *prop =
                new KisCurveOptionDataUniformProperty(data, id, settings, 0);
            QObject::connect(updateProxy, SIGNAL(sigSettingsChanged()), prop, SLOT(requestReadValue()));
            prop->requestReadValue();
            props << toQShared(prop);
        };

        addCurve(KisWetPickupOptionData(), "wetpaint_pickup");
        addCurve(KisWetSmearOptionData(), "wetpaint_smear");
        addCurve(KisWetReloadOptionData(), "wetpaint_reload");

        {
            KisUniformPaintOpPropertyCallback *prop =
                new KisUniformPaintOpPropertyCallback(KisUniformPaintOpPropertyCallback::Bool,
                                                      KoID("wetpaint_clean_brush", i18n("Clean Brush")),
                                                      settings, 0);
            prop->setReadCallback([](KisUniformPaintOpProperty *prop) {
                KisWetPaintOptionData data;
                data.read(prop->settings().data());
                prop->setValue(data.cleanBrush);
            });
            prop->setWriteCallback([](KisUniformPaintOpProperty *prop) {
                KisWetPaintOptionData data;
                data.read(prop->settings().data());
                data.cleanBrush = prop->value().toBool();
                data.write(prop->settings().data());
            });
            QObject::connect(updateProxy, SIGNAL(sigSettingsChanged()), prop, SLOT(requestReadValue()));
            prop->requestReadValue();
            props << toQShared(prop);
        }

        m_d->uniformProperties = listStrongToWeak(props);
    }

    return KisBrushBasedPaintOpSettings::uniformProperties(settings, updateProxy) + props;
}
