/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisDhRadialSettings.h"

#include <QAction>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QTreeWidgetItemIterator>
#include <QVector>
#include <QColor>

#include <algorithm>

#include <klocalizedstring.h>
#include <kconfiggroup.h>
#include <ksharedconfig.h>
#include <kactioncollection.h>
#include <KisActionsSnapshot.h>
#include <KisPart.h>
#include <KisMainWindow.h>
#include <kis_icon_utils.h>
#include <kis_debug.h>

namespace {

const char *CONFIG_GROUP = "DhRadialMenu";
const int COLUMN_LABEL = 0;
const int COLUMN_ACTION = 1;
const int ROLE_COLOUR = Qt::UserRole + 1;

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

} // namespace

KisDhRadialSettings::KisDhRadialSettings(QWidget *parent)
    : KisPreferenceSet(parent)
{
    buildUi();
    loadPreferences();
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
        i18n("An item with children is a branch: tapping it opens a sub-ring and its own "
             "action is ignored. An item with an action is a wedge that fires it. "
             "Bind the menu itself to a key or the S-Pen under Configure Shortcuts, "
             "search for \"Radial Menu\"."), this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    root->addWidget(splitter, 1);

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
    QWidget *right = new QWidget(splitter);
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

    rightLayout->addWidget(props);

    QGroupBox *sizeBox = new QGroupBox(i18n("Size"), right);
    QFormLayout *sizeForm = new QFormLayout(sizeBox);

    m_outerRadius = new QSpinBox(sizeBox);
    m_outerRadius->setRange(70, 600);
    m_outerRadius->setSuffix(i18n(" px"));
    m_outerRadius->setToolTip(i18n("Radius of the ring. It shrinks automatically if it "
                                   "would not fit on screen."));
    sizeForm->addRow(i18n("Outer radius:"), m_outerRadius);

    m_innerRatio = new QDoubleSpinBox(sizeBox);
    m_innerRatio->setRange(0.15, 0.75);
    m_innerRatio->setSingleStep(0.02);
    m_innerRatio->setDecimals(2);
    m_innerRatio->setToolTip(i18n("Size of the hole in the middle, as a fraction of the "
                                  "outer radius. The hole is the back/close button."));
    sizeForm->addRow(i18n("Hub size:"), m_innerRatio);

    rightLayout->addWidget(sizeBox);
    rightLayout->addStretch();
    splitter->addWidget(right);

    splitter->setSizes(QList<int>() << 430 << 370);

    connect(m_tree, SIGNAL(itemSelectionChanged()), this, SLOT(slotSelectionChanged()));
    connect(m_label, SIGNAL(textEdited(QString)), this, SLOT(slotLabelEdited(QString)));
    connect(m_action, SIGNAL(textEdited(QString)), this, SLOT(slotActionEdited(QString)));
    connect(m_pick, SIGNAL(clicked()), this, SLOT(slotPickAction()));
    connect(m_swatch, SIGNAL(clicked()), this, SLOT(slotPickColour()));
    connect(m_inherit, SIGNAL(clicked()), this, SLOT(slotClearColour()));
    connect(m_addSibling, SIGNAL(clicked()), this, SLOT(slotAddSibling()));
    connect(m_addChild, SIGNAL(clicked()), this, SLOT(slotAddChild()));
    connect(m_remove, SIGNAL(clicked()), this, SLOT(slotRemove()));
    connect(m_up, SIGNAL(clicked()), this, SLOT(slotMoveUp()));
    connect(m_down, SIGNAL(clicked()), this, SLOT(slotMoveDown()));
}

// ---------------------------------------------------------------------------
// tree <-> config
// ---------------------------------------------------------------------------

