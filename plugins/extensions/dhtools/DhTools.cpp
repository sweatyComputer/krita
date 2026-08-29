/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "DhTools.h"
#include "DhNodeUtils.h"
#include "DhLayerCycle.h"
#include "DhRadialMenu.h"
#include "DhBrushHud.h"
#include "KisDhRadialSettings.h"

#include <QAction>
#include <QCursor>
#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

#include <klocalizedstring.h>
#include <kpluginfactory.h>
#include <kactioncollection.h>

#include <kis_action.h>
#include <kis_action_manager.h>
#include <KisViewManager.h>
#include <KisMainWindow.h>
#include <KisPart.h>
#include <KisDocument.h>
#include <kis_node_manager.h>
#include <kis_node_commands_adapter.h>
#include <kis_image.h>
#include <kis_node.h>
#include <kis_mask.h>
#include <kis_layer.h>
#include <kis_selection.h>
#include <kis_adjustment_layer.h>
#include <kis_layer_properties_icons.h>
#include <filter/kis_filter.h>
#include <filter/kis_filter_registry.h>
#include <filter/kis_filter_configuration.h>
#include <KisGlobalResourcesInterface.h>
#include <canvas/kis_guides_config.h>
#include <kis_canvas2.h>
#include <KisView.h>
#include <kis_canvas_decoration.h>
#include <kis_preference_set_registry.h>
#include <kis_debug.h>

K_PLUGIN_FACTORY_WITH_JSON(DhToolsFactory, "kritadhtools.json", registerPlugin<DhTools>();)

DhTools::DhTools(QObject *parent, const QVariantList &)
    : KisActionPlugin(parent)
{
    connect(createAction("dh_hide_selected"), SIGNAL(triggered()),
            this, SLOT(slotHideSelected()));
    connect(createAction("dh_layer_above"), SIGNAL(triggered()),
            this, SLOT(slotLayerAbove()));
    connect(createAction("dh_layer_below"), SIGNAL(triggered()),
            this, SLOT(slotLayerBelow()));
    connect(createAction("dh_thirds_guides"), SIGNAL(triggered()),
            this, SLOT(slotThirdsGuides()));
    connect(createAction("dh_add_levels_layer"), SIGNAL(triggered()),
            this, SLOT(slotAddLevelsLayer()));
    connect(createAction("dh_add_hsv_layer"), SIGNAL(triggered()),
            this, SLOT(slotAddHsvLayer()));
    connect(createAction("dh_show_radial_menu"), SIGNAL(triggered()),
            this, SLOT(slotShowRadialMenu()));
    connect(createAction("dh_open_current_dir"), SIGNAL(triggered()),
            this, SLOT(slotOpenCurrentDirectory()));

    m_actionToggleCycle = createAction("dh_toggle_layer_cycle");
    m_actionToggleCycle->setCheckable(true);
    connect(m_actionToggleCycle.data(), SIGNAL(toggled(bool)),
            this, SLOT(slotToggleLayerCycle(bool)));

    // "DH Radial Menu" page in Configure Krita, so the wedges are editable on a
    // tablet instead of only by hand-editing JSON you cannot reach.
    KisPreferenceSetRegistry *registry = KisPreferenceSetRegistry::instance();
    registry->add("KisDhRadialSettingsFactory", new KisDhRadialSettingsFactory());

    m_layerCycle = new DhLayerCycle(viewManager(), this);
    connect(m_layerCycle, SIGNAL(disarmed()), this, SLOT(slotLayerCycleDisarmed()));

    // The brush HUD is a per-view canvas decoration, but this plugin is created
    // once per main window, so cover the view that already exists and then watch
    // for any opened later.
    if (KisMainWindow *window = KisPart::instance()->currentMainwindow()) {
        attachBrushHud(window->activeView());
    }
    connect(KisPart::instance(), SIGNAL(sigViewAdded(KisView *)),
            this, SLOT(slotViewAdded(KisView *)), Qt::UniqueConnection);
}

