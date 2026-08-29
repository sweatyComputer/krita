/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DHRADIALMENU_H
#define DHRADIALMENU_H

#include <QList>
#include <QPoint>
#include <QWidget>

#include "DhRadialConfig.h"

/**
 * A touch-first pie menu over the canvas.
 *
 * Reads the same JSON layout the Python plugin used, so the wedges and
 * submenus are data, not code -- edit dh_radial_menu.json and restart. Wedges
 * fire Krita actions by name through the main window's action collection,
 * which means anything in Krita's shortcut list is fair game, including the
 * actions this plugin adds.
 */
class DhRadialMenu : public QWidget
{
    Q_OBJECT
public:
    explicit DhRadialMenu(QWidget *parent = nullptr);
    ~DhRadialMenu() override;

    /// Reload the layout from disk. Cheap enough to call on every popup.
    void reloadConfig();

    /// Show the menu centred on @p globalPos, clamped to stay on screen.
    void popupAt(const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QList<DhRadialItemSP> currentItems() const;
    int indexAt(const QPointF &pos) const;
    bool isInCentre(const QPointF &pos) const;
    QColor wedgeColour(const DhRadialItemSP &item, int index) const;
    QString centreLabel() const;

    void activate(int index);
    void goBack();
    void triggerAction(const QString &actionId);

    QList<DhRadialItemSP> m_root;
    QList<QList<DhRadialItemSP>> m_stack;   ///< drill-down breadcrumb
    QStringList m_stackLabels;

    QPointF m_centre;
    qreal m_outerRadius{0.0};
    qreal m_innerRadius{0.0};
    int m_hoverIndex{-1};
    bool m_hoverCentre{false};
};

#endif // DHRADIALMENU_H
