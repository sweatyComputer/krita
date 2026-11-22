/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisDhRadialSettings.h"
#include "DhRadialPresets.h"
#include "DhRadialIcons.h"

#include <QAction>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QListView>
#include <QSet>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QTreeWidgetItemIterator>
#include <QVector>
#include <QColor>

#include <algorithm>

#include <klocalizedstring.h>
#include <kactioncollection.h>
#include <KisActionsSnapshot.h>
#include <KisPart.h>
#include <KisMainWindow.h>
#include <kis_icon_utils.h>
#include <kis_debug.h>

namespace {

const int COLUMN_LABEL = 0;
const int COLUMN_ACTION = 1;
const int ROLE_COLOUR = Qt::UserRole + 1;
const int ROLE_ITEM = Qt::UserRole + 2;

QString layoutTag(DhRadialItem::SlotLayout layout)
{
    switch (layout) {
    case DhRadialItem::Grid:     return i18n("(grid)");
    case DhRadialItem::Drawer:   return i18n("(drawer)");
    case DhRadialItem::Dropdown: return i18n("(dropdown)");
    case DhRadialItem::Sprout:   return i18n("(sprout)");
    case DhRadialItem::Submenu:
    default:                     return i18n("(submenu)");
    }
}

/**
 * Searchable picker over the live action collection.
 *
 * The Python version carried a hand-maintained list of action ids, which went
 * stale every time Krita renamed something. This asks the running application
 * what it actually has, so the list is correct by construction.
 */
class DhActionPicker : public QDialog
{
public:
    explicit DhActionPicker(QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(i18n("Pick an Action"));
        resize(560, 460);

        QVBoxLayout *layout = new QVBoxLayout(this);

        m_search = new QLineEdit(this);
        m_search->setPlaceholderText(i18n("Filter by name or id..."));
        layout->addWidget(m_search);

        m_list = new QListWidget(this);
        layout->addWidget(m_list, 1);

        QLabel *hint = new QLabel(
            i18n("The id in brackets is what gets stored. If an action is missing here, "
                 "you can still type its visible menu text instead and Krita will match on that."),
            this);
        hint->setWordWrap(true);
        layout->addWidget(hint);

        QDialogButtonBox *buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        layout->addWidget(buttons);

        connect(buttons, SIGNAL(accepted()), this, SLOT(accept()));
        connect(buttons, SIGNAL(rejected()), this, SLOT(reject()));
        connect(m_list, SIGNAL(itemDoubleClicked(QListWidgetItem *)), this, SLOT(accept()));
        connect(m_search, &QLineEdit::textChanged, this, &DhActionPicker::refilter);

        collect();
        refilter(QString());
    }

    QString selectedId() const
    {
        QListWidgetItem *item = m_list->currentItem();
        return item ? item->data(Qt::UserRole).toString() : QString();
    }

private:
    struct Entry {
        QString text;
        QString id;
        QIcon icon;
    };

    void collect()
    {
        KisMainWindow *window = KisPart::instance()->currentMainwindow();
        if (!window || !window->actionCollection()) return;

        // The snapshot lets us enumerate everything even with no document open.
        QScopedPointer<KisActionsSnapshot> snapshot(new KisActionsSnapshot());
        for (QAction *action : window->actionCollection()->actions()) {
            snapshot->addAction(action->objectName(), action);
        }

        QMap<QString, KisKActionCollection *> collections = snapshot->actionCollections();
        for (KisKActionCollection *collection : collections) {
            for (QAction *action : collection->actions()) {
                if (action->objectName().isEmpty()) continue;
                Entry entry;
                entry.text = KLocalizedString::removeAcceleratorMarker(action->text());
                if (entry.text.isEmpty()) entry.text = action->objectName();
                entry.id = action->objectName();
                entry.icon = action->icon();
                m_entries.append(entry);
            }
        }

        std::sort(m_entries.begin(), m_entries.end(),
                  [](const Entry &a, const Entry &b) {
                      return a.text.localeAwareCompare(b.text) < 0;
                  });
    }

    void refilter(const QString &filter)
    {
        m_list->clear();
        for (const Entry &entry : m_entries) {
            if (!filter.isEmpty()
                && !entry.text.contains(filter, Qt::CaseInsensitive)
                && !entry.id.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            QListWidgetItem *item =
                new QListWidgetItem(entry.icon, QStringLiteral("%1  (%2)").arg(entry.text, entry.id));
            item->setData(Qt::UserRole, entry.id);
            m_list->addItem(item);
        }
        if (m_list->count()) m_list->setCurrentRow(0);
    }

    QLineEdit *m_search{nullptr};
    QListWidget *m_list{nullptr};
    QVector<Entry> m_entries;
};

/**
 * Grid picker over every icon that actually renders: the SVGs bundled in
 * dhtools.qrc, Krita's own built-in icon library, and the icons attached to
 * live actions. Each entry is resolved through DhRadialIcons::resolve() - the
 * same path the menu paints with - so nothing broken can be offered.
 */
class DhIconPicker : public QDialog
{
public:
    explicit DhIconPicker(QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(i18n("Pick an Icon"));
        resize(620, 520);

        QVBoxLayout *layout = new QVBoxLayout(this);

        QHBoxLayout *top = new QHBoxLayout();
        m_source = new QComboBox(this);
        m_source->addItems(QStringList() << i18n("Bundled") << i18n("Krita library")
                                         << i18n("Krita actions"));
        top->addWidget(m_source);
        m_search = new QLineEdit(this);
        m_search->setPlaceholderText(i18n("Filter..."));
        top->addWidget(m_search, 1);
        layout->addLayout(top);

        m_list = new QListWidget(this);
        m_list->setViewMode(QListView::IconMode);
        m_list->setIconSize(QSize(32, 32));
        m_list->setGridSize(QSize(104, 72));
        m_list->setResizeMode(QListView::Adjust);
        m_list->setMovement(QListView::Static);
        m_list->setWordWrap(true);
        m_list->setUniformItemSizes(true);
        layout->addWidget(m_list, 1);

        QHBoxLayout *bar = new QHBoxLayout();
        QPushButton *none = new QPushButton(i18n("No Icon"), this);
        QPushButton *autoBtn = new QPushButton(i18n("Auto"), this);
        none->setAutoDefault(false);
        autoBtn->setAutoDefault(false);
        bar->addWidget(none);
        bar->addWidget(autoBtn);
        bar->addStretch();
        QDialogButtonBox *buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        bar->addWidget(buttons);
        layout->addLayout(bar);

        connect(buttons, SIGNAL(accepted()), this, SLOT(accept()));
        connect(buttons, SIGNAL(rejected()), this, SLOT(reject()));
        connect(m_list, SIGNAL(itemDoubleClicked(QListWidgetItem *)), this, SLOT(accept()));
        connect(none, &QPushButton::clicked, this, [this]() { m_forced = QStringLiteral("none"); accept(); });
        connect(autoBtn, &QPushButton::clicked, this, [this]() { m_forced = QString(); m_useForced = true; accept(); });
        connect(m_search, &QLineEdit::textChanged, this, [this](const QString &) { refill(); });
        connect(m_source, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int) { refill(); });

        refill();
    }

    /// The spec to store. Empty + accepted via "Auto" means clear the field.
    QString chosen() const
    {
        if (m_useForced || !m_forced.isEmpty()) return m_forced;
        QListWidgetItem *item = m_list->currentItem();
        return item ? item->data(Qt::UserRole).toString() : QString();
    }

private:
    struct Entry { QString spec; QString label; QIcon icon; };