DhTools::~DhTools()
{
    if (m_radialMenu) {
        m_radialMenu->close();
        delete m_radialMenu;
    }
}

void DhTools::slotHideSelected()
{
    KisViewManager *view = viewManager();
    if (!view || !view->image()) return;

    KisNodeManager *nodeManager = view->nodeManager();
    if (!nodeManager) return;

    // Match the Layers docker's notion of "selected": whatever is highlighted
    // there, falling back to the single active node when nothing is.
    KisNodeList nodes = nodeManager->selectedNodes();
    if (nodes.isEmpty()) {
        KisNodeSP active = nodeManager->activeNode();
        if (!active) {
            view->showFloatingMessage(i18n("No layer selected"), QIcon());
            return;
        }
        nodes << active;
    }

    QStringList names;
    for (KisNodeSP node : nodes) {
        if (!node) continue;
        // Unlike Krita's own "toggle visibility" this always hides, so hitting
        // it on a mixed selection leaves you with a predictable result.
        KisLayerPropertiesIcons::setNodePropertyAutoUndo(
            node, KisLayerPropertiesIcons::visible, false, view->image());
        names << node->name();
    }

    view->showFloatingMessage(i18n("Hid: %1", names.join(QStringLiteral(", "))), QIcon());
}

void DhTools::slotLayerAbove()
{
    KisNodeSP node = DhNodeUtils::activateRelativeNode(viewManager(), -1);
    if (!node) {
        viewManager()->showFloatingMessage(i18n("Top of the stack"), QIcon());
    }
}

void DhTools::slotLayerBelow()
{
    KisNodeSP node = DhNodeUtils::activateRelativeNode(viewManager(), +1);
    if (!node) {
        viewManager()->showFloatingMessage(i18n("Bottom of the stack"), QIcon());
    }
}

void DhTools::slotThirdsGuides()
{
    KisViewManager *view = viewManager();
    if (!view || !view->document() || !view->image()) return;

    KisImageSP image = view->image();
    const qreal width = image->width();
    const qreal height = image->height();

    KisGuidesConfig config = view->document()->guidesConfig();
    config.setVerticalGuideLines({ width / 3.0, width * 2.0 / 3.0 });
    config.setHorizontalGuideLines({ height / 3.0, height * 2.0 / 3.0 });
    config.setShowGuides(true);
    view->document()->setGuidesConfig(config);

    view->showFloatingMessage(i18n("Rule of thirds guides added"), QIcon());
}

void DhTools::slotAddLevelsLayer()
{
    insertFilterLayer(i18n("Levels"), QStringLiteral("levels"));
}

void DhTools::slotAddHsvLayer()
{
    insertFilterLayer(i18n("HSV/HSL"), QStringLiteral("hsvadjustment"));
}

void DhTools::insertFilterLayer(const QString &layerName, const QString &filterId)
{
    KisViewManager *view = viewManager();
    if (!view || !view->image()) return;

    KisNodeManager *nodeManager = view->nodeManager();
    if (!nodeManager) return;

    KisFilterSP filter = KisFilterRegistry::instance()->value(filterId);
    if (!filter) {
        warnKrita << "DH tools: no filter registered as" << filterId;
        view->showFloatingMessage(i18n("Filter \"%1\" is not available", filterId), QIcon());
        return;
    }

    KisFilterConfigurationSP config =
        filter->defaultConfiguration(KisGlobalResourcesInterface::instance());

    // A null selection means "affect everything", which is what you want from
    // a one-tap adjustment layer. Krita treats an *empty* selection as fully
    // masked out, so passing 0 is deliberate, not an oversight.
    KisSelectionSP selection = view->selection();

    KisImageSP image = view->image();
    KisAdjustmentLayerSP layer = new KisAdjustmentLayer(
        image, layerName,
        config ? config->cloneWithResourcesSnapshot() : KisFilterConfigurationSP(),
        selection);

    // Adjustment layers cannot live inside a layer's mask stack, so if a mask
    // is active climb out to the layer that owns it before inserting.
    KisNodeSP above = nodeManager->activeNode();
    while (above && above->inherits("KisMask")) {
        above = above->parent();
    }

    KisNodeSP parent = above ? above->parent() : KisNodeSP(image->root());
    if (!parent) parent = image->root();

    KisNodeCommandsAdapter adapter(view);
    adapter.addNode(layer, parent, above);

    nodeManager->slotNonUiActivatedNode(layer);

    // Same as double-clicking the new layer: open its settings so you can
    // actually dial the filter in instead of staring at a no-op adjustment.
    if (KisMainWindow *window = KisPart::instance()->currentMainwindow()) {
        if (KisKActionCollection *collection = window->actionCollection()) {
            if (QAction *properties = collection->action("layer_properties")) {
                properties->trigger();
            }
        }
    }
}

