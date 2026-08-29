/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisAndroidGestureBridge.h"

#include <QAtomicInt>
#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>
#include <QtMath>

#include <klocalizedstring.h>
#include <kconfiggroup.h>
#include <ksharedconfig.h>

#include <KisPart.h>
#include <KisMainWindow.h>
#include <KisViewManager.h>
#include <kis_canvas_resource_provider.h>
#include <kis_canvas2.h>
#include <kis_floating_message.h>
#include <kis_debug.h>

namespace {
const char *CONFIG_GROUP = "AndroidGestures";

// Below this much travel we have not decided which axis the user meant, so we
// sit on our hands rather than nudging the brush on a sloppy three-finger tap.
const qreal DEFAULT_DEAD_ZONE = 12.0;
}

struct KisAndroidGestureBridge::Private
{
    // Read from the Android UI thread in postGesture(), written on the GUI
    // thread in reloadSettings(). QAtomicInt keeps that honest without a lock.
    QAtomicInt enabled{1};

    Mapping verticalMapping{BrushSize};
    Mapping horizontalMapping{Opacity};

    bool invertVertical{false};
    bool invertHorizontal{false};
    bool lockToAxis{true};
    bool showFeedback{true};

    // Brush size is in px, so this is "brush px per finger px".
    qreal sizeSensitivity{0.5};
    // Opacity/flow are 0..1, so this is "units per finger px":
    // 0.0025 means a 400px drag walks the full range.
    qreal opacitySensitivity{0.0025};
    qreal deadZone{DEFAULT_DEAD_ZONE};

    // Per-gesture state.
    bool active{false};
    bool axisResolved{false};
    bool verticalGesture{false};
    qreal startSize{0.0};
    qreal startOpacity{0.0};
    qreal startFlow{0.0};

    KisViewManager *viewManager() const
    {
        if (!KisPart::exists()) return nullptr;
        KisMainWindow *window = KisPart::instance()->currentMainwindow();
        return window ? window->viewManager() : nullptr;
    }
};

KisAndroidGestureBridge::KisAndroidGestureBridge()
    : QObject(nullptr)
    , m_d(new Private)
{
    reloadSettings();
}

KisAndroidGestureBridge::~KisAndroidGestureBridge()
{
    delete m_d;
}

KisAndroidGestureBridge *KisAndroidGestureBridge::instance()
{
    // Function-local static so the private constructor stays private, and so
    // the object exists before the first JNI call can possibly arrive.
    static KisAndroidGestureBridge bridge;

    // JNI calls come in on the Android UI thread. If that thread is what
    // happened to create us, our queued invocations would be posted to an
    // event loop that does not exist. Pin us to the GUI thread instead.
    if (qApp && bridge.thread() != qApp->thread()) {
        bridge.moveToThread(qApp->thread());
    }

    return &bridge;
}

QString KisAndroidGestureBridge::mappingToId(Mapping mapping)
{
    switch (mapping) {
    case BrushSize: return QStringLiteral("brush_size");
    case Opacity:   return QStringLiteral("opacity");
    case Flow:      return QStringLiteral("flow");
    case NoMapping:
    default:        return QStringLiteral("none");
    }
}

KisAndroidGestureBridge::Mapping KisAndroidGestureBridge::mappingFromId(const QString &id, Mapping fallback)
{
    if (id == QLatin1String("brush_size")) return BrushSize;
    if (id == QLatin1String("opacity"))    return Opacity;
    if (id == QLatin1String("flow"))       return Flow;
    if (id == QLatin1String("none"))       return NoMapping;
    return fallback;
}

void KisAndroidGestureBridge::reloadSettings()
{
    KConfigGroup cfg = KSharedConfig::openConfig()->group(CONFIG_GROUP);

    m_d->enabled.storeRelaxed(cfg.readEntry("enabled", true) ? 1 : 0);

    m_d->verticalMapping =
        mappingFromId(cfg.readEntry("verticalAction", mappingToId(BrushSize)), BrushSize);
    m_d->horizontalMapping =
        mappingFromId(cfg.readEntry("horizontalAction", mappingToId(Opacity)), Opacity);

    m_d->invertVertical = cfg.readEntry("invertVertical", false);
    m_d->invertHorizontal = cfg.readEntry("invertHorizontal", false);
    m_d->lockToAxis = cfg.readEntry("lockToAxis", true);
    m_d->showFeedback = cfg.readEntry("showFeedback", false);

    m_d->sizeSensitivity = cfg.readEntry("sizeSensitivity", 0.5);
    m_d->opacitySensitivity = cfg.readEntry("opacitySensitivity", 0.0025);
    m_d->deadZone = cfg.readEntry("deadZone", DEFAULT_DEAD_ZONE);
}