    const QVector<Entry> &entries(int source)
    {
        if (m_cache.contains(source)) return m_cache[source];
        QVector<Entry> out;
        auto add = [&out](const QString &spec, const QString &label) {
            const QIcon icon = DhRadialIcons::resolve(spec, QString(), false);
            if (!icon.isNull()) out.append({spec, label, icon});
        };
        if (source == 0) {
            for (const QString &n : DhRadialIcons::bundledNames()) add(QStringLiteral("dh:") + n, n);
        } else if (source == 1) {
            for (const QString &n : DhRadialIcons::kritaLibraryNames()) add(QStringLiteral("kicon:") + n, n);
        } else {
            KisMainWindow *window = KisPart::instance()->currentMainwindow();
            if (window && window->actionCollection()) {
                QSet<QString> seen;
                for (QAction *action : window->actionCollection()->actions()) {
                    const QString id = action->objectName();
                    if (id.isEmpty() || seen.contains(id)) continue;
                    seen.insert(id);
                    QString text = KLocalizedString::removeAcceleratorMarker(action->text());
                    if (text.isEmpty()) text = id;
                    add(QStringLiteral("krita:") + id, text);
                }
            }
            std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) {
                return a.label.localeAwareCompare(b.label) < 0;
            });
        }
        m_cache.insert(source, out);
        return m_cache[source];
    }

    void refill()
    {
        m_list->clear();
        const QString filter = m_search->text().trimmed();
        for (const Entry &e : entries(m_source->currentIndex())) {
            if (!filter.isEmpty() && !e.label.contains(filter, Qt::CaseInsensitive)
                && !e.spec.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            QListWidgetItem *item = new QListWidgetItem(e.icon, e.label);
            item->setData(Qt::UserRole, e.spec);
            item->setToolTip(e.spec);
            m_list->addItem(item);
        }
        if (m_list->count()) m_list->setCurrentRow(0);
    }

    QComboBox *m_source{nullptr};
    QLineEdit *m_search{nullptr};
    QListWidget *m_list{nullptr};
    QMap<int, QVector<Entry>> m_cache;
    QString m_forced;
    bool m_useForced{false};
};

} // namespace

KisDhRadialSettings::KisDhRadialSettings(QWidget *parent)
    : KisPreferenceSet(parent)
{
    buildUi();
    loadPreferences();

    // Every QPushButton inside a QDialog is autoDefault, so Enter in a field
    // used to "click" + Item. Strip that; keyPressEvent swallows the rest.
    for (QPushButton *button : findChildren<QPushButton *>()) {
        button->setAutoDefault(false);
        button->setDefault(false);
    }
}

void KisDhRadialSettings::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (QAbstractSpinBox *spin = qobject_cast<QAbstractSpinBox *>(focusWidget())) {
            spin->interpretText();
        }
        event->accept();
        return;
    }
    KisPreferenceSet::keyPressEvent(event);
}

KisDhRadialSettings::~KisDhRadialSettings()
{
}

QString KisDhRadialSettings::id()      { return QString("DhRadialSettings"); }
QString KisDhRadialSettings::name()    { return header(); }
QString KisDhRadialSettings::header()  { return QString(i18n("DH Radial Menu")); }
QIcon   KisDhRadialSettings::icon()    { return KisIconUtils::loadIcon("shape-choose"); }

void KisDhRadialSettings::buildUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);

    QLabel *intro = new QLabel(
        i18n("An item with children is a branch: its Layout decides how they are revealed - "
             "Submenu replaces the ring, Grid packs them inline, Drawer/Dropdown slide out on "
             "hover or tap, Sprout fans them onto an arc. An item with an action fires it. "
             "Bind the menu itself to a key or a gesture under Configure Shortcuts / Canvas "
             "Input Settings, search for \"Radial Menu\"."), this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    QTabWidget *tabs = new QTabWidget(this);
    root->addWidget(tabs, 1);
    tabs->addTab(buildItemsTab(tabs), i18n("Items"));
    tabs->addTab(buildDesignTab(tabs), i18n("Design"));
}

QWidget *KisDhRadialSettings::buildItemsTab(QWidget *parent)
{
    QWidget *page = new QWidget(parent);
    QVBoxLayout *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, page);
    pageLayout->addWidget(splitter, 1);

    // ---- left: the tree ----------------------------------------------------
    QWidget *left = new QWidget(splitter);
    QVBoxLayout *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeWidget(left);
    m_tree->setHeaderLabels(QStringList() << i18n("Label") << i18n("Action"));
    m_tree->setColumnWidth(COLUMN_LABEL, 190);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    leftLayout->addWidget(m_tree, 1);

    QHBoxLayout *treeButtons = new QHBoxLayout();
    m_addSibling = new QPushButton(i18n("+ Item"), left);
    m_addChild   = new QPushButton(i18n("+ Child"), left);
    m_remove     = new QPushButton(i18n("Remove"), left);
    m_up         = new QPushButton(QStringLiteral("▲"), left);
    m_down       = new QPushButton(QStringLiteral("▼"), left);
    m_up->setFixedWidth(40);
    m_down->setFixedWidth(40);
    treeButtons->addWidget(m_addSibling);
    treeButtons->addWidget(m_addChild);
    treeButtons->addWidget(m_remove);
    treeButtons->addStretch();
    treeButtons->addWidget(m_up);
    treeButtons->addWidget(m_down);
    leftLayout->addLayout(treeButtons);

    splitter->addWidget(left);

    // ---- right: properties of the selected item ---------------------------
    QScrollArea *scroll = new QScrollArea(splitter);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    QWidget *right = new QWidget(scroll);
    scroll->setWidget(right);
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    QGroupBox *props = new QGroupBox(i18n("Selected Item"), right);
    QFormLayout *form = new QFormLayout(props);

    m_label = new QLineEdit(props);
    form->addRow(i18n("Label:"), m_label);

    QHBoxLayout *actionRow = new QHBoxLayout();
    m_action = new QLineEdit(props);
    m_action->setPlaceholderText(i18n("action id, or a menu item's visible text"));
    m_pick = new QPushButton(i18n("Pick..."), props);
    actionRow->addWidget(m_action, 1);
    actionRow->addWidget(m_pick);
    form->addRow(i18n("Action:"), actionRow);

    QHBoxLayout *colourRow = new QHBoxLayout();
    m_swatch = new QPushButton(props);
    m_swatch->setFixedSize(28, 24);
    m_inherit = new QPushButton(i18n("Default"), props);
    m_inherit->setToolTip(i18n("Clear the colour; the wedge takes one from the palette."));
    colourRow->addWidget(m_swatch);
    colourRow->addWidget(m_inherit);
    colourRow->addStretch();
    form->addRow(i18n("Colour:"), colourRow);

    QHBoxLayout *iconRow = new QHBoxLayout();
    m_icon = new QLineEdit(props);
    m_icon->setPlaceholderText(i18n("dh:name | kicon:name | krita:action_id | /path | blank = auto"));
    m_pickIcon = new QPushButton(i18n("Pick..."), props);
    iconRow->addWidget(m_icon, 1);
    iconRow->addWidget(m_pickIcon);
    form->addRow(i18n("Icon:"), iconRow);

    m_tooltip = new QLineEdit(props);
    form->addRow(i18n("Tooltip:"), m_tooltip);

    m_shape = new QComboBox(props);
    m_shape->addItems(QStringList() << i18n("Inherit") << i18n("Rectangle") << i18n("Circle"));
    form->addRow(i18n("Shape:"), m_shape);

    rightLayout->addWidget(props);

    QGroupBox *panelBox = new QGroupBox(i18n("Branch: How Children Appear"), right);
    QFormLayout *panelForm = new QFormLayout(panelBox);

    m_layout = new QComboBox(panelBox);
    m_layout->addItems(QStringList() << i18n("Submenu (replaces the ring)")
                                     << i18n("Grid (packed inline)")
                                     << i18n("Drawer (slides out)")
                                     << i18n("Dropdown (drops below, edge-aligned)")
                                     << i18n("Sprout (fans onto an arc)"));
    panelForm->addRow(i18n("Layout:"), m_layout);

    m_columns = new QSpinBox(panelBox);
    m_columns->setRange(0, 8);
    m_columns->setSpecialValueText(i18n("design default"));
    panelForm->addRow(i18n("Columns:"), m_columns);

    m_colSpan = new QDoubleSpinBox(panelBox);
    m_colSpan->setRange(0.5, 4.0);
    m_colSpan->setSingleStep(0.5);
    m_colSpan->setDecimals(1);
    panelForm->addRow(i18n("This item's column span:"), m_colSpan);

    QHBoxLayout *cellRow = new QHBoxLayout();
    m_cellWidth = new QSpinBox(panelBox);
    m_cellWidth->setRange(0, 400);
    m_cellWidth->setSpecialValueText(i18n("auto"));
    m_cellHeight = new QSpinBox(panelBox);
    m_cellHeight->setRange(0, 200);
    m_cellHeight->setSpecialValueText(i18n("auto"));
    cellRow->addWidget(m_cellWidth);
    cellRow->addWidget(new QLabel(QStringLiteral("x"), panelBox));
    cellRow->addWidget(m_cellHeight);
    panelForm->addRow(i18n("Cell size (px):"), cellRow);

    m_drawerDirection = new QComboBox(panelBox);
    m_drawerDirection->addItems(QStringList() << QStringLiteral("AUTO") << QStringLiteral("RIGHT")
                                              << QStringLiteral("LEFT") << QStringLiteral("UP")
                                              << QStringLiteral("DOWN") << QStringLiteral("RADIAL"));
    panelForm->addRow(i18n("Drawer direction:"), m_drawerDirection);

    m_drawerFlow = new QComboBox(panelBox);
    m_drawerFlow->addItems(QStringList() << QStringLiteral("LINE") << QStringLiteral("GRID"));
    panelForm->addRow(i18n("Drawer flow:"), m_drawerFlow);

    m_drawerAlign = new QComboBox(panelBox);
    m_drawerAlign->addItems(QStringList() << QStringLiteral("AUTO") << QStringLiteral("START")
                                          << QStringLiteral("CENTER") << QStringLiteral("END"));
    panelForm->addRow(i18n("Drawer align:"), m_drawerAlign);

    rightLayout->addWidget(panelBox);
    rightLayout->addStretch();
    splitter->addWidget(scroll);

    splitter->setSizes(QList<int>() << 430 << 370);

    connect(m_tree, SIGNAL(itemSelectionChanged()), this, SLOT(slotSelectionChanged()));
    connect(m_label, SIGNAL(textEdited(QString)), this, SLOT(slotLabelEdited(QString)));
    connect(m_action, SIGNAL(textEdited(QString)), this, SLOT(slotActionEdited(QString)));
    connect(m_pick, SIGNAL(clicked()), this, SLOT(slotPickAction()));
    connect(m_pickIcon, SIGNAL(clicked()), this, SLOT(slotPickIcon()));
    connect(m_swatch, SIGNAL(clicked()), this, SLOT(slotPickColour()));
    connect(m_inherit, SIGNAL(clicked()), this, SLOT(slotClearColour()));
    connect(m_addSibling, SIGNAL(clicked()), this, SLOT(slotAddSibling()));
    connect(m_addChild, SIGNAL(clicked()), this, SLOT(slotAddChild()));
    connect(m_remove, SIGNAL(clicked()), this, SLOT(slotRemove()));
    connect(m_up, SIGNAL(clicked()), this, SLOT(slotMoveUp()));
    connect(m_down, SIGNAL(clicked()), this, SLOT(slotMoveDown()));
    connect(m_icon, SIGNAL(textEdited(QString)), this, SLOT(slotIconEdited(QString)));
    connect(m_tooltip, SIGNAL(textEdited(QString)), this, SLOT(slotTooltipEdited(QString)));
    connect(m_shape, SIGNAL(currentIndexChanged(int)), this, SLOT(slotShapeChanged(int)));
    connect(m_layout, SIGNAL(currentIndexChanged(int)), this, SLOT(slotLayoutChanged(int)));
    connect(m_columns, SIGNAL(valueChanged(int)), this, SLOT(slotColumnsChanged(int)));
    connect(m_colSpan, SIGNAL(valueChanged(double)), this, SLOT(slotColSpanChanged(double)));
    connect(m_cellWidth, SIGNAL(valueChanged(int)), this, SLOT(slotCellWidthChanged(int)));
    connect(m_cellHeight, SIGNAL(valueChanged(int)), this, SLOT(slotCellHeightChanged(int)));
    connect(m_drawerDirection, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDrawerDirectionChanged(int)));
    connect(m_drawerFlow, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDrawerFlowChanged(int)));
    connect(m_drawerAlign, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDrawerAlignChanged(int)));

    return page;
}

