/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "DhComfyPlugin.h"

#include <kpluginfactory.h>
#include <KoDockFactoryBase.h>
#include <KoDockRegistry.h>

#include "DhComfyDock.h"

K_PLUGIN_FACTORY_WITH_JSON(DhComfyPluginFactory, "krita_dhcomfy.json", registerPlugin<DhComfyPlugin>();)

namespace {
class DhComfyDockFactory : public KoDockFactoryBase
{
public:
    QString id() const override { return QStringLiteral("DhComfyDocker"); }
    virtual Qt::DockWidgetArea defaultDockWidgetArea() const { return Qt::RightDockWidgetArea; }
    QDockWidget *createDockWidget() override
    {
        DhComfyDock *dock = new DhComfyDock();
        dock->setObjectName(id());
        return dock;
    }
    DockPosition defaultDockPosition() const override { return DockRight; }
};
}

DhComfyPlugin::DhComfyPlugin(QObject *parent, const QVariantList &)
    : QObject(parent)
{
    KoDockRegistry::instance()->add(new DhComfyDockFactory());
}

DhComfyPlugin::~DhComfyPlugin()
{
}

#include "DhComfyPlugin.moc"
