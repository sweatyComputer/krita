/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisTouchGestureSettings.h"

#include <QComboBox>

#include <kconfiggroup.h>
#include <ksharedconfig.h>
#include <klocalizedstring.h>
#include <kis_icon_utils.h>

#include <input/KisAndroidGestureBridge.h>

namespace {
const char *CONFIG_GROUP = "AndroidGestures";
}

KisTouchGestureSettings::KisTouchGestureSettings(QWidget *parent)
    : KisPreferenceSet(parent)
{
    mUi = new WdgTouchGestureSettings(this);
    mUi->setupUi(this);

    fillMappingCombo(mUi->cmbVerticalAction);
    fillMappingCombo(mUi->cmbHorizontalAction);

    loadPreferences();
}

KisTouchGestureSettings::~KisTouchGestureSettings()
{
    delete mUi;
}

QString KisTouchGestureSettings::id()
{
    return QString("TouchGestureSettings");
}

QString KisTouchGestureSettings::name()
{
    return header();
}

QString KisTouchGestureSettings::header()
{
    return QString(i18n("Touch Gestures"));
}

QIcon KisTouchGestureSettings::icon()
{
    return KisIconUtils::loadIcon("input-touchscreen");
}

void KisTouchGestureSettings::fillMappingCombo(QComboBox *combo) const
{
    using Bridge = KisAndroidGestureBridge;

    combo->clear();
    combo->addItem(i18n("Do nothing"), Bridge::mappingToId(Bridge::NoMapping));
    combo->addItem(i18n("Brush Size"), Bridge::mappingToId(Bridge::BrushSize));
    combo->addItem(i18n("Opacity"),    Bridge::mappingToId(Bridge::Opacity));
    combo->addItem(i18n("Flow"),       Bridge::mappingToId(Bridge::Flow));
}

void KisTouchGestureSettings::selectMapping(QComboBox *combo, const QString &id) const
{
    const int index = combo->findData(id);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

QString KisTouchGestureSettings::mappingAt(const QComboBox *combo) const
{
    return combo->currentData().toString();
}

void KisTouchGestureSettings::savePreferences() const
{
    KConfigGroup cfg = KSharedConfig::openConfig()->group(CONFIG_GROUP);

    cfg.writeEntry("enabled", mUi->chkEnabled->isChecked());
    cfg.writeEntry("verticalAction", mappingAt(mUi->cmbVerticalAction));
    cfg.writeEntry("horizontalAction", mappingAt(mUi->cmbHorizontalAction));
    cfg.writeEntry("invertVertical", mUi->chkInvertVertical->isChecked());
    cfg.writeEntry("invertHorizontal", mUi->chkInvertHorizontal->isChecked());
    cfg.writeEntry("lockToAxis", mUi->chkLockToAxis->isChecked());
    cfg.writeEntry("showFeedback", mUi->chkShowFeedback->isChecked());
    cfg.writeEntry("sizeSensitivity", mUi->spbSizeSensitivity->value());
    cfg.writeEntry("opacitySensitivity", mUi->spbOpacitySensitivity->value() / 1000.0);
    cfg.writeEntry("deadZone", static_cast<double>(mUi->spbDeadZone->value()));

    cfg.sync();

    Q_EMIT settingsChanged();
}

void KisTouchGestureSettings::loadPreferences()
{
    using Bridge = KisAndroidGestureBridge;

    KConfigGroup cfg = KSharedConfig::openConfig()->group(CONFIG_GROUP);

    mUi->chkEnabled->setChecked(cfg.readEntry("enabled", true));
    selectMapping(mUi->cmbVerticalAction,
                  cfg.readEntry("verticalAction", Bridge::mappingToId(Bridge::BrushSize)));
    selectMapping(mUi->cmbHorizontalAction,
                  cfg.readEntry("horizontalAction", Bridge::mappingToId(Bridge::Opacity)));
    mUi->chkInvertVertical->setChecked(cfg.readEntry("invertVertical", false));
    mUi->chkInvertHorizontal->setChecked(cfg.readEntry("invertHorizontal", false));
    mUi->chkLockToAxis->setChecked(cfg.readEntry("lockToAxis", true));
    mUi->chkShowFeedback->setChecked(cfg.readEntry("showFeedback", false));
    mUi->spbSizeSensitivity->setValue(cfg.readEntry("sizeSensitivity", 0.5));
    // Stored as opacity-per-dp; shown per 1000 dp so the number is readable.
    mUi->spbOpacitySensitivity->setValue(cfg.readEntry("opacitySensitivity", 0.0025) * 1000.0);
    mUi->spbDeadZone->setValue(qRound(cfg.readEntry("deadZone", 12.0)));
}

void KisTouchGestureSettings::loadDefaultPreferences()
{
    using Bridge = KisAndroidGestureBridge;

    mUi->chkEnabled->setChecked(true);
    selectMapping(mUi->cmbVerticalAction, Bridge::mappingToId(Bridge::BrushSize));
    selectMapping(mUi->cmbHorizontalAction, Bridge::mappingToId(Bridge::Opacity));
    mUi->chkInvertVertical->setChecked(false);
    mUi->chkInvertHorizontal->setChecked(false);
    mUi->chkLockToAxis->setChecked(true);
    mUi->chkShowFeedback->setChecked(false);
    mUi->spbSizeSensitivity->setValue(0.5);
    mUi->spbOpacitySensitivity->setValue(2.5);
    mUi->spbDeadZone->setValue(12);
}