// ---------------------------------------------------------------------------
// Design tab
// ---------------------------------------------------------------------------

QSpinBox *KisDhRadialSettings::addIntRow(const QString &key, QFormLayout *form, const QString &label,
                                         int min, int max, const QString &suffix)
{
    QSpinBox *box = new QSpinBox();
    box->setRange(min, max);
    if (!suffix.isEmpty()) box->setSuffix(suffix);
    form->addRow(label, box);
    m_designInts.insert(key, box);
    return box;
}

QDoubleSpinBox *KisDhRadialSettings::addRealRow(const QString &key, QFormLayout *form, const QString &label,
                                                double min, double max, double step)
{
    QDoubleSpinBox *box = new QDoubleSpinBox();
    box->setRange(min, max);
    box->setSingleStep(step);
    box->setDecimals(2);
    form->addRow(label, box);
    m_designReals.insert(key, box);
    return box;
}

QCheckBox *KisDhRadialSettings::addBoolRow(const QString &key, QFormLayout *form, const QString &label)
{
    QCheckBox *box = new QCheckBox();
    form->addRow(label, box);
    m_designBools.insert(key, box);
    return box;
}

QComboBox *KisDhRadialSettings::addComboRow(const QString &key, QFormLayout *form, const QString &label,
                                            const QStringList &options)
{
    QComboBox *box = new QComboBox();
    box->addItems(options);
    form->addRow(label, box);
    m_designCombos.insert(key, box);
    return box;
}

QPushButton *KisDhRadialSettings::addColourRow(const QString &key, QFormLayout *form, const QString &label)
{
    QPushButton *swatch = new QPushButton();
    swatch->setFixedSize(28, 24);
    form->addRow(label, swatch);
    m_designColours.insert(key, swatch);
    m_designColourValues.insert(key, QColor(Qt::black));
    connect(swatch, SIGNAL(clicked()), this, SLOT(slotDesignColourClicked()));
    return swatch;
}

