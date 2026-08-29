/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TOUCHGESTURES_H
#define TOUCHGESTURES_H

#include <QObject>
#include <KisActionPlugin.h>

/**
 * Android-only. Owns nothing at runtime -- the gesture handling itself lives in
 * KisAndroidGestureBridge inside kritaui, so it keeps working even if this
 * plugin fails to load. All this does is put a page in Configure Krita and
 * poke the bridge when the user changes something.
 */
class TouchGestures : public KisActionPlugin
{
    Q_OBJECT
public:
    TouchGestures(QObject *parent, const QVariantList &);
    ~TouchGestures() override;

public Q_SLOTS:
    void slotSettingsChanged();
};

#endif // TOUCHGESTURES_H
