/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DHTOOLS_H
#define DHTOOLS_H

#include <QObject>
#include <QPointer>
#include <QVariant>
#include <KisActionPlugin.h>

class KisAction;
class DhLayerCycle;
class DhRadialMenu;
class KisView;

/**
 * Native port of the dh_krita_tools / dh_radial_menu Python plugins.
 *
 * Everything here used to live in pykrita, which does not exist on Android.
 * Rewriting them as a normal Krita extension plugin means they are ordinary
 * KisActions: they show up in Settings -> Configure Shortcuts, in the touch
 * docker, and in the radial menu, with no interpreter in sight.
 */
class DhTools : public KisActionPlugin
{
    Q_OBJECT
public:
    DhTools(QObject *parent, const QVariantList &);
    ~DhTools() override;

private Q_SLOTS:
    void slotHideSelected();
    void slotLayerAbove();
    void slotLayerBelow();
    void slotThirdsGuides();
    void slotAddLevelsLayer();
    void slotAddHsvLayer();
    void slotToggleLayerCycle(bool checked);
    void slotLayerCycleDisarmed();
    void slotShowRadialMenu();
    void slotOpenCurrentDirectory();
    void slotViewAdded(KisView *view);

private:
    void insertFilterLayer(const QString &layerName, const QString &filterId);
    void attachBrushHud(KisView *view);

    QPointer<KisAction> m_actionToggleCycle;
    DhLayerCycle *m_layerCycle{nullptr};
    QPointer<DhRadialMenu> m_radialMenu;
};

#endif // DHTOOLS_H