QWidget *KisDhRadialSettings::buildDesignTab(QWidget *parent)
{
    QScrollArea *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    QWidget *page = new QWidget(scroll);
    scroll->setWidget(page);
    QVBoxLayout *layout = new QVBoxLayout(page);

    // ----- presets -----------------------------------------------------
    QGroupBox *presetBox = new QGroupBox(i18n("Preset"), page);
    QHBoxLayout *presetRow = new QHBoxLayout(presetBox);
    m_presetCombo = new QComboBox(presetBox);
    m_presetCombo->addItems(DhRadialPresets::names());
    QPushButton *applyPreset = new QPushButton(i18n("Apply"), presetBox);
    presetRow->addWidget(m_presetCombo, 1);
    presetRow->addWidget(applyPreset);
    layout->addWidget(presetBox);
    connect(applyPreset, SIGNAL(clicked()), this, SLOT(slotApplyPreset()));

    // ----- style & geometry ---------------------------------------------
    QGroupBox *styleBox = new QGroupBox(i18n("Style && Geometry"), page);
    QFormLayout *styleForm = new QFormLayout(styleBox);
    addComboRow(QStringLiteral("slice_style"), styleForm, i18n("Slice style:"),
               QStringList() << QStringLiteral("BUTTONS") << QStringLiteral("RING"));
    addRealRow(QStringLiteral("ui_scale"), styleForm, i18n("UI scale:"), 0.25, 4.0, 0.05);
    addIntRow(QStringLiteral("pie_radius"), styleForm, i18n("Pie radius:"), 40, 500, i18n(" px"));
    addIntRow(QStringLiteral("button_width"), styleForm, i18n("Button width:"), 30, 400, i18n(" px"));
    addBoolRow(QStringLiteral("auto_width"), styleForm, i18n("Auto width:"));
    addIntRow(QStringLiteral("button_height"), styleForm, i18n("Button height:"), 16, 200, i18n(" px"));
    addComboRow(QStringLiteral("button_shape"), styleForm, i18n("Button shape:"),
               QStringList() << QStringLiteral("RECTANGLE") << QStringLiteral("CIRCLE"));
    addIntRow(QStringLiteral("circle_diameter"), styleForm, i18n("Circle diameter:"), 20, 300, i18n(" px"));
    addBoolRow(QStringLiteral("circle_caption"), styleForm, i18n("Circle caption:"));
    addRealRow(QStringLiteral("corner_radius"), styleForm, i18n("Corner radius:"), 0.0, 60.0, 1.0);
    addRealRow(QStringLiteral("border_width"), styleForm, i18n("Border width:"), 0.0, 10.0, 0.2);
    addIntRow(QStringLiteral("item_spacing"), styleForm, i18n("Item spacing:"), 0, 60, i18n(" px"));
    addComboRow(QStringLiteral("ring_mode"), styleForm, i18n("Ring mode:"),
               QStringList() << QStringLiteral("UNIFORM") << QStringLiteral("COMPACT"));
    addIntRow(QStringLiteral("slot_padding"), styleForm, i18n("Slot padding:"), 0, 80, i18n(" px"));
    layout->addWidget(styleBox);

    // ----- grid -----------------------------------------------------------
    QGroupBox *gridBox = new QGroupBox(i18n("Grid"), page);
    QFormLayout *gridForm = new QFormLayout(gridBox);
    addIntRow(QStringLiteral("grid_columns"), gridForm, i18n("Columns:"), 1, 8);
    addIntRow(QStringLiteral("grid_cell_width"), gridForm, i18n("Cell width:"), 20, 300, i18n(" px"));
    addIntRow(QStringLiteral("grid_cell_height"), gridForm, i18n("Cell height:"), 12, 150, i18n(" px"));
    addBoolRow(QStringLiteral("grid_header"), gridForm, i18n("Show header:"));
    layout->addWidget(gridBox);

    // ----- ring (RING style only) ------------------------------------------
    QGroupBox *ringBox = new QGroupBox(i18n("Ring Style"), page);
    QFormLayout *ringForm = new QFormLayout(ringBox);
    addIntRow(QStringLiteral("ring_inner_radius"), ringForm, i18n("Inner radius:"), 10, 400, i18n(" px"));
    addIntRow(QStringLiteral("ring_outer_radius"), ringForm, i18n("Outer radius:"), 30, 600, i18n(" px"));
    addRealRow(QStringLiteral("ring_gap_deg"), ringForm, i18n("Gap:"), 0.0, 20.0, 0.5);
    layout->addWidget(ringBox);

    // ----- panels: drawer, dropdown, sprout --------------------------------
    QGroupBox *panelBox = new QGroupBox(i18n("Drawers && Dropdowns"), page);
    QFormLayout *panelForm = new QFormLayout(panelBox);
    addComboRow(QStringLiteral("panel_trigger"), panelForm, i18n("Trigger:"),
               QStringList() << QStringLiteral("HOVER") << QStringLiteral("CLICK"));
    addIntRow(QStringLiteral("panel_delay_ms"), panelForm, i18n("Hover delay:"), 0, 1500, i18n(" ms"));
    addIntRow(QStringLiteral("panel_gap"), panelForm, i18n("Gap:"), 0, 60, i18n(" px"));
    addBoolRow(QStringLiteral("panel_arrow"), panelForm, i18n("Show direction glyph:"));
    addIntRow(QStringLiteral("drawer_cell_width"), panelForm, i18n("Drawer cell width:"), 0, 400, i18n(" px (0=auto)"));
    addIntRow(QStringLiteral("drawer_cell_height"), panelForm, i18n("Drawer cell height:"), 10, 200, i18n(" px"));
    addIntRow(QStringLiteral("dropdown_cell_width"), panelForm, i18n("Dropdown cell width:"), 0, 400, i18n(" px (0=auto)"));
    addIntRow(QStringLiteral("dropdown_cell_height"), panelForm, i18n("Dropdown cell height:"), 10, 200, i18n(" px"));
    layout->addWidget(panelBox);

    QGroupBox *sproutBox = new QGroupBox(i18n("Sprout"), page);
    QFormLayout *sproutForm = new QFormLayout(sproutBox);
    addComboRow(QStringLiteral("sprout_shape"), sproutForm, i18n("Satellite shape:"),
               QStringList() << QStringLiteral("CIRCLE") << QStringLiteral("RECTANGLE"));
    addIntRow(QStringLiteral("sprout_radius"), sproutForm, i18n("Radius:"), 20, 300, i18n(" px"));
    addIntRow(QStringLiteral("sprout_size"), sproutForm, i18n("Satellite size:"), 16, 150, i18n(" px"));
    addRealRow(QStringLiteral("sprout_spread_deg"), sproutForm, i18n("Spread:"), 20.0, 360.0, 5.0);
    addIntRow(QStringLiteral("sprout_max_fan"), sproutForm, i18n("Max fan (else sub-ring):"), 1, 12);
    addIntRow(QStringLiteral("sprout_delay_ms"), sproutForm, i18n("Hover delay:"), 0, 1500, i18n(" ms"));
    addBoolRow(QStringLiteral("sprout_connectors"), sproutForm, i18n("Show connectors:"));
    layout->addWidget(sproutBox);

    // ----- hub & pointer -----------------------------------------------
    QGroupBox *hubBox = new QGroupBox(i18n("Centre Hub && Pointer"), page);
    QFormLayout *hubForm = new QFormLayout(hubBox);
    addBoolRow(QStringLiteral("show_center"), hubForm, i18n("Show hub:"));
    addIntRow(QStringLiteral("center_radius"), hubForm, i18n("Hub radius:"), 10, 200, i18n(" px"));
    addBoolRow(QStringLiteral("center_label"), hubForm, i18n("Hub label:"));
    addBoolRow(QStringLiteral("show_trail"), hubForm, i18n("Show pointer trail:"));
    addRealRow(QStringLiteral("trail_width"), hubForm, i18n("Trail width:"), 0.5, 8.0, 0.2);
    addIntRow(QStringLiteral("dead_zone"), hubForm, i18n("Dead zone:"), 0, 100, i18n(" px"));
    addBoolRow(QStringLiteral("flick_select"), hubForm, i18n("Flick-select past dead zone:"));
    layout->addWidget(hubBox);

    // ----- text & icons -----------------------------------------------
    QGroupBox *textBox = new QGroupBox(i18n("Text && Icons"), page);
    QFormLayout *textForm = new QFormLayout(textBox);
    m_fontFamily = new QLineEdit();
    textForm->addRow(i18n("Font family:"), m_fontFamily);
    addIntRow(QStringLiteral("font_size"), textForm, i18n("Font size:"), 6, 40);
    addIntRow(QStringLiteral("center_font_size"), textForm, i18n("Hub font size:"), 6, 40);
    addBoolRow(QStringLiteral("text_shadow"), textForm, i18n("Text shadow:"));
    addIntRow(QStringLiteral("icon_size"), textForm, i18n("Icon size:"), 8, 80, i18n(" px"));
    addComboRow(QStringLiteral("icon_side"), textForm, i18n("Icon side:"),
               QStringList() << QStringLiteral("LEFT") << QStringLiteral("TOP") << QStringLiteral("ONLY"));
    addRealRow(QStringLiteral("icon_opacity"), textForm, i18n("Icon opacity:"), 0.0, 1.0, 0.05);
    addBoolRow(QStringLiteral("auto_icons"), textForm, i18n("Auto icons from actions:"));
    layout->addWidget(textBox);

    // ----- colours -----------------------------------------------------
    QGroupBox *colourBox = new QGroupBox(i18n("Colours"), page);
    QFormLayout *colourForm = new QFormLayout(colourBox);
    addColourRow(QStringLiteral("bg_color"), colourForm, i18n("Background wash:"));
    addColourRow(QStringLiteral("button_color"), colourForm, i18n("Button:"));
    addColourRow(QStringLiteral("button_hover_color"), colourForm, i18n("Button (hover):"));
    addColourRow(QStringLiteral("button_active_color"), colourForm, i18n("Button (active):"));
    addColourRow(QStringLiteral("border_color"), colourForm, i18n("Border:"));
    addColourRow(QStringLiteral("border_hover_color"), colourForm, i18n("Border (hover):"));
    addColourRow(QStringLiteral("text_color"), colourForm, i18n("Text:"));
    addColourRow(QStringLiteral("text_hover_color"), colourForm, i18n("Text (hover):"));
    addColourRow(QStringLiteral("center_color"), colourForm, i18n("Hub:"));
    addColourRow(QStringLiteral("center_border_color"), colourForm, i18n("Hub border:"));
    addColourRow(QStringLiteral("center_text_color"), colourForm, i18n("Hub text:"));
    addColourRow(QStringLiteral("trail_color"), colourForm, i18n("Pointer trail:"));
    addColourRow(QStringLiteral("shadow_color"), colourForm, i18n("Shadow:"));
    addRealRow(QStringLiteral("tint_strength"), colourForm, i18n("Item colour tint strength:"), 0.0, 1.0, 0.05);
    layout->addWidget(colourBox);

    // ----- effects -----------------------------------------------------
    QGroupBox *fxBox = new QGroupBox(i18n("Glow && Animation"), page);
    QFormLayout *fxForm = new QFormLayout(fxBox);
    addBoolRow(QStringLiteral("glow_enabled"), fxForm, i18n("Hover glow:"));
    addColourRow(QStringLiteral("glow_color"), fxForm, i18n("Glow colour:"));
    addRealRow(QStringLiteral("glow_size"), fxForm, i18n("Glow size:"), 0.0, 60.0, 1.0);
    addBoolRow(QStringLiteral("open_animation"), fxForm, i18n("Open animation:"));
    addIntRow(QStringLiteral("anim_ms"), fxForm, i18n("Animation length:"), 0, 1000, i18n(" ms"));
    layout->addWidget(fxBox);

    layout->addStretch();

    return scroll;
}