QTreeWidgetItem *KisDhRadialSettings::makeRow(const DhRadialItemSP &item) const
{
    QTreeWidgetItem *row = new QTreeWidgetItem();
    row->setFlags(row->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
    row->setText(COLUMN_LABEL, item ? item->label : QString());
    row->setText(COLUMN_ACTION, item ? item->action : QString());
    if (item && item->color.isValid()) {
        row->setData(COLUMN_LABEL, ROLE_COLOUR, item->color.name());
    }
    if (item) {
        for (const DhRadialItemSP &child : item->children) {
            row->addChild(makeRow(child));
        }
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
        row->setText(COLUMN_ACTION, i18n("(submenu)"));
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

        DhRadialItemSP item(new DhRadialItem());
        item->label = row->text(COLUMN_LABEL);

        const QString colour = row->data(COLUMN_LABEL, ROLE_COLOUR).toString();
        if (!colour.isEmpty()) item->color = QColor(colour);

        if (row->childCount() > 0) {
            item->children = harvest(row);
        } else {
            const QString action = row->text(COLUMN_ACTION);
            item->action = (action == i18n("(submenu)")) ? QString() : action;
        }

        items.append(item);
    }

    return items;
}

// ---------------------------------------------------------------------------
// editing
// ---------------------------------------------------------------------------

void KisDhRadialSettings::slotSelectionChanged()
{
    QTreeWidgetItem *row = m_tree->currentItem();

    m_updatingFields = true;
    if (row) {
        m_label->setText(row->text(COLUMN_LABEL));
        m_action->setText(row->childCount() > 0 ? QString() : row->text(COLUMN_ACTION));
        updateSwatch(QColor(row->data(COLUMN_LABEL, ROLE_COLOUR).toString()));
    } else {
        m_label->clear();
        m_action->clear();
        updateSwatch(QColor());
    }
    m_updatingFields = false;

    updateButtonStates();
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
    m_addChild->setEnabled(have);
    m_remove->setEnabled(have);
    m_up->setEnabled(have);
    m_down->setEnabled(have);
}

void KisDhRadialSettings::slotLabelEdited(const QString &text)
{
    if (m_updatingFields) return;
    if (QTreeWidgetItem *row = m_tree->currentItem()) {
        row->setText(COLUMN_LABEL, text);
    }
}

void KisDhRadialSettings::slotActionEdited(const QString &text)
{
    if (m_updatingFields) return;
    QTreeWidgetItem *row = m_tree->currentItem();
    if (row && row->childCount() == 0) {
        row->setText(COLUMN_ACTION, text);
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
    refreshRow(row);
    updateSwatch(chosen);
}

void KisDhRadialSettings::slotClearColour()
{
    QTreeWidgetItem *row = m_tree->currentItem();
    if (!row) return;
    row->setData(COLUMN_LABEL, ROLE_COLOUR, QVariant());
    refreshRow(row);
    updateSwatch(QColor());
}

void KisDhRadialSettings::slotAddSibling()
{
    QTreeWidgetItem *row = new QTreeWidgetItem();
    row->setFlags(row->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
    row->setText(COLUMN_LABEL, i18n("New Item"));

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

    QTreeWidgetItem *row = new QTreeWidgetItem();
    row->setFlags(row->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
    row->setText(COLUMN_LABEL, i18n("New Item"));

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
    populateTree(DhRadialConfig::load());

    KConfigGroup cfg = KSharedConfig::openConfig()->group(CONFIG_GROUP);
    m_outerRadius->setValue(qRound(cfg.readEntry("outerRadius", 170.0)));
    m_innerRatio->setValue(cfg.readEntry("innerRatio", 0.38));
}

void KisDhRadialSettings::loadDefaultPreferences()
{
    populateTree(DhRadialConfig::loadDefaults());
    m_outerRadius->setValue(170);
    m_innerRatio->setValue(0.38);
}

void KisDhRadialSettings::savePreferences() const
{
    QString error;
    if (!DhRadialConfig::save(harvest(nullptr), &error)) {
        warnKrita << "DH radial menu: could not save the layout:" << error;
    }

    KConfigGroup cfg = KSharedConfig::openConfig()->group(CONFIG_GROUP);
    cfg.writeEntry("outerRadius", static_cast<double>(m_outerRadius->value()));
    cfg.writeEntry("innerRatio", m_innerRatio->value());
    cfg.sync();

    Q_EMIT settingsChanged();
}
