/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_ANDROID_GESTURE_BRIDGE_H
#define KIS_ANDROID_GESTURE_BRIDGE_H

#include <QObject>
#include <kritaui_export.h>

/**
 * Receives multi-touch gestures that the Android Activity intercepts before
 * Qt ever sees them, and turns them into Krita canvas resource changes.
 *
 * The Java side is deliberately dumb: it recognises "three fingers are down"
 * and forwards the raw centroid delta in device-independent pixels. Every
 * decision that a user might want to change -- which axis does what, how fast,
 * whether it is inverted -- lives here, backed by kritarc, so the Krita
 * preferences dialog is the single place gestures get configured.
 *
 * Calls arrive on the Android UI thread, so postGesture() marshals onto the
 * Qt GUI thread before touching anything.
 */
class KRITAUI_EXPORT KisAndroidGestureBridge : public QObject
{
    Q_OBJECT

public:
    enum Phase {
        PhaseBegin  = 0,
        PhaseUpdate = 1,
        PhaseEnd    = 2,
        PhaseCancel = 3
    };
    Q_ENUM(Phase)

    /// What a drag along one axis is wired to.
    enum Mapping {
        NoMapping = 0,
        BrushSize,
        Opacity,
        Flow
    };
    Q_ENUM(Mapping)

    static KisAndroidGestureBridge *instance();

    /**
     * Thread-safe entry point used by the JNI shim in main.cc.
     * @return true if Krita wants the gesture, i.e. the Activity should
     *         swallow the touch events instead of handing them to Qt.
     */
    static bool postGesture(int phase, float dx, float dy);

    /// Re-read the kritarc "AndroidGestures" group. Cheap; call it freely.
    void reloadSettings();

    static QString mappingToId(Mapping mapping);
    static Mapping mappingFromId(const QString &id, Mapping fallback);

public Q_SLOTS:
    void slotHandleGesture(int phase, qreal dx, qreal dy);

private:
    KisAndroidGestureBridge();
    ~KisAndroidGestureBridge() override;

    struct Private;
    Private *const m_d;
};

#endif // KIS_ANDROID_GESTURE_BRIDGE_H
