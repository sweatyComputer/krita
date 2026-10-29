/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DHRADIALMENU_H
#define DHRADIALMENU_H

#include <QList>
#include <QPoint>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QIcon>
#include <QPainterPath>
#include <QRectF>
#include <QTimer>
#include <QElapsedTimer>
#include <QWidget>

#include "DhRadialConfig.h"
#include "DhRadialDesign.h"
#include "DhRadialLayout.h"

/**
 * A touch-first radial menu over the canvas.
 *
 * Reads the same config.json as the desktop plugin, design block and all, so a
 * menu authored there works here unchanged. Two looks, chosen by the config's
 * slice_style:
 *
 *   BUTTONS  discrete buttons placed at a radius. Radius and button size are
 *            separate numbers, so you can spread items out without fattening
 *            them. This is the desktop plugin's default and the better one.
 *   RING     the older annular wedges, kept because the setting exists.
 *
 * Wedges fire Krita actions by name through the main window's action
 * collection, so anything in Krita's shortcut list is fair game.
 */
class DhRadialMenu : public QWidget
{
    Q_OBJECT
public:
    explicit DhRadialMenu(QWidget *parent = nullptr);
    ~DhRadialMenu() override;

    /// Reload layout and design from disk. Cheap enough to call on every popup.
    void reloadConfig();

    /// Show the menu centred on @p globalPos, clamped to stay on screen.
    void popupAt(const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private Q_SLOTS:
    void slotPanelTimeout();

private:
    /**
     * One hit target and draw unit.
     *
     * A slot is not always one button: a GRID slot draws its children inline as
     * a packed grid, so it contributes one cell per child. Flattening
     * everything to cells means painting and hit-testing share a single list
     * and can never disagree about where a button is.
     */
    struct Cell {
        QRectF rect;
        DhRadialItemSP item;
        int slot{-1};      ///< index into the current level
        int child{-1};     ///< -1 = the slot itself, else index into its children
        bool circle{false};
    };

    /// A drawer/dropdown's children, laid out but not yet turned into Cells -
    /// shared between contentRect() (which only needs the geometry, to reserve
    /// room) and layoutStripPanel() (which turns it into hit-testable Cells).
    struct PanelGeometry {
        QString direction{QStringLiteral("DOWN")};
        QVector<QRectF> cells;   ///< relative to the menu centre, parent order
    };
    PanelGeometry computeStripPanel(int slot, const DhRadialItemSP &parent) const;

    QList<DhRadialItemSP> currentItems() const;

    /// Measure every item and place it, then size the widget to fit.
    void rebuildLayout();
    QSizeF sizeForItem(const DhRadialItemSP &item) const;
    bool isCircle(const DhRadialItemSP &item) const;

    int cellAt(const QPointF &pos) const;
    int panelCellAt(const QPointF &pos) const;
    bool isInCentre(const QPointF &pos) const;

    /**
     * Bounding box of everything that will be drawn, relative to the centre.
     *
     * Asymmetric on purpose. Sizing the widget from a symmetric half-extent
     * and parking the centre in its middle is what let content fall off the
     * edge: near a screen border the frame gets clamped, and anything the
     * box did not account for - a fan, a tall grid - had nowhere to go.
     */
    QRectF contentRect() const;

    /// Shrink uiScale until the content fits @p available, then re-lay out.
    void fitToScreen(const QSize &available);

    void openPanel(int slot);
    void closePanel();
    bool isPanelSlot(const DhRadialItemSP &item) const;
    /// isPanelSlot(), and not crowded past the point its layout can handle.
    bool isPanelReady(const DhRadialItemSP &item) const;

    /// FAN (sprout), LINE or GRID (drawer/dropdown flow) - decides how
    /// paintPanel() draws the backing plate, if any.
    void layoutFanPanel(int slot, const DhRadialItemSP &parent);
    void layoutStripPanel(int slot, const DhRadialItemSP &parent);

    void buildCells();
    void paintCell(QPainter &painter, const Cell &cell, bool hovered);
    void paintGlow(QPainter &painter, const QPainterPath &path);
    void paintPanel(QPainter &painter);
    void paintButtons(QPainter &painter);
    void paintRing(QPainter &painter);
    void paintHub(QPainter &painter);
    QIcon iconFor(const DhRadialItemSP &item) const;

    QColor restingFill(const DhRadialItemSP &item, int index,
                       const DhRadialItemSP &parent = DhRadialItemSP()) const;

    /**
     * The colour an item contributes to its own fill.
     *
     * A child drawn inline (grid, drawer, dropdown, sprout) with no colour of
     * its own inherits its parent's, so the group reads as one block. Only
     * when neither has a colour do we fall back to the palette - otherwise a
     * parent with an explicit colour would sprout children tinted by whatever
     * the palette happens to hold at the parent's slot index, which is a
     * colour belonging to nothing on screen.
     */
    QColor tintFor(const DhRadialItemSP &item, int index,
                   const DhRadialItemSP &parent) const;

    QColor paletteColour(int index) const;
    QString centreLabel() const;

    void activate(int index);
    void activateItem(const DhRadialItemSP &item);
    void goBack();
    void triggerAction(const QString &actionId);

    DhRadialDesign m_design;

    QList<DhRadialItemSP> m_root;
    QList<QList<DhRadialItemSP>> m_stack;   ///< drill-down breadcrumb
    QStringList m_stackLabels;

    QVector<DhRadialLayout::Block> m_blocks;
    QVector<QSizeF> m_sizes;
    QVector<Cell> m_cells;

    // Children of the currently open drawer/dropdown/sprout, rebuilt each
    // time one opens. m_panelDir says how they were laid out, which decides
    // how paintPanel() draws behind them: "FAN" gets optional hairlines,
    // "LINE"/"GRID" get a backing plate so they read as one block.
    QVector<Cell> m_panelCells;
    int m_panelSlot{-1};
    int m_pendingPanel{-1};
    QTimer m_panelTimer;
    QString m_panelDir{QStringLiteral("FAN")};

    QPointF m_centre;
    qreal m_outerRadius{0.0};   ///< RING style only
    qreal m_innerRadius{0.0};   ///< RING style only
    qreal m_hubRadius{0.0};
    int m_hoverCell{-1};      ///< index into m_cells
    int m_hoverPanel{-1};    ///< index into m_panelCells
    bool m_hoverCentre{false};

    // Open animation: a quick scale+fade in from the popup point, skipped
    // entirely when design.openAnimation is off.
    QTimer m_animTimer;
    QElapsedTimer m_animClock;
    qreal m_animT{1.0};
};

#endif // DHRADIALMENU_H
