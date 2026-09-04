/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DHRADIALLAYOUT_H
#define DHRADIALLAYOUT_H

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QVector>

/**
 * Pure layout maths for the radial menu. No painting in here on purpose: this
 * is where every "why is that button there" question gets answered, and it can
 * be reasoned about without a screen.
 *
 * Ported from the desktop plugin's layout.py so both platforms place things
 * identically.
 *
 * The core idea: buttons are DISCRETE RECTANGLES PLACED AT A RADIUS, not
 * wedges cut from a donut. In a donut, radius and button size are the same
 * number, so you can never spread items out without fattening them.
 *
 * Blocks are anchored by their INNER edge, not their centre. Anchor by centre
 * and a tall grid pushes its own middle outward by half its height, dragging
 * the whole ring with it and leaving a crater around the hub. Anchor by the
 * inner edge and every slot starts the same distance from the cursor - the
 * distance your hand actually has to travel - while a big block simply extends
 * further out into empty screen, where it costs nothing.
 */
namespace DhRadialLayout
{
    struct Block {
        QPointF centre;   ///< relative to the menu centre
        qreal theta{0.0}; ///< degrees, 0 = twelve o'clock, increasing clockwise
        qreal radius{0.0};
    };

    /// Angle per slot. Slot 0 sits at the top because that is where the eye starts.
    QVector<qreal> slotAngles(int count);

    /// Screen-space unit vector for a clockwise-from-top angle (y grows down).
    QPointF unitVector(qreal degrees);

    /// Inverse of unitVector: a screen delta back to clockwise-from-top degrees.
    qreal angleOf(qreal dx, qreal dy);

    /// Smallest absolute difference between two angles, in degrees.
    qreal angleDelta(qreal a, qreal b);

    /**
     * Half-extent of a w x h rect measured along a unit direction.
     *
     * This is the number that decides whether neighbouring slots collide -
     * not the diagonal, which is far too pessimistic and shoves a wide flat
     * grid miles out, but how much of the block sticks out along the direction
     * that separates it from its neighbour.
     */
    qreal perpendicularExtent(const QSizeF &size, const QPointF &unit);

    /**
     * Place blocks around a ring. @p radius is the INNER edge - the distance to
     * the near side of the content, not its middle. Blocks grow outward.
     *
     * @param compact pull each slot back toward the hub as far as it will go.
     *        The uniform ring is set by the single most crowded pair, so one
     *        fat grid can park every small button half a screen away. Compact
     *        keeps short trips short, at the cost of the ring no longer being
     *        a circle - which is why it is a setting, not the default.
     */
    QVector<Block> placeBlocks(const QVector<QSizeF> &sizes,
                               qreal radius,
                               qreal hubClearance,
                               qreal padding,
                               bool compact = false,
                               qreal maxRadiusMultiplier = 4.0);

    /**
     * Pack items into a grid, col_span aware.
     *
     * Occupancy is tracked at tenth-of-a-column resolution, so a span of 0.5
     * packs two buttons into one cell with no special-casing. Items flow
     * left-to-right, top-to-bottom and take the first slot they fit in.
     *
     * @param spans   col_span per item
     * @param columns column count
     * @return cells in local coordinates, origin at the grid's top-left
     */
    QVector<QRectF> gridMetrics(const QVector<qreal> &spans, int columns,
                                qreal cellWidth, qreal cellHeight, qreal spacing,
                                QSizeF *totalSize = nullptr);

    /**
     * Distance from the parent at which @p count satellites of @p size fit on
     * an arc of @p spreadDeg without touching.
     *
     * The gap between neighbours on an arc is a chord, so solve the chord for
     * the radius rather than guessing: a fan that overlaps itself is worse
     * than no fan.
     */
    qreal fanRadius(int count, qreal size, qreal spreadDeg,
                    qreal baseRadius, qreal padding);

    /// Angles for @p count satellites centred on @p baseTheta.
    QVector<qreal> fanAngles(int count, qreal baseTheta, qreal spreadDeg);

    /// Half-width / half-height needed to hold every placed block.
    QSizeF boundingExtent(const QVector<Block> &blocks, const QVector<QSizeF> &sizes);

    // ---------------------------------------------------------------------
    // Drawers & dropdowns: children revealed in a strip (or grid) off one
    // side of the parent, rather than fanned on an arc or replacing the ring.
    // ---------------------------------------------------------------------

    /// The axis a slot at @p degrees points along, outward from the ring.
    /// Snapped to one of four rather than following the ray exactly - a strip
    /// of rectangles marching off on a diagonal reads as a mistake even
    /// though it is the geometrically honest answer.
    QString snapDirection(qreal degrees);

    /// Lay @p sizes out as a single strip. Local coordinates, origin at the
    /// block's own top-left. @p reverse mirrors the strip so the first child
    /// ends up nearest the parent button - the panel grows away from its
    /// button but is numbered toward it.
    QVector<QRectF> lineMetrics(const QVector<QSizeF> &sizes, bool vertical,
                               qreal spacing, bool reverse, QSizeF *totalSize = nullptr);

    /// Top-left of a @p bw x @p bh panel opening off @p parentRect in
    /// @p direction (RIGHT/LEFT/UP/DOWN), aligned across that direction by
    /// @p align (START/CENTER/END). Both in the same coordinate space as
    /// @p parentRect.
    QPointF placePanel(const QRectF &parentRect, qreal bw, qreal bh,
                       const QString &direction, const QString &align, qreal gap);
}

#endif // DHRADIALLAYOUT_H
