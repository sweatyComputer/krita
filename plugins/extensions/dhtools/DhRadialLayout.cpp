/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "DhRadialLayout.h"

#include <QtMath>
#include <QPair>
#include <QSet>
#include <algorithm>

namespace {

struct Rect { qreal x, y, w, h; };

bool rectsOverlap(const Rect &a, const Rect &b, qreal pad)
{
    return (qAbs(a.x - b.x) * 2.0 < (a.w + b.w + pad * 2.0))
        && (qAbs(a.y - b.y) * 2.0 < (a.h + b.h + pad * 2.0));
}

QVector<QPointF> centresFor(const QVector<QSizeF> &sizes,
                            const QVector<QPointF> &units,
                            qreal inner)
{
    QVector<QPointF> out;
    out.reserve(sizes.size());
    for (int i = 0; i < sizes.size(); ++i) {
        const qreal along = DhRadialLayout::perpendicularExtent(sizes.at(i), units.at(i));
        const qreal r = inner + along;
        out.append(QPointF(units.at(i).x() * r, units.at(i).y() * r));
    }
    return out;
}

bool anyOverlap(const QVector<QSizeF> &sizes,
                const QVector<QPointF> &centres,
                qreal padding)
{
    for (int i = 0; i < sizes.size(); ++i) {
        for (int j = i + 1; j < sizes.size(); ++j) {
            const Rect a{centres.at(i).x(), centres.at(i).y(), sizes.at(i).width(), sizes.at(i).height()};
            const Rect b{centres.at(j).x(), centres.at(j).y(), sizes.at(j).width(), sizes.at(j).height()};
            if (rectsOverlap(a, b, padding / 2.0)) return true;
        }
    }
    return false;
}

bool overlapsOthers(const QVector<QSizeF> &sizes,
                    const QVector<QPointF> &centres,
                    qreal padding, int index)
{
    const Rect a{centres.at(index).x(), centres.at(index).y(),
                 sizes.at(index).width(), sizes.at(index).height()};
    for (int j = 0; j < sizes.size(); ++j) {
        if (j == index) continue;
        const Rect b{centres.at(j).x(), centres.at(j).y(),
                     sizes.at(j).width(), sizes.at(j).height()};
        if (rectsOverlap(a, b, padding / 2.0)) return true;
    }
    return false;
}

/**
 * Smallest inner radius at which no two blocks overlap.
 *
 * Solved by walking outward rather than with a closed form: with inner-edge
 * anchoring every block sits at its own centre radius (inner plus however far
 * it sticks out along its ray), and the neat chord formula stops being exact.
 * There are at most a dozen slots and this runs once per open, so exactness is
 * worth more than cleverness.
 */
qreal solveInnerRadius(const QVector<QSizeF> &sizes,
                       const QVector<QPointF> &units,
                       qreal start, qreal padding, qreal ceiling, qreal step = 5.0)
{
    qreal inner = start;
    for (int guard = 0; guard < 240; ++guard) {
        if (!anyOverlap(sizes, centresFor(sizes, units, inner), padding)) return inner;
        inner += step;
        if (inner >= ceiling) return ceiling;
    }
    return inner;
}

QVector<qreal> compactRadii(const QVector<QSizeF> &sizes,
                            const QVector<QPointF> &units,
                            qreal inner, qreal base, qreal padding, qreal step = 4.0)
{
    QVector<qreal> radii(sizes.size(), inner);

    auto centres = [&]() {
        QVector<QPointF> out;
        out.reserve(sizes.size());
        for (int i = 0; i < sizes.size(); ++i) {
            const qreal along = DhRadialLayout::perpendicularExtent(sizes.at(i), units.at(i));
            const qreal r = radii.at(i) + along;
            out.append(QPointF(units.at(i).x() * r, units.at(i).y() * r));
        }
        return out;
    };

    // Smallest blocks first: they are the ones that gain most from moving in,
    // and letting them settle first leaves the big ones somewhere sensible.
    QVector<int> order;
    order.reserve(sizes.size());
    for (int i = 0; i < sizes.size(); ++i) order.append(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return sizes.at(a).width() * sizes.at(a).height()
             < sizes.at(b).width() * sizes.at(b).height();
    });

    for (int pass = 0; pass < 2; ++pass) {
        for (int i : order) {
            while (radii[i] - step >= base) {
                const qreal keep = radii[i];
                radii[i] -= step;
                if (overlapsOthers(sizes, centres(), padding, i)) {
                    radii[i] = keep;
                    break;
                }
            }
        }
    }
    return radii;
}

} // namespace

