/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISDHRADIALSETTINGS_H
#define KISDHRADIALSETTINGS_H

#include <QList>
#include <QMap>
#include <QString>
#include <kis_preference_set_registry.h>

#include "DhRadialConfig.h"
#include "DhRadialDesign.h"

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QPushButton;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QWidget;
class QFormLayout;

/**
 * "DH Radial Menu" page in Configure Krita: a tree editor for the wedges,
 * plus a full editor for the design block (colours, sizing, panel layouts,
 * presets) that used to only be authorable on desktop.
 *
 * The item tree keeps the live DhRadialItemSP each row was built from
 * attached to the row (Qt::UserRole), rather than re-parsing text back into
 * fresh objects on save. Anything this build does not have a field for -
 * unknown JSON keys from a newer desktop plugin - lives in that same object's
 * `raw` and survives a save untouched. Rebuilding fresh objects from column
 * text, the old approach, silently dropped every v2 field it did not know
 * about; this does not.
 */
class KisDhRadialSettings : public KisPreferenceSet
{
    Q_OBJECT
public:
    KisDhRadialSettings(QWidget *parent = 0);
    ~KisDhRadialSettings() override;

    QString id() override;
    QString name() override;
    QString header() override;
    QIcon icon() override;

public Q_SLOTS:
    void savePreferences() const override;
    void loadPreferences() override;
    void loadDefaultPreferences() override;

Q_SIGNALS:
    void settingsChanged() const;

private Q_SLOTS:
    void slotSelectionChanged();
    void slotLabelEdited(const QString &text);
    void slotActionEdited(const QString &text);
    void slotPickAction();
    void slotPickColour();
    void slotClearColour();
    void slotAddSibling();
    void slotAddChild();
    void slotRemove();
    void slotMoveUp();
    void slotMoveDown();
    void slotIconEdited(const QString &text);
    void slotTooltipEdited(const QString &text);
    void slotShapeChanged(int index);
    void slotLayoutChanged(int index);
    void slotColumnsChanged(int value);
    void slotColSpanChanged(double value);
    void slotCellWidthChanged(int value);
    void slotCellHeightChanged(int value);
    void slotDrawerDirectionChanged(int index);
    void slotDrawerFlowChanged(int index);
    void slotDrawerAlignChanged(int index);
    void slotApplyPreset();
    void slotDesignColourClicked();

private:
    void buildUi();
    QWidget *buildItemsTab(QWidget *parent);
    QWidget *buildDesignTab(QWidget *parent);

    void populateTree(const QList<DhRadialItemSP> &items);
    QTreeWidgetItem *makeRow(const DhRadialItemSP &item);
    void refreshRow(QTreeWidgetItem *row) const;
    QList<DhRadialItemSP> harvest(const QTreeWidgetItem *parent) const;
    void move(int delta);
    void updateSwatch(const QColor &colour);
    void updateButtonStates();
    void updateDetailVisibility();
    DhRadialItemSP currentItem() const;

    // ----- design tab helpers -----------------------------------------------
    QSpinBox *addIntRow(const QString &key, QFormLayout *form, const QString &label,
                        int min, int max, const QString &suffix = QString());
    QDoubleSpinBox *addRealRow(const QString &key, QFormLayout *form, const QString &label,
                               double min, double max, double step = 0.05);
    QCheckBox *addBoolRow(const QString &key, QFormLayout *form, const QString &label);
    QComboBox *addComboRow(const QString &key, QFormLayout *form, const QString &label,
                           const QStringList &options);
    QPushButton *addColourRow(const QString &key, QFormLayout *form, const QString &label);

    DhRadialDesign designFromWidgets() const;
    void designToWidgets(const DhRadialDesign &design);

    QTreeWidget *m_tree{nullptr};
    QLineEdit *m_label{nullptr};
    QLineEdit *m_action{nullptr};
    QPushButton *m_pick{nullptr};
    QPushButton *m_swatch{nullptr};
    QPushButton *m_inherit{nullptr};
    QPushButton *m_addSibling{nullptr};
    QPushButton *m_addChild{nullptr};
    QPushButton *m_remove{nullptr};
    QPushButton *m_up{nullptr};
    QPushButton *m_down{nullptr};

    // ----- per-item v2 fields -------------------------------------------
    QLineEdit *m_icon{nullptr};
    QLineEdit *m_tooltip{nullptr};
    QComboBox *m_shape{nullptr};
    QComboBox *m_layout{nullptr};
    QSpinBox *m_columns{nullptr};
    QDoubleSpinBox *m_colSpan{nullptr};
    QSpinBox *m_cellWidth{nullptr};
    QSpinBox *m_cellHeight{nullptr};
    QComboBox *m_drawerDirection{nullptr};
    QComboBox *m_drawerFlow{nullptr};
    QComboBox *m_drawerAlign{nullptr};

    // ----- design tab: every control keyed by the same name design.py /
    // DhRadialDesign::read() use, so applying a preset or (de)serialising is
    // one lookup instead of fifty named members. -----------------------------
    QMap<QString, QSpinBox *> m_designInts;
    QMap<QString, QDoubleSpinBox *> m_designReals;
    QMap<QString, QCheckBox *> m_designBools;
    QMap<QString, QComboBox *> m_designCombos;
    QMap<QString, QPushButton *> m_designColours;
    QMap<QString, QColor> m_designColourValues;
    QComboBox *m_presetCombo{nullptr};
    QLineEdit *m_fontFamily{nullptr};

    bool m_updatingFields{false};
};

class KisDhRadialSettingsUpdateRepeater : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void settingsUpdated();
public Q_SLOTS:
    void updateSettings() { Q_EMIT settingsUpdated(); }
};

class KisDhRadialSettingsFactory : public KisAbstractPreferenceSetFactory
{
public:
    KisPreferenceSet *createPreferenceSet() override {
        KisDhRadialSettings *ps = new KisDhRadialSettings();
        QObject::connect(ps, SIGNAL(settingsChanged()), &repeater, SLOT(updateSettings()),
                         Qt::UniqueConnection);
        return ps;
    }
    QString id() const override { return "DhRadialSettings"; }
    KisDhRadialSettingsUpdateRepeater repeater;
};

#endif // KISDHRADIALSETTINGS_H