// ---------------------------------------------------------------------------
// design <-> widgets
// ---------------------------------------------------------------------------

DhRadialDesign KisDhRadialSettings::designFromWidgets() const
{
    DhRadialDesign d;

    d.sliceStyle = m_designCombos.value(QStringLiteral("slice_style"))->currentText() == QLatin1String("RING")
        ? DhRadialDesign::Ring : DhRadialDesign::Buttons;
    d.uiScale = m_designReals.value(QStringLiteral("ui_scale"))->value();

    d.pieRadius = m_designInts.value(QStringLiteral("pie_radius"))->value();
    d.buttonWidth = m_designInts.value(QStringLiteral("button_width"))->value();
    d.autoWidth = m_designBools.value(QStringLiteral("auto_width"))->isChecked();
    d.buttonHeight = m_designInts.value(QStringLiteral("button_height"))->value();
    d.buttonShape = m_designCombos.value(QStringLiteral("button_shape"))->currentText() == QLatin1String("CIRCLE")
        ? DhRadialDesign::Circle : DhRadialDesign::Rectangle;
    d.circleDiameter = m_designInts.value(QStringLiteral("circle_diameter"))->value();
    d.circleCaption = m_designBools.value(QStringLiteral("circle_caption"))->isChecked();
    d.cornerRadius = m_designReals.value(QStringLiteral("corner_radius"))->value();
    d.borderWidth = m_designReals.value(QStringLiteral("border_width"))->value();
    d.itemSpacing = m_designInts.value(QStringLiteral("item_spacing"))->value();
    d.ringMode = m_designCombos.value(QStringLiteral("ring_mode"))->currentText() == QLatin1String("COMPACT")
        ? DhRadialDesign::Compact : DhRadialDesign::Uniform;
    d.gridColumns = m_designInts.value(QStringLiteral("grid_columns"))->value();
    d.gridCellWidth = m_designInts.value(QStringLiteral("grid_cell_width"))->value();
    d.gridCellHeight = m_designInts.value(QStringLiteral("grid_cell_height"))->value();
    d.gridHeader = m_designBools.value(QStringLiteral("grid_header"))->isChecked();
    d.slotPadding = m_designInts.value(QStringLiteral("slot_padding"))->value();

    d.ringInnerRadius = m_designInts.value(QStringLiteral("ring_inner_radius"))->value();
    d.ringOuterRadius = m_designInts.value(QStringLiteral("ring_outer_radius"))->value();
    d.ringGapDeg = m_designReals.value(QStringLiteral("ring_gap_deg"))->value();

    d.panelTrigger = m_designCombos.value(QStringLiteral("panel_trigger"))->currentText() == QLatin1String("CLICK")
        ? DhRadialDesign::PanelClick : DhRadialDesign::PanelHover;
    d.panelDelayMs = m_designInts.value(QStringLiteral("panel_delay_ms"))->value();
    d.panelGap = m_designInts.value(QStringLiteral("panel_gap"))->value();
    d.panelArrow = m_designBools.value(QStringLiteral("panel_arrow"))->isChecked();
    d.drawerCellWidth = m_designInts.value(QStringLiteral("drawer_cell_width"))->value();
    d.drawerCellHeight = m_designInts.value(QStringLiteral("drawer_cell_height"))->value();
    d.dropdownCellWidth = m_designInts.value(QStringLiteral("dropdown_cell_width"))->value();
    d.dropdownCellHeight = m_designInts.value(QStringLiteral("dropdown_cell_height"))->value();

    d.sproutShape = m_designCombos.value(QStringLiteral("sprout_shape"))->currentText();
    d.sproutRadius = m_designInts.value(QStringLiteral("sprout_radius"))->value();
    d.sproutSize = m_designInts.value(QStringLiteral("sprout_size"))->value();
    d.sproutSpreadDeg = m_designReals.value(QStringLiteral("sprout_spread_deg"))->value();
    d.sproutMaxFan = m_designInts.value(QStringLiteral("sprout_max_fan"))->value();
    d.sproutDelayMs = m_designInts.value(QStringLiteral("sprout_delay_ms"))->value();
    d.sproutConnectors = m_designBools.value(QStringLiteral("sprout_connectors"))->isChecked();

    d.showCenter = m_designBools.value(QStringLiteral("show_center"))->isChecked();
    d.centerRadius = m_designInts.value(QStringLiteral("center_radius"))->value();
    d.centerLabel = m_designBools.value(QStringLiteral("center_label"))->isChecked();

    d.showTrail = m_designBools.value(QStringLiteral("show_trail"))->isChecked();
    d.trailWidth = m_designReals.value(QStringLiteral("trail_width"))->value();
    d.deadZone = m_designInts.value(QStringLiteral("dead_zone"))->value();
    d.flickSelect = m_designBools.value(QStringLiteral("flick_select"))->isChecked();

    d.fontFamily = m_fontFamily->text();
    d.fontSize = m_designInts.value(QStringLiteral("font_size"))->value();
    d.centerFontSize = m_designInts.value(QStringLiteral("center_font_size"))->value();
    d.textShadow = m_designBools.value(QStringLiteral("text_shadow"))->isChecked();

    d.iconSize = m_designInts.value(QStringLiteral("icon_size"))->value();
    const QString side = m_designCombos.value(QStringLiteral("icon_side"))->currentText();
    d.iconSide = side == QLatin1String("TOP") ? DhRadialDesign::IconTop
               : side == QLatin1String("ONLY") ? DhRadialDesign::IconOnly : DhRadialDesign::IconLeft;
    d.iconOpacity = m_designReals.value(QStringLiteral("icon_opacity"))->value();
    d.autoIcons = m_designBools.value(QStringLiteral("auto_icons"))->isChecked();

    d.bgColor = m_designColourValues.value(QStringLiteral("bg_color"));
    d.buttonColor = m_designColourValues.value(QStringLiteral("button_color"));
    d.buttonHoverColor = m_designColourValues.value(QStringLiteral("button_hover_color"));
    d.buttonActiveColor = m_designColourValues.value(QStringLiteral("button_active_color"));
    d.borderColor = m_designColourValues.value(QStringLiteral("border_color"));
    d.borderHoverColor = m_designColourValues.value(QStringLiteral("border_hover_color"));
    d.textColor = m_designColourValues.value(QStringLiteral("text_color"));
    d.textHoverColor = m_designColourValues.value(QStringLiteral("text_hover_color"));
    d.centerColor = m_designColourValues.value(QStringLiteral("center_color"));
    d.centerBorderColor = m_designColourValues.value(QStringLiteral("center_border_color"));
    d.centerTextColor = m_designColourValues.value(QStringLiteral("center_text_color"));
    d.trailColor = m_designColourValues.value(QStringLiteral("trail_color"));
    d.shadowColor = m_designColourValues.value(QStringLiteral("shadow_color"));
    d.tintStrength = m_designReals.value(QStringLiteral("tint_strength"))->value();

    d.glowEnabled = m_designBools.value(QStringLiteral("glow_enabled"))->isChecked();
    d.glowColor = m_designColourValues.value(QStringLiteral("glow_color"));
    d.glowSize = m_designReals.value(QStringLiteral("glow_size"))->value();
    d.openAnimation = m_designBools.value(QStringLiteral("open_animation"))->isChecked();
    d.animMs = m_designInts.value(QStringLiteral("anim_ms"))->value();

    return d;
}