QVector<qreal> DhRadialLayout::slotAngles(int count)
{
    QVector<qreal> out;
    if (count <= 0) return out;
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        out.append(i * 360.0 / count);
    }
    return out;
}

QPointF DhRadialLayout::unitVector(qreal degrees)
{
    const qreal rad = qDegreesToRadians(degrees);
    return QPointF(qSin(rad), -qCos(rad));
}

qreal DhRadialLayout::angleOf(qreal dx, qreal dy)
{
    qreal deg = qRadiansToDegrees(qAtan2(dx, -dy));
    while (deg < 0.0) deg += 360.0;
    while (deg >= 360.0) deg -= 360.0;
    return deg;
}

qreal DhRadialLayout::angleDelta(qreal a, qreal b)
{
    qreal d = qAbs(a - b);
    while (d >= 360.0) d -= 360.0;
    return qMin(d, 360.0 - d);
}

qreal DhRadialLayout::perpendicularExtent(const QSizeF &size, const QPointF &unit)
{
    return qAbs(unit.x()) * size.width() / 2.0 + qAbs(unit.y()) * size.height() / 2.0;
}

QVector<DhRadialLayout::Block> DhRadialLayout::placeBlocks(const QVector<QSizeF> &sizes,
                                                           qreal radius,
                                                           qreal hubClearance,
                                                           qreal padding,
                                                           bool compact,
                                                           qreal maxRadiusMultiplier)
{
    QVector<Block> out;
    const int n = sizes.size();
    if (n == 0) return out;

    const QVector<qreal> thetas = slotAngles(n);
    QVector<QPointF> units;
    units.reserve(n);
    for (qreal theta : thetas) units.append(unitVector(theta));

    const qreal base = qMax(radius, hubClearance);
    qreal inner = base;
    if (n > 1) {
        inner = solveInnerRadius(sizes, units, base, padding,
                                 qMax(base, radius * maxRadiusMultiplier));
    }

    QVector<qreal> radii;
    if (compact && n > 1 && inner > base) {
        radii = compactRadii(sizes, units, inner, base, padding);
    } else {
        radii = QVector<qreal>(n, inner);
    }

    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        const qreal along = perpendicularExtent(sizes.at(i), units.at(i));
        const qreal r = radii.at(i) + along;
        Block block;
        block.centre = QPointF(units.at(i).x() * r, units.at(i).y() * r);
        block.theta = thetas.at(i);
        block.radius = r;
        out.append(block);
    }
    return out;
}

QSizeF DhRadialLayout::boundingExtent(const QVector<Block> &blocks, const QVector<QSizeF> &sizes)
{
    qreal ex = 0.0, ey = 0.0;
    for (int i = 0; i < blocks.size() && i < sizes.size(); ++i) {
        ex = qMax(ex, qAbs(blocks.at(i).centre.x()) + sizes.at(i).width() / 2.0);
        ey = qMax(ey, qAbs(blocks.at(i).centre.y()) + sizes.at(i).height() / 2.0);
    }
    return QSizeF(ex, ey);
}

// ---------------------------------------------------------------------------
// Grid packing
// ---------------------------------------------------------------------------

QVector<QRectF> DhRadialLayout::gridMetrics(const QVector<qreal> &spans, int columns,
                                            qreal cellWidth, qreal cellHeight, qreal spacing,
                                            QSizeF *totalSize)
{
    QVector<QRectF> cells;
    columns = qMax(1, columns);

    // Tenth-of-a-column occupancy: a span of 0.5 then packs two to a cell
    // without the packer needing to know that halves are a special case.
    const int tenths = columns * 10;
    QSet<QPair<int, int>> occupied;

    auto isFree = [&](int row, int col, int span) {
        for (int t = col; t < col + span; ++t) {
            if (occupied.contains(qMakePair(row, t))) return false;
        }
        return true;
    };

    int row = 0;
    int col = 0;
    int rows = 0;

    for (qreal rawSpan : spans) {
        const qreal span = qBound(0.1, rawSpan > 0.0 ? rawSpan : 1.0, qreal(columns));
        const int tspan = qMax(1, qRound(span * 10.0));

        while (true) {
            if (col + tspan > tenths) { col = 0; ++row; continue; }
            if (isFree(row, col, tspan)) break;
            ++col;
            if (col >= tenths) { col = 0; ++row; }
        }

        for (int t = col; t < col + tspan; ++t) occupied.insert(qMakePair(row, t));

        const qreal stepX = cellWidth + spacing;
        const qreal stepY = cellHeight + spacing;
        const qreal x = (col / 10.0) * stepX;
        const qreal w = qMax(1.0, span * stepX - spacing);
        cells.append(QRectF(x, row * stepY, w, cellHeight));

        col += tspan;
        if (col >= tenths) { col = 0; ++row; }
        rows = qMax(rows, row + 1);
    }

    if (totalSize) {
        const qreal stepX = cellWidth + spacing;
        const qreal stepY = cellHeight + spacing;
        *totalSize = QSizeF(columns * stepX - spacing, rows > 0 ? rows * stepY - spacing : 0.0);
    }
    return cells;
}