void DhTools::slotToggleLayerCycle(bool checked)
{
    if (!m_layerCycle) return;

    if (checked) {
        if (!m_layerCycle->arm()) {
            // Nothing to cycle through; un-check rather than sit there armed
            // and lying about it.
            if (m_actionToggleCycle) {
                m_actionToggleCycle->setChecked(false);
            }
        }
    } else {
        m_layerCycle->disarm();
    }
}

void DhTools::slotLayerCycleDisarmed()
{
    if (m_actionToggleCycle && m_actionToggleCycle->isChecked()) {
        m_actionToggleCycle->setChecked(false);
    }
}

void DhTools::slotShowRadialMenu()
{
    KisViewManager *view = viewManager();
    if (!view) return;

    // Rebuilt every time rather than reused. A translucent popup that gets
    // hidden and re-shown at a new position tends to keep its old surface on
    // Android, which is what the ghosting was; a fresh widget cannot ghost.
    if (m_radialMenu) {
        m_radialMenu->close();
        m_radialMenu->deleteLater();
        m_radialMenu.clear();
    }
    QWidget *parent = view->canvasBase() ? view->canvasBase()->canvasWidget() : nullptr;
    m_radialMenu = new DhRadialMenu(parent);

    QPoint position = QCursor::pos();

    // On a touch screen the cursor is wherever the last event left it, which
    // may be nowhere useful. Falling back to the centre of the canvas beats
    // opening the menu off in a corner.
    if (view->canvasBase() && view->canvasBase()->canvasWidget()) {
        QWidget *canvas = view->canvasBase()->canvasWidget();
        if (!canvas->rect().contains(canvas->mapFromGlobal(position))) {
            position = canvas->mapToGlobal(canvas->rect().center());
        }
    }

    m_radialMenu->popupAt(position);
}

void DhTools::slotViewAdded(KisView *view)
{
    attachBrushHud(view);
}

void DhTools::attachBrushHud(KisView *view)
{
    if (!view || !view->canvasBase()) return;

    // Views can be handed to us more than once (split views, a second window on
    // the same document); adding a second HUD would just draw everything twice.
    if (view->canvasBase()->decoration(DhBrushHud::ID)) return;

    KisCanvasDecorationSP hud(new DhBrushHud(view));
    view->canvasBase()->addDecoration(hud);
}

void DhTools::slotOpenCurrentDirectory()
{
    KisViewManager *view = viewManager();
    if (!view || !view->document()) return;

    const QString fileName = view->document()->path();
    if (fileName.isEmpty()) {
        view->showFloatingMessage(i18n("Document has not been saved yet"), QIcon());
        return;
    }

    const QString directory = QFileInfo(fileName).absolutePath();

#ifdef Q_OS_ANDROID
    // Android has no "reveal in file manager" that works on an app-private
    // path, so the honest thing is to tell you where the file is and stop.
    view->showFloatingMessage(directory, QIcon(), 6000);
#else
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(directory))) {
        view->showFloatingMessage(i18n("Could not open %1", directory), QIcon());
    }
#endif
}

#include "DhTools.moc"