void KisDhRadialSettings::designToWidgets(const DhRadialDesign &d)
{
    m_updatingFields = true;

    {
        QComboBox *box = m_designCombos.value(QStringLiteral("slice_style"));
        int idx = box->findText(d.sliceStyle == DhRadialDesign::Ring ? QStringLiteral("RING") : QStringLiteral("BUTTONS"));
        box->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    m_designReals.value(QStringLiteral("ui_scale"))->setValue(d.uiScale);

    m_designInts.value(QStringLiteral("pie_radius"))->setValue(d.pieRadius);
    m_designInts.value(QStringLiteral("button_width"))->setValue(d.buttonWidth);
    m_designBools.value(QStringLiteral("auto_width"))->setChecked(d.autoWidth);
    m_designInts.value(QStringLiteral("button_height"))->setValue(d.buttonHeight);
    {
        QComboBox *box = m_designCombos.value(QStringLiteral("button_shape"));
        int idx = box->findText(d.buttonShape == DhRadialDesign::Circle ? QStringLiteral("CIRCLE") : QStringLiteral("RECTANGLE"));
        box->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    m_designInts.value(QStringLiteral("circle_diameter"))->setValue(d.circleDiameter);
    m_designBools.value(QStringLiteral("circle_caption"))->setChecked(d.circleCaption);
    m_designReals.value(QStringLiteral("corner_radius"))->setValue(d.cornerRadius);
    m_designReals.value(QStringLiteral("border_width"))->setValue(d.borderWidth);
    m_designInts.value(QStringLiteral("item_spacing"))->setValue(d.itemSpacing);
    {
        QComboBox *box = m_designCombos.value(QStringLiteral("ring_mode"));
        int idx = box->findText(d.ringMode == DhRadialDesign::Compact ? QStringLiteral("COMPACT") : QStringLiteral("UNIFORM"));
        box->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    m_designInts.value(QStringLiteral("grid_columns"))->setValue(d.gridColumns);
    m_designInts.value(QStringLiteral("grid_cell_width"))->setValue(d.gridCellWidth);
    m_designInts.value(QStringLiteral("grid_cell_height"))->setValue(d.gridCellHeight);
    m_designBools.value(QStringLiteral("grid_header"))->setChecked(d.gridHeader);
    m_designInts.value(QStringLiteral("slot_padding"))->setValue(d.slotPadding);

    m_designInts.value(QStringLiteral("ring_inner_radius"))->setValue(d.ringInnerRadius);
    m_designInts.value(QStringLiteral("ring_outer_radius"))->setValue(d.ringOuterRadius);
    m_designReals.value(QStringLiteral("ring_gap_deg"))->setValue(d.ringGapDeg);

    {
        QComboBox *box = m_designCombos.value(QStringLiteral("panel_trigger"));
        int idx = box->findText(d.panelTrigger == DhRadialDesign::PanelClick ? QStringLiteral("CLICK") : QStringLiteral("HOVER"));
        box->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    m_designInts.value(QStringLiteral("panel_delay_ms"))->setValue(d.panelDelayMs);
    m_designInts.value(QStringLiteral("panel_gap"))->setValue(d.panelGap);
    m_designBools.value(QStringLiteral("panel_arrow"))->setChecked(d.panelArrow);
    m_designInts.value(QStringLiteral("drawer_cell_width"))->setValue(d.drawerCellWidth);
    m_designInts.value(QStringLiteral("drawer_cell_height"))->setValue(d.drawerCellHeight);
    m_designInts.value(QStringLiteral("dropdown_cell_width"))->setValue(d.dropdownCellWidth);
    m_designInts.value(QStringLiteral("dropdown_cell_height"))->setValue(d.dropdownCellHeight);

    QComboBox *sproutShapeBox = m_designCombos.value(QStringLiteral("sprout_shape"));
    int shapeIdx = sproutShapeBox->findText(d.sproutShape);
    sproutShapeBox->setCurrentIndex(shapeIdx >= 0 ? shapeIdx : 0);
    m_designInts.value(QStringLiteral("sprout_radius"))->setValue(d.sproutRadius);
    m_designInts.value(QStringLiteral("sprout_size"))->setValue(d.sproutSize);
    m_designReals.value(QStringLiteral("sprout_spread_deg"))->setValue(d.sproutSpreadDeg);
    m_designInts.value(QStringLiteral("sprout_max_fan"))->setValue(d.sproutMaxFan);
    m_designInts.value(QStringLiteral("sprout_delay_ms"))->setValue(d.sproutDelayMs);
    m_designBools.value(QStringLiteral("sprout_connectors"))->setChecked(d.sproutConnectors);

    m_designBools.value(QStringLiteral("show_center"))->setChecked(d.showCenter);
    m_designInts.value(QStringLiteral("center_radius"))->setValue(d.centerRadius);
    m_designBools.value(QStringLiteral("center_label"))->setChecked(d.centerLabel);

    m_designBools.value(QStringLiteral("show_trail"))->setChecked(d.showTrail);
    m_designReals.value(QStringLiteral("trail_width"))->setValue(d.trailWidth);
    m_designInts.value(QStringLiteral("dead_zone"))->setValue(d.deadZone);
    m_designBools.value(QStringLiteral("flick_select"))->setChecked(d.flickSelect);

    m_fontFamily->setText(d.fontFamily);
    m_designInts.value(QStringLiteral("font_size"))->setValue(d.fontSize);
    m_designInts.value(QStringLiteral("center_font_size"))->setValue(d.centerFontSize);
    m_designBools.value(QStringLiteral("text_shadow"))->setChecked(d.textShadow);

    m_designInts.value(QStringLiteral("icon_size"))->setValue(d.iconSize);
    {
        QComboBox *box = m_designCombos.value(QStringLiteral("icon_side"));
        int idx = box->findText(d.iconSide == DhRadialDesign::IconTop ? QStringLiteral("TOP") : d.iconSide == DhRadialDesign::IconOnly ? QStringLiteral("ONLY") : QStringLiteral("LEFT"));
        box->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    m_designReals.value(QStringLiteral("icon_opacity"))->setValue(d.iconOpacity);
    m_designBools.value(QStringLiteral("auto_icons"))->setChecked(d.autoIcons);

    auto setColour = [this](const QString &key, const QColor &c) {
        m_designColourValues.insert(key, c);
        QPushButton *swatch = m_designColours.value(key);
        if (swatch) {
            swatch->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid palette(mid);")
                                  .arg(c.name(QColor::HexArgb)));
        }
    };
    setColour(QStringLiteral("bg_color"), d.bgColor);
    setColour(QStringLiteral("button_color"), d.buttonColor);
    setColour(QStringLiteral("button_hover_color"), d.buttonHoverColor);
    setColour(QStringLiteral("button_active_color"), d.buttonActiveColor);
    setColour(QStringLiteral("border_color"), d.borderColor);
    setColour(QStringLiteral("border_hover_color"), d.borderHoverColor);
    setColour(QStringLiteral("text_color"), d.textColor);
    setColour(QStringLiteral("text_hover_color"), d.textHoverColor);
    setColour(QStringLiteral("center_color"), d.centerColor);
    setColour(QStringLiteral("center_border_color"), d.centerBorderColor);
    setColour(QStringLiteral("center_text_color"), d.centerTextColor);
    setColour(QStringLiteral("trail_color"), d.trailColor);
    setColour(QStringLiteral("shadow_color"), d.shadowColor);
    m_designReals.value(QStringLiteral("tint_strength"))->setValue(d.tintStrength);

    m_designBools.value(QStringLiteral("glow_enabled"))->setChecked(d.glowEnabled);
    setColour(QStringLiteral("glow_color"), d.glowColor);
    m_designReals.value(QStringLiteral("glow_size"))->setValue(d.glowSize);
    m_designBools.value(QStringLiteral("open_animation"))->setChecked(d.openAnimation);
    m_designInts.value(QStringLiteral("anim_ms"))->setValue(d.animMs);

    m_updatingFields = false;
}

void KisDhRadialSettings::slotApplyPreset()
{
    if (!m_presetCombo) return;
    designToWidgets(DhRadialPresets::apply(m_presetCombo->currentText()));
}

void KisDhRadialSettings::slotDesignColourClicked()
{
    QPushButton *button = qobject_cast<QPushButton *>(sender());
    if (!button) return;

    QString key;
    for (auto it = m_designColours.constBegin(); it != m_designColours.constEnd(); ++it) {
        if (it.value() == button) { key = it.key(); break; }
    }
    if (key.isEmpty()) return;

    const QColor current = m_designColourValues.value(key);
    const QColor chosen = QColorDialog::getColor(
        current, this, i18n("Choose Colour"),
        QColorDialog::ShowAlphaChannel);
    if (!chosen.isValid()) return;

    m_designColourValues.insert(key, chosen);
    button->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid palette(mid);")
                          .arg(chosen.name(QColor::HexArgb)));
}

// ---------------------------------------------------------------------------
// tree <-> live items
//
// Every row keeps the DhRadialItemSP it was built from attached (ROLE_ITEM).
// Field edits write straight into that object; harvest() only ever needs to
// reconcile parent/child nesting and order onto it, never rebuild it. This is
// what makes a v2 field this UI does not surface (or a key a newer desktop
// plugin added) survive a save: the object edited here IS the object that
// gets serialised, `raw` included, not a fresh stand-in built from column
// text.
// ---------------------------------------------------------------------------

QTreeWidgetItem *KisDhRadialSettings::makeRow(const DhRadialItemSP &itemIn)
{
    DhRadialItemSP item = itemIn ? itemIn : DhRadialItemSP(new DhRadialItem());

    QTreeWidgetItem *row = new QTreeWidgetItem();
    row->setFlags(row->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
    row->setText(COLUMN_LABEL, item->label);
    row->setText(COLUMN_ACTION, item->action);
    if (item->color.isValid()) {
        row->setData(COLUMN_LABEL, ROLE_COLOUR, item->color.name());
    }
    row->setData(COLUMN_LABEL, ROLE_ITEM, QVariant::fromValue(item));

    for (const DhRadialItemSP &child : item->children) {
        row->addChild(makeRow(child));
    }
    return row;
}

void KisDhRadialSettings::refreshRow(QTreeWidgetItem *row) const
{
    if (!row) return;
    const QString colour = row->data(COLUMN_LABEL, ROLE_COLOUR).toString();
    if (!colour.isEmpty()) {
        row->setForeground(COLUMN_LABEL, QColor(colour));
    } else {
        row->setData(COLUMN_LABEL, Qt::ForegroundRole, QVariant());
    }
    // A branch ignores its own action, so stop implying otherwise.
    if (row->childCount() > 0) {
        const DhRadialItemSP item = row->data(COLUMN_LABEL, ROLE_ITEM).value<DhRadialItemSP>();
        row->setText(COLUMN_ACTION, item ? layoutTag(item->slotLayout) : i18n("(submenu)"));
    }
}

void KisDhRadialSettings::populateTree(const QList<DhRadialItemSP> &items)
{
    m_tree->blockSignals(true);
    m_tree->clear();
    for (const DhRadialItemSP &item : items) {
        QTreeWidgetItem *row = makeRow(item);
        m_tree->addTopLevelItem(row);
    }
    m_tree->expandAll();
    m_tree->blockSignals(false);

    QTreeWidgetItemIterator it(m_tree);
    while (*it) { refreshRow(*it); ++it; }

    if (m_tree->topLevelItemCount()) {
        m_tree->setCurrentItem(m_tree->topLevelItem(0));
    }
    slotSelectionChanged();
}

QList<DhRadialItemSP> KisDhRadialSettings::harvest(const QTreeWidgetItem *parent) const
{
    QList<DhRadialItemSP> items;

    const int count = parent ? parent->childCount() : m_tree->topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        const QTreeWidgetItem *row = parent ? parent->child(i) : m_tree->topLevelItem(i);
        if (!row) continue;

        DhRadialItemSP item = row->data(COLUMN_LABEL, ROLE_ITEM).value<DhRadialItemSP>();
        if (!item) continue;   // should not happen; every row is made by makeRow()

        item->children = (row->childCount() > 0) ? harvest(row) : QList<DhRadialItemSP>();
        items.append(item);
    }

    return items;
}

DhRadialItemSP KisDhRadialSettings::currentItem() const
{
    QTreeWidgetItem *row = m_tree->currentItem();
    if (!row) return DhRadialItemSP();
    return row->data(COLUMN_LABEL, ROLE_ITEM).value<DhRadialItemSP>();
}

// ---------------------------------------------------------------------------
// editing
// ---------------------------------------------------------------------------

void KisDhRadialSettings::slotSelectionChanged()
{
    QTreeWidgetItem *row = m_tree->currentItem();
    DhRadialItemSP item = currentItem();

    m_updatingFields = true;
    if (row && item) {
        m_label->setText(item->label);
        m_action->setText(row->childCount() > 0 ? QString() : item->action);
        updateSwatch(item->color);

        m_icon->setText(item->icon);
        m_tooltip->setText(item->tooltip);
        m_shape->setCurrentIndex(item->shape == QLatin1String("CIRCLE") ? 2
                                 : item->shape == QLatin1String("RECTANGLE") ? 1 : 0);

        int layoutIdx = 0;
        switch (item->slotLayout) {
        case DhRadialItem::Grid:     layoutIdx = 1; break;
        case DhRadialItem::Drawer:   layoutIdx = 2; break;
        case DhRadialItem::Dropdown: layoutIdx = 3; break;
        case DhRadialItem::Sprout:   layoutIdx = 4; break;
        case DhRadialItem::Submenu:
        default:                     layoutIdx = 0; break;
        }
        m_layout->setCurrentIndex(layoutIdx);

        m_columns->setValue(item->columns);
        m_colSpan->setValue(item->colSpan);
        m_cellWidth->setValue(item->cellWidth);
        m_cellHeight->setValue(item->cellHeight);

        int dirIdx = m_drawerDirection->findText(item->drawerDirection);
        m_drawerDirection->setCurrentIndex(dirIdx >= 0 ? dirIdx : 0);
        int flowIdx = m_drawerFlow->findText(item->drawerFlow);
        m_drawerFlow->setCurrentIndex(flowIdx >= 0 ? flowIdx : 0);
        int alignIdx = m_drawerAlign->findText(item->drawerAlign);
        m_drawerAlign->setCurrentIndex(alignIdx >= 0 ? alignIdx : 0);
    } else {
        m_label->clear();
        m_action->clear();
        updateSwatch(QColor());
        m_icon->clear();
        m_tooltip->clear();
        m_shape->setCurrentIndex(0);
        m_layout->setCurrentIndex(0);
        m_columns->setValue(0);
        m_colSpan->setValue(1.0);
        m_cellWidth->setValue(0);
        m_cellHeight->setValue(0);
        m_drawerDirection->setCurrentIndex(0);
        m_drawerFlow->setCurrentIndex(0);
        m_drawerAlign->setCurrentIndex(0);
    }
    m_updatingFields = false;

    updateButtonStates();
    updateDetailVisibility();
}

void KisDhRadialSettings::updateButtonStates()
{
    QTreeWidgetItem *row = m_tree->currentItem();
    const bool have = row != nullptr;
    const bool branch = have && row->childCount() > 0;

    m_label->setEnabled(have);
    m_action->setEnabled(have && !branch);
    m_pick->setEnabled(have && !branch);
    m_swatch->setEnabled(have);
    m_inherit->setEnabled(have);
    m_icon->setEnabled(have);
    m_pickIcon->setEnabled(have);
    m_tooltip->setEnabled(have);
    m_shape->setEnabled(have);
    m_addChild->setEnabled(have);
    m_remove->setEnabled(have);
    m_up->setEnabled(have);
    m_down->setEnabled(have);
}

void KisDhRadialSettings::updateDetailVisibility()
{
    QTreeWidgetItem *row = m_tree->currentItem();
    const bool branch = row && row->childCount() > 0;
    DhRadialItemSP item = currentItem();

    m_layout->setEnabled(branch);

    const bool isGrid = branch && item && item->slotLayout == DhRadialItem::Grid;
    const bool isDrawerLike = branch && item
        && (item->slotLayout == DhRadialItem::Drawer || item->slotLayout == DhRadialItem::Dropdown);
    const bool isDrawerGridFlow = isDrawerLike && item->drawerFlow == QLatin1String("GRID");

    m_columns->setEnabled(isGrid || isDrawerGridFlow);
    m_colSpan->setEnabled(isGrid || isDrawerGridFlow);
    m_cellWidth->setEnabled(isGrid || isDrawerLike);
    m_cellHeight->setEnabled(isGrid || isDrawerLike);

    m_drawerDirection->setEnabled(isDrawerLike);
    m_drawerFlow->setEnabled(isDrawerLike);
    m_drawerAlign->setEnabled(isDrawerLike);
}

void KisDhRadialSettings::slotLabelEdited(const QString &text)
{
    if (m_updatingFields) return;
    if (QTreeWidgetItem *row = m_tree->currentItem()) {
        row->setText(COLUMN_LABEL, text);
        if (DhRadialItemSP item = currentItem()) item->label = text;
    }
}

void KisDhRadialSettings::slotActionEdited(const QString &text)
{
    if (m_updatingFields) return;
    QTreeWidgetItem *row = m_tree->currentItem();
    if (row && row->childCount() == 0) {
        row->setText(COLUMN_ACTION, text);
        if (DhRadialItemSP item = currentItem()) item->action = text;
    }
}

void KisDhRadialSettings::slotPickAction()
{
    QTreeWidgetItem *row = m_tree->currentItem();
    if (!row || row->childCount() > 0) return;

    DhActionPicker picker(this);
    if (picker.exec() != QDialog::Accepted) return;

    const QString id = picker.selectedId();
    if (id.isEmpty()) return;

    row->setText(COLUMN_ACTION, id);
    if (DhRadialItemSP item = currentItem()) item->action = id;
    m_updatingFields = true;
    m_action->setText(id);
    m_updatingFields = false;
}

void KisDhRadialSettings::updateSwatch(const QColor &colour)
{
    const QString css = colour.isValid() ? colour.name() : QStringLiteral("transparent");
    m_swatch->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid palette(mid);").arg(css));
    m_swatch->setToolTip(colour.isValid() ? colour.name() : i18n("No colour set"));
}

void KisDhRadialSettings::slotPickColour()
{
    QTreeWidgetItem *row = m_tree->currentItem();
    if (!row) return;

    const QColor current(row->data(COLUMN_LABEL, ROLE_COLOUR).toString());
    const QColor chosen = QColorDialog::getColor(current.isValid() ? current : QColor("#4A8FD4"),
                                                 this, i18n("Wedge Colour"));
    if (!chosen.isValid()) return;

    row->setData(COLUMN_LABEL, ROLE_COLOUR, chosen.name());
    if (DhRadialItemSP item = currentItem()) item->color = chosen;
    refreshRow(row);
    updateSwatch(chosen);
}

void KisDhRadialSettings::slotClearColour()
{
    QTreeWidgetItem *row = m_tree->currentItem();
    if (!row) return;
    row->setData(COLUMN_LABEL, ROLE_COLOUR, QVariant());
    if (DhRadialItemSP item = currentItem()) item->color = QColor();
    refreshRow(row);
    updateSwatch(QColor());
}

void KisDhRadialSettings::slotIconEdited(const QString &text)
{
    if (m_updatingFields) return;
    if (DhRadialItemSP item = currentItem()) item->icon = text;
}

void KisDhRadialSettings::slotPickIcon()
{
    if (!currentItem()) return;

    DhIconPicker picker(this);
    if (picker.exec() != QDialog::Accepted) return;

    const QString spec = picker.chosen();
    if (DhRadialItemSP item = currentItem()) item->icon = spec;
    m_updatingFields = true;
    m_icon->setText(spec);
    m_updatingFields = false;
}

void KisDhRadialSettings::slotTooltipEdited(const QString &text)
{
    if (m_updatingFields) return;
    if (DhRadialItemSP item = currentItem()) item->tooltip = text;
}

void KisDhRadialSettings::slotShapeChanged(int index)
{
    if (m_updatingFields) return;
    if (DhRadialItemSP item = currentItem()) {
        item->shape = index == 2 ? QStringLiteral("CIRCLE")
                    : index == 1 ? QStringLiteral("RECTANGLE") : QString();
    }
}

void KisDhRadialSettings::slotLayoutChanged(int index)
{
    if (m_updatingFields) return;
    DhRadialItemSP item = currentItem();
    if (!item) return;

    switch (index) {
    case 1: item->slotLayout = DhRadialItem::Grid; break;
    case 2: item->slotLayout = DhRadialItem::Drawer; break;
    case 3: item->slotLayout = DhRadialItem::Dropdown; break;
    case 4: item->slotLayout = DhRadialItem::Sprout; break;
    case 0:
    default: item->slotLayout = DhRadialItem::Submenu; break;
    }

    if (QTreeWidgetItem *row = m_tree->currentItem()) refreshRow(row);
    updateDetailVisibility();
}

void KisDhRadialSettings::slotColumnsChanged(int value)
{
    if (m_updatingFields) return;
    if (DhRadialItemSP item = currentItem()) item->columns = value;
}

void KisDhRadialSettings::slotColSpanChanged(double value)
{
    if (m_updatingFields) return;
    if (DhRadialItemSP item = currentItem()) item->colSpan = value;
}

void KisDhRadialSettings::slotCellWidthChanged(int value)
{
    if (m_updatingFields) return;
    if (DhRadialItemSP item = currentItem()) item->cellWidth = value;
}

void KisDhRadialSettings::slotCellHeightChanged(int value)
{
    if (m_updatingFields) return;
    if (DhRadialItemSP item = currentItem()) item->cellHeight = value;
}

void KisDhRadialSettings::slotDrawerDirectionChanged(int index)
{
    if (m_updatingFields) return;
    if (DhRadialItemSP item = currentItem()) item->drawerDirection = m_drawerDirection->itemText(index);
}

void KisDhRadialSettings::slotDrawerFlowChanged(int index)
{
    if (m_updatingFields) return;
    if (DhRadialItemSP item = currentItem()) item->drawerFlow = m_drawerFlow->itemText(index);
    updateDetailVisibility();
}

void KisDhRadialSettings::slotDrawerAlignChanged(int index)
{
    if (m_updatingFields) return;
    if (DhRadialItemSP item = currentItem()) item->drawerAlign = m_drawerAlign->itemText(index);
}

void KisDhRadialSettings::slotAddSibling()
{
    QTreeWidgetItem *row = makeRow(DhRadialItemSP());
    row->setText(COLUMN_LABEL, i18n("New Item"));
    if (DhRadialItemSP item = row->data(COLUMN_LABEL, ROLE_ITEM).value<DhRadialItemSP>()) {
        item->label = i18n("New Item");
    }

    QTreeWidgetItem *current = m_tree->currentItem();
    if (current && current->parent()) {
        current->parent()->insertChild(current->parent()->indexOfChild(current) + 1, row);
    } else if (current) {
        m_tree->insertTopLevelItem(m_tree->indexOfTopLevelItem(current) + 1, row);
    } else {
        m_tree->addTopLevelItem(row);
    }

    m_tree->setCurrentItem(row);
}

void KisDhRadialSettings::slotAddChild()
{
    QTreeWidgetItem *current = m_tree->currentItem();
    if (!current) return;

    QTreeWidgetItem *row = makeRow(DhRadialItemSP());
    row->setText(COLUMN_LABEL, i18n("New Item"));
    if (DhRadialItemSP item = row->data(COLUMN_LABEL, ROLE_ITEM).value<DhRadialItemSP>()) {
        item->label = i18n("New Item");
    }

    current->addChild(row);
    current->setExpanded(true);
    refreshRow(current);
    m_tree->setCurrentItem(row);
}

void KisDhRadialSettings::slotRemove()
{
    QTreeWidgetItem *row = m_tree->currentItem();
    if (!row) return;

    QTreeWidgetItem *parent = row->parent();
    if (parent) {
        parent->removeChild(row);
        refreshRow(parent);
    } else {
        m_tree->takeTopLevelItem(m_tree->indexOfTopLevelItem(row));
    }
    delete row;

    slotSelectionChanged();
}

void KisDhRadialSettings::move(int delta)
{
    QTreeWidgetItem *row = m_tree->currentItem();
    if (!row) return;

    QTreeWidgetItem *parent = row->parent();
    const int index = parent ? parent->indexOfChild(row) : m_tree->indexOfTopLevelItem(row);
    const int count = parent ? parent->childCount() : m_tree->topLevelItemCount();
    const int target = index + delta;
    if (target < 0 || target >= count) return;

    const bool expanded = row->isExpanded();

    if (parent) {
        parent->takeChild(index);
        parent->insertChild(target, row);
    } else {
        m_tree->takeTopLevelItem(index);
        m_tree->insertTopLevelItem(target, row);
    }

    row->setExpanded(expanded);
    m_tree->setCurrentItem(row);
}

void KisDhRadialSettings::slotMoveUp()   { move(-1); }
void KisDhRadialSettings::slotMoveDown() { move(+1); }

// ---------------------------------------------------------------------------
// KisPreferenceSet
// ---------------------------------------------------------------------------

void KisDhRadialSettings::loadPreferences()
{
    DhRadialDesign design;
    populateTree(DhRadialConfig::load(&design));
    designToWidgets(design);
}

void KisDhRadialSettings::loadDefaultPreferences()
{
    DhRadialDesign design;
    populateTree(DhRadialConfig::loadDefaults());
    designToWidgets(design);
}

void KisDhRadialSettings::savePreferences() const
{
    const DhRadialDesign design = designFromWidgets();

    QString error;
    if (!DhRadialConfig::save(harvest(nullptr), &design, &error)) {
        warnKrita << "DH radial menu: could not save the layout:" << error;
    }

    Q_EMIT settingsChanged();
}