// ---------------------------------------------------------------------------
// Sprout fans
// ---------------------------------------------------------------------------

qreal DhRadialLayout::fanRadius(int count, qreal size, qreal spreadDeg,
                                qreal baseRadius, qreal padding)
{
    if (count < 2) return baseRadius;

    const qreal step = qDegreesToRadians(spreadDeg / (count - 1));
    const qreal chord = 2.0 * qSin(step / 2.0);
    if (chord <= 1e-6) return baseRadius;

    return qMax(baseRadius, (size + padding) / chord);
}

QVector<qreal> DhRadialLayout::fanAngles(int count, qreal baseTheta, qreal spreadDeg)
{
    QVector<qreal> out;
    if (count <= 0) return out;
    if (count == 1) { out.append(baseTheta); return out; }

    const qreal start = baseTheta - spreadDeg / 2.0;
    const qreal step = spreadDeg / (count - 1);
    out.reserve(count);
    for (int i = 0; i < count; ++i) out.append(start + i * step);
    return out;
}

// ---------------------------------------------------------------------------
// Drawers & dropdowns
// ---------------------------------------------------------------------------

QString DhRadialLayout::snapDirection(qreal degrees)
{
    const QPointF u = unitVector(degrees);
    if (qAbs(u.x()) >= qAbs(u.y())) {
        return u.x() >= 0 ? QStringLiteral("RIGHT") : QStringLiteral("LEFT");
    }
    return u.y() >= 0 ? QStringLiteral("DOWN") : QStringLiteral("UP");
}

QVector<QRectF> DhRadialLayout::lineMetrics(const QVector<QSizeF> &sizes, bool vertical,
                                            qreal spacing, bool reverse, QSizeF *totalSize)
{
    QVector<QRectF> cells;
    cells.reserve(sizes.size());
    if (sizes.isEmpty()) {
        if (totalSize) *totalSize = QSizeF(0, 0);
        return cells;
    }

    qreal width = 0.0, height = 0.0;

    if (vertical) {
        for (const QSizeF &s : sizes) width = qMax(width, s.width());
        qreal y = 0.0;
        for (const QSizeF &s : sizes) {
            cells.append(QRectF(0.0, y, s.width(), s.height()));
            y += s.height() + spacing;
        }
        height = y - spacing;
    } else {
        for (const QSizeF &s : sizes) height = qMax(height, s.height());
        qreal x = 0.0;
        for (const QSizeF &s : sizes) {
            cells.append(QRectF(x, 0.0, s.width(), s.height()));
            x += s.width() + spacing;
        }
        width = x - spacing;
    }

    if (reverse) {
        for (QRectF &cell : cells) {
            if (vertical) {
                cell.moveTop(height - cell.top() - cell.height());
            } else {
                cell.moveLeft(width - cell.left() - cell.width());
            }
        }
    }

    if (totalSize) *totalSize = QSizeF(width, height);
    return cells;
}

QPointF DhRadialLayout::placePanel(const QRectF &parentRect, qreal bw, qreal bh,
                                   const QString &direction, const QString &align, qreal gap)
{
    qreal x = 0.0, y = 0.0;

    if (direction == QLatin1String("RIGHT") || direction == QLatin1String("LEFT")) {
        x = (direction == QLatin1String("RIGHT"))
            ? parentRect.right() + gap
            : parentRect.left() - gap - bw;

        if (align == QLatin1String("START")) {
            y = parentRect.top();
        } else if (align == QLatin1String("END")) {
            y = parentRect.bottom() - bh;
        } else {
            y = parentRect.center().y() - bh / 2.0;
        }
    } else {
        y = (direction == QLatin1String("DOWN"))
            ? parentRect.bottom() + gap
            : parentRect.top() - gap - bh;

        if (align == QLatin1String("START")) {
            x = parentRect.left();
        } else if (align == QLatin1String("END")) {
            x = parentRect.right() - bw;
        } else {
            x = parentRect.center().x() - bw / 2.0;
        }
    }

    return QPointF(x, y);
}
