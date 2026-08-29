/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISDHRADIALSETTINGS_H
#define KISDHRADIALSETTINGS_H

#include <QList>
#include <QString>
#include <kis_preference_set_registry.h>

#include "DhRadialConfig.h"

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QPushButton;
class QDoubleSpinBox;
class QSpinBox;

/**
 * "DH Radial Menu" page in Configure Krita: a tree editor for the wedges.
 *
 * Same model as the Python version this replaces -- an item with children is a
 * branch that opens a sub-ring, an item with an action is a leaf that fires it
 * -- so an existing layout reads straight in. The difference is the action
 * picker, which enumerates the live action collection instead of a hand-kept
 * list, so it can never drift out of date with the build you are running.
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

private:
    void buildUi();
    void populateTree(const QList<DhRadialItemSP> &items);
    QTreeWidgetItem *makeRow(const DhRadialItemSP &item) const;
    void refreshRow(QTreeWidgetItem *row) const;
    QList<DhRadialItemSP> harvest(const QTreeWidgetItem *parent) const;
    void move(int delta);
    void updateSwatch(const QColor &colour);
    void updateButtonStates();

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
    QSpinBox *m_outerRadius{nullptr};
    QDoubleSpinBox *m_innerRatio{nullptr};

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
