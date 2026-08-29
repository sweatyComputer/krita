/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISTOUCHGESTURESETTINGS_H
#define KISTOUCHGESTURESETTINGS_H

#include <QObject>
#include <QString>
#include <kis_preference_set_registry.h>

#include "ui_wdg_touchgesturesettings.h"

class QComboBox;

class WdgTouchGestureSettings : public QWidget, public Ui::WdgTouchGestureSettings
{
    Q_OBJECT
public:
    WdgTouchGestureSettings(QWidget *parent) : QWidget(parent) {}
};

/**
 * The "Touch Gestures" page in Configure Krita. Writes the kritarc
 * [AndroidGestures] group that KisAndroidGestureBridge reads.
 */
class KisTouchGestureSettings : public KisPreferenceSet
{
    Q_OBJECT
public:
    KisTouchGestureSettings(QWidget *parent = 0);
    ~KisTouchGestureSettings() override;

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

private:
    void fillMappingCombo(QComboBox *combo) const;
    void selectMapping(QComboBox *combo, const QString &id) const;
    QString mappingAt(const QComboBox *combo) const;

    WdgTouchGestureSettings *mUi;
};

class KisTouchGestureSettingsUpdateRepeater : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void settingsUpdated();
public Q_SLOTS:
    void updateSettings() {
        Q_EMIT settingsUpdated();
    }
};

class KisTouchGestureSettingsFactory : public KisAbstractPreferenceSetFactory
{
public:
    KisPreferenceSet *createPreferenceSet() override {
        KisTouchGestureSettings *ps = new KisTouchGestureSettings();
        QObject::connect(ps, SIGNAL(settingsChanged()), &repeater, SLOT(updateSettings()), Qt::UniqueConnection);
        return ps;
    }
    QString id() const override { return "TouchGestureSettings"; }
    KisTouchGestureSettingsUpdateRepeater repeater;
};

#endif // KISTOUCHGESTURESETTINGS_H