bool KisAndroidGestureBridge::postGesture(int phase, float dx, float dy)
{
    KisAndroidGestureBridge *bridge = instance();
    if (!bridge) return false;

    if (!bridge->m_d->enabled.loadRelaxed()) {
        return false;
    }

    // Nothing to drive yet (splash screen, no document): let Qt have the touch.
    if (!KisPart::exists() || !KisPart::instance()->currentMainwindow()) {
        return false;
    }

    QMetaObject::invokeMethod(bridge, "slotHandleGesture", Qt::QueuedConnection,
                              Q_ARG(int, phase),
                              Q_ARG(qreal, static_cast<qreal>(dx)),
                              Q_ARG(qreal, static_cast<qreal>(dy)));
    return true;
}

void KisAndroidGestureBridge::slotHandleGesture(int phase, qreal dx, qreal dy)
{
    KisViewManager *view = m_d->viewManager();
    if (!view) return;

    KisCanvasResourceProvider *provider = view->canvasResourceProvider();
    if (!provider) return;

    switch (phase) {
    case PhaseBegin:
        m_d->active = true;
        m_d->axisResolved = false;
        m_d->verticalGesture = false;
        m_d->startSize = provider->size();
        m_d->startOpacity = provider->opacity();
        m_d->startFlow = provider->flow();
        return;

    case PhaseEnd:
    case PhaseCancel:
        m_d->active = false;
        m_d->axisResolved = false;
        return;

    case PhaseUpdate:
        break;

    default:
        return;
    }

    if (!m_d->active) return;

    if (!m_d->axisResolved) {
        const qreal travel = qMax(qAbs(dx), qAbs(dy));
        if (travel < m_d->deadZone) {
            return;
        }
        m_d->verticalGesture = qAbs(dy) >= qAbs(dx);
        m_d->axisResolved = true;
    }

    // With axis lock on we commit to the axis picked at the start of the drag,
    // so a wobbly finger can't flip you from size to opacity mid-stroke. With
    // it off, whichever axis currently dominates wins, re-evaluated every move.
    const bool vertical = m_d->lockToAxis
        ? m_d->verticalGesture
        : (qAbs(dy) >= qAbs(dx));

    const Mapping mapping = vertical ? m_d->verticalMapping : m_d->horizontalMapping;
    if (mapping == NoMapping) return;

    const bool invert = vertical ? m_d->invertVertical : m_d->invertHorizontal;

    // Screen y grows downward; dragging up should mean "more", so flip dy.
    qreal delta = vertical ? -dy : dx;
    if (invert) delta = -delta;

    QString message;

    switch (mapping) {
    case BrushSize: {
        const qreal size = qBound(1.0, m_d->startSize + delta * m_d->sizeSensitivity, 1000.0);
        provider->setSize(size);
        message = i18n("Brush Size: %1 px", QString::number(size, 'f', 1));
        break;
    }
    case Opacity: {
        const qreal opacity = qBound(0.0, m_d->startOpacity + delta * m_d->opacitySensitivity, 1.0);
        provider->setOpacity(opacity);
        message = i18n("Opacity: %1%", QString::number(opacity * 100.0, 'f', 0));
        break;
    }
    case Flow: {
        const qreal flow = qBound(0.0, m_d->startFlow + delta * m_d->opacitySensitivity, 1.0);
        provider->setFlow(flow);
        message = i18n("Flow: %1%", QString::number(flow * 100.0, 'f', 0));
        break;
    }
    case NoMapping:
    default:
        return;
    }

    if (m_d->showFeedback && !message.isEmpty()) {
        view->showFloatingMessage(message, QIcon(), 500,
                                  KisFloatingMessage::High,
                                  Qt::AlignCenter);
    }
}
