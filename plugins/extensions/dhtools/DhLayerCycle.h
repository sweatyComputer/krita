/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DHLAYERCYCLE_H
#define DHLAYERCYCLE_H

#include <QObject>
#include <QPointer>
#include <kis_types.h>

class KisViewManager;

/**
 * A toggle-mode "tool" for scrubbing the active layer with a drag.
 *
 * Krita's real canvas tools are a C++ extension point that expects a full
 * KisTool implementation and a slot in the toolbox; this is deliberately not
 * that. Arm the mode, drag anywhere, and the active node walks the flattened
 * stack live. Releasing commits wherever it landed -- it does not snap back to
 * where it started. Escape does the same. While armed the input is eaten
 * whole, so nothing paints on the way past.
 */
class DhLayerCycle : public QObject
{
    Q_OBJECT
public:
    explicit DhLayerCycle(KisViewManager *view, QObject *parent = nullptr);
    ~DhLayerCycle() override;

    bool isArmed() const;

    /// @return true if the mode actually armed (needs a document and a layer).
    bool arm();
    void disarm();

    bool eventFilter(QObject *watched, QEvent *event) override;

Q_SIGNALS:
    /// Emitted whenever the mode turns itself off, so the action can uncheck.
    void disarmed();

private:
    void applyDeltaFromY(int y);

    QPointer<KisViewManager> m_view;
    bool m_armed{false};
    bool m_haveOrigin{false};
    int m_originY{0};
    int m_startIndex{-1};
    int m_currentIndex{-1};
    KisNodeList m_flat;
};

#endif // DHLAYERCYCLE_H
