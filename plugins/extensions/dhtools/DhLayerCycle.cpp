/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "DhLayerCycle.h"
#include "DhNodeUtils.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QtMath>

#include <klocalizedstring.h>

#include <kis_image.h>
#include <kis_node.h>
#include <kis_node_manager.h>
#include <KisViewManager.h>

namespace {
// Pixels of drag per layer step. Low enough to feel immediate on a tablet,
// high enough that a shaky hand does not fly through a twenty-layer stack.
const int PIXELS_PER_STEP = 40;
}

DhLayerCycle::DhLayerCycle(KisViewManager *view, QObject *parent)
    : QObject(parent)
    , m_view(view)
{
}

DhLayerCycle::~DhLayerCycle()
{
    if (m_armed && qApp) {
        qApp->removeEventFilter(this);
    }
}

bool DhLayerCycle::isArmed() const
{
    return m_armed;
}

bool DhLayerCycle::arm()
{
    if (m_armed) return true;
    if (!m_view || !m_view->image()) return false;

    KisNodeManager *nodeManager = m_view->nodeManager();
    if (!nodeManager) return false;

    KisNodeSP active = nodeManager->activeNode();
    if (!active) return false;

    m_flat = DhNodeUtils::flattenVisualNodeOrder(m_view->image()->root());
    m_startIndex = m_flat.indexOf(active);
    if (m_startIndex < 0) {
        m_flat.clear();
        return false;
    }

    m_currentIndex = m_startIndex;
    m_haveOrigin = false;
    m_armed = true;

    // Application-wide rather than on one widget: which widget actually
    // receives pen events moves around between Krita versions and window
    // layouts, and guessing wrong means the mode silently does nothing.
    qApp->installEventFilter(this);

    m_view->showFloatingMessage(
        i18n("Layer Cycle: drag to scrub, release to commit"), QIcon(), 1500);

    return true;
}

void DhLayerCycle::disarm()
{
    if (!m_armed) return;

    m_armed = false;
    m_haveOrigin = false;
    m_flat.clear();
    m_startIndex = -1;
    m_currentIndex = -1;

    if (qApp) {
        qApp->removeEventFilter(this);
    }

    Q_EMIT disarmed();
}

void DhLayerCycle::applyDeltaFromY(int y)
{
    if (!m_haveOrigin) {
        m_originY = y;
        m_haveOrigin = true;
        return;
    }

    // Screen y grows downward and the flattened list runs top-to-bottom, so
    // dragging down and stepping down the list are the same sign. No flip.
    const int steps = (y - m_originY) / PIXELS_PER_STEP;
    const int target = qBound(0, m_startIndex + steps, m_flat.size() - 1);

    if (target == m_currentIndex) return;
    m_currentIndex = target;

    if (!m_view) return;
    KisNodeManager *nodeManager = m_view->nodeManager();
    if (!nodeManager) return;

    KisNodeSP node = m_flat.at(target);
    if (!node) return;

    nodeManager->slotNonUiActivatedNode(node);
    m_view->showFloatingMessage(node->name(), QIcon(), 700);
}

bool DhLayerCycle::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);

    if (!m_armed) return false;

    switch (event->type()) {
    case QEvent::KeyPress: {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape || keyEvent->key() == Qt::Key_Return
            || keyEvent->key() == Qt::Key_Enter) {
            disarm();
            return true;
        }
        return false;
    }

    case QEvent::MouseMove: {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        applyDeltaFromY(qRound(mouseEvent->globalPosition().y()));
#else
        applyDeltaFromY(mouseEvent->globalPos().y());
#endif
        return true;
    }

    case QEvent::TabletMove: {
        QTabletEvent *tabletEvent = static_cast<QTabletEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        applyDeltaFromY(qRound(tabletEvent->globalPosition().y()));
#else
        applyDeltaFromY(qRound(tabletEvent->globalPosF().y()));
#endif
        return true;
    }

    case QEvent::TouchBegin:
    case QEvent::TouchUpdate: {
        QTouchEvent *touchEvent = static_cast<QTouchEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        if (!touchEvent->points().isEmpty()) {
            applyDeltaFromY(qRound(touchEvent->points().first().globalPosition().y()));
        }
#else
        if (!touchEvent->touchPoints().isEmpty()) {
            applyDeltaFromY(qRound(touchEvent->touchPoints().first().screenPos().y()));
        }
#endif
        return true;
    }

    case QEvent::MouseButtonPress:
    case QEvent::TabletPress:
        // Swallow the press so it never reaches the canvas as a dab; the
        // matching release is what commits.
        return true;

    case QEvent::MouseButtonRelease:
    case QEvent::TabletRelease:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
        disarm();
        return true;

    default:
        return false;
    }
}
