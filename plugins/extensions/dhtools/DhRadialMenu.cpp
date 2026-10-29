/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "DhRadialMenu.h"

#include "DhRadialIcons.h"

#include <QAction>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QtMath>

#include <klocalizedstring.h>
#include <kactioncollection.h>
#include <KisPart.h>
#include <KisMainWindow.h>
#include <kis_debug.h>

namespace {

// Used when an item does not name its own colour.
const char *DEFAULT_PALETTE[] = {
    "#4A8FD4", "#1ABC9C", "#9B59B6", "#E67E22",
    "#2ECC71", "#F1C40F", "#E74C3C", "#7F8C8D"
};
const int DEFAULT_PALETTE_SIZE = 8;

// Breathing room around the placed blocks, for shadows and the outer stroke.
const int WIDGET_MARGIN = 16;

inline QPointF eventPos(const QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
}

} // namespace

DhRadialMenu::DhRadialMenu(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    // Without this the compositor never clears our surface between shows, so a
    // popup at a new position paints on top of the ghost of the old one.
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    m_panelTimer.setSingleShot(true);
    connect(&m_panelTimer, SIGNAL(timeout()), this, SLOT(slotPanelTimeout()));

    m_animTimer.setInterval(16);
    connect(&m_animTimer, &QTimer::timeout, this, [this]() {
        const qreal elapsed = m_animClock.isValid() ? m_animClock.elapsed() : m_design.animMs;
        m_animT = qBound(0.0, elapsed / qMax(1, m_design.animMs), 1.0);
        if (m_animT >= 1.0) m_animTimer.stop();
        update();
    });

    reloadConfig();
}

DhRadialMenu::~DhRadialMenu()
{
}

void DhRadialMenu::reloadConfig()
{
    m_design = DhRadialDesign();
    m_root = DhRadialConfig::load(&m_design);
    m_stack.clear();
    m_stackLabels.clear();
}

QList<DhRadialItemSP> DhRadialMenu::currentItems() const
{
    return m_stack.isEmpty() ? m_root : m_stack.last();
}

QColor DhRadialMenu::paletteColour(int index) const
{
    return QColor(QString::fromLatin1(DEFAULT_PALETTE[index % DEFAULT_PALETTE_SIZE]));
}

QString DhRadialMenu::centreLabel() const
{
    return m_stack.isEmpty() ? i18n("Close") : i18n("Back");
}

bool DhRadialMenu::isCircle(const DhRadialItemSP &item) const
{
    if (item && !item->shape.isEmpty()) {
        return item->shape == QLatin1String("CIRCLE");
    }
    return m_design.buttonShape == DhRadialDesign::Circle;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

bool DhRadialMenu::isPanelSlot(const DhRadialItemSP &item) const
{
    return item && item->isSubmenu()
        && (item->slotLayout == DhRadialItem::Sprout
            || item->slotLayout == DhRadialItem::Drawer
            || item->slotLayout == DhRadialItem::Dropdown);
}

QIcon DhRadialMenu::iconFor(const DhRadialItemSP &item) const
{
    if (!item) return QIcon();
    return DhRadialIcons::resolve(item->icon, item->action, m_design.autoIcons);
}

QSizeF DhRadialMenu::sizeForItem(const DhRadialItemSP &item) const
{
    const qreal scale = m_design.uiScale;

    // A GRID slot is not a button: it is its whole grid, laid out inline. That
    // is the space saving - a grid cell is deliberately smaller than a lone
    // button, so eight children cost far less room than eight buttons would.
    if (item && item->isSubmenu() && item->slotLayout == DhRadialItem::Grid) {
        QVector<qreal> spans;
        spans.reserve(item->children.size());
        for (const DhRadialItemSP &child : item->children) {
            spans.append(child ? child->colSpan : 1.0);
        }

        const int columns = item->columns > 0 ? item->columns : m_design.gridColumns;
        QSizeF total;
        DhRadialLayout::gridMetrics(spans, columns,
                                    (item->cellWidth > 0 ? item->cellWidth : m_design.gridCellWidth) * scale,
                                    (item->cellHeight > 0 ? item->cellHeight : m_design.gridCellHeight) * scale,
                                    m_design.itemSpacing * scale,
                                    &total);

        const qreal header = m_design.gridHeader ? (m_design.fontSize * scale * 1.9) : 0.0;
        return QSizeF(total.width(), total.height() + header);
    }

    if (isCircle(item)) {
        const qreal diameter = m_design.circleDiameter * scale;
        // A captioned circle needs room underneath for its label, which counts
        // toward collision or the text of one slot lands on the next.
        const qreal caption = m_design.circleCaption
            ? (m_design.fontSize * scale * 1.6)
            : 0.0;
        return QSizeF(diameter, diameter + caption);
    }

    qreal width = m_design.buttonWidth * scale;

    if (m_design.autoWidth && item) {
        // Grow a lone button to fit its own label rather than eliding it. The
        // fixed width is a minimum, not a maximum.
        QFont font(m_design.fontFamily, qMax(1, qRound(m_design.fontSize * scale)));
        const QFontMetricsF metrics(font);
        qreal needed = metrics.horizontalAdvance(item->label) + 26.0 * scale;
        if (m_design.iconSide != DhRadialDesign::IconOnly && !item->icon.isEmpty()) {
            needed += m_design.iconSize * scale + 8.0 * scale;
        }
        width = qMax(width, needed);
    }

    return QSizeF(width, m_design.buttonHeight * scale);
}

void DhRadialMenu::rebuildLayout()
{
    const QList<DhRadialItemSP> items = currentItems();

    m_sizes.clear();
    m_sizes.reserve(items.size());
    for (const DhRadialItemSP &item : items) {
        m_sizes.append(sizeForItem(item));
    }

    const qreal scale = m_design.uiScale;
    m_hubRadius = m_design.showCenter ? m_design.centerRadius * scale : 0.0;

    // pieRadius is the distance to the INNER edge of the ring, so the hub only
    // sets a floor: whichever is further out wins.
    const qreal hubClearance = m_hubRadius + m_design.slotPadding * scale;

    m_blocks = DhRadialLayout::placeBlocks(
        m_sizes,
        m_design.pieRadius * scale,
        hubClearance,
        m_design.slotPadding * scale,
        m_design.ringMode == DhRadialDesign::Compact);

    // RING style keeps its own two radii.
    m_innerRadius = m_design.ringInnerRadius * scale;
    m_outerRadius = m_design.ringOuterRadius * scale;

    buildCells();
}

void DhRadialMenu::buildCells()
{
    m_cells.clear();
    closePanel();

    const QList<DhRadialItemSP> items = currentItems();
    const qreal scale = m_design.uiScale;

    for (int i = 0; i < items.size() && i < m_blocks.size(); ++i) {
        const DhRadialItemSP item = items.at(i);

        QRectF block(QPointF(0, 0), m_sizes.at(i));
        block.moveCenter(m_blocks.at(i).centre);   // still relative to the centre

        if (item && item->isSubmenu() && item->slotLayout == DhRadialItem::Grid) {
            QVector<qreal> spans;
            spans.reserve(item->children.size());
            for (const DhRadialItemSP &child : item->children) {
                spans.append(child ? child->colSpan : 1.0);
            }

            const int columns = item->columns > 0 ? item->columns : m_design.gridColumns;
            const qreal header = m_design.gridHeader ? (m_design.fontSize * scale * 1.9) : 0.0;

            const QVector<QRectF> cells = DhRadialLayout::gridMetrics(
                spans, columns,
                (item->cellWidth > 0 ? item->cellWidth : m_design.gridCellWidth) * scale,
                (item->cellHeight > 0 ? item->cellHeight : m_design.gridCellHeight) * scale,
                m_design.itemSpacing * scale);

            for (int c = 0; c < cells.size() && c < item->children.size(); ++c) {
                Cell cell;
                cell.rect = cells.at(c).translated(block.left(), block.top() + header);
                cell.item = item->children.at(c);
                cell.slot = i;
                cell.child = c;
                cell.circle = false;   // grid cells are always rectangles
                m_cells.append(cell);
            }
            continue;
        }

        Cell cell;
        cell.rect = block;
        cell.item = item;
        cell.slot = i;
        cell.child = -1;
        cell.circle = isCircle(item);
        m_cells.append(cell);
    }
}

void DhRadialMenu::openPanel(int slot)
{
    m_panelCells.clear();
    m_panelSlot = -1;
    m_panelDir = QStringLiteral("FAN");

    const QList<DhRadialItemSP> items = currentItems();
    if (slot < 0 || slot >= items.size() || slot >= m_blocks.size()) return;

    const DhRadialItemSP parent = items.at(slot);
    if (!isPanelSlot(parent)) return;

    if (parent->slotLayout == DhRadialItem::Sprout) {
        layoutFanPanel(slot, parent);
    } else {
        layoutStripPanel(slot, parent);
    }
}

void DhRadialMenu::layoutFanPanel(int slot, const DhRadialItemSP &parent)
{
    const qreal scale = m_design.uiScale;
    const int count = parent->children.size();

    // More children than the fan can hold without crowding: leave it as a
    // sub-ring instead - tapping the slot still opens it, just the long way.
    if (count > m_design.sproutMaxFan) return;

    const qreal size = m_design.sproutSize * scale;
    const qreal radius = DhRadialLayout::fanRadius(count, size, m_design.sproutSpreadDeg,
                                                   m_design.sproutRadius * scale,
                                                   m_design.itemSpacing * scale);
    const QVector<qreal> angles =
        DhRadialLayout::fanAngles(count, m_blocks.at(slot).theta, m_design.sproutSpreadDeg);

    for (int c = 0; c < count && c < angles.size(); ++c) {
        const QPointF unit = DhRadialLayout::unitVector(angles.at(c));
        Cell cell;
        cell.rect = QRectF(0, 0, size, size);
        cell.rect.moveCenter(m_blocks.at(slot).centre + unit * radius);
        cell.item = parent->children.at(c);
        cell.slot = slot;
        cell.child = c;
        cell.circle = true;
        m_panelCells.append(cell);
    }

    m_panelSlot = slot;
    m_panelDir = QStringLiteral("FAN");
}

DhRadialMenu::PanelGeometry DhRadialMenu::computeStripPanel(int slot, const DhRadialItemSP &parent) const
{
    PanelGeometry geom;
    if (!parent || parent->children.isEmpty()) return geom;
    if (slot < 0 || slot >= m_blocks.size() || slot >= m_sizes.size()) return geom;

    const qreal scale = m_design.uiScale;
    const bool dropdown = parent->slotLayout == DhRadialItem::Dropdown;

    QRectF parentRect(QPointF(0, 0), m_sizes.at(slot));
    parentRect.moveCenter(m_blocks.at(slot).centre);

    QString direction = parent->drawerDirection.isEmpty()
        ? QStringLiteral("AUTO") : parent->drawerDirection;
    if (direction == QLatin1String("AUTO")) {
        direction = dropdown ? QStringLiteral("DOWN")
                             : DhRadialLayout::snapDirection(m_blocks.at(slot).theta);
    }
    if (direction != QLatin1String("RIGHT") && direction != QLatin1String("LEFT")
        && direction != QLatin1String("UP") && direction != QLatin1String("DOWN")) {
        // RADIAL, or anything unrecognised: fall back to the snapped axis. The
        // desktop plugin's exact-ray placement for RADIAL is not ported.
        direction = DhRadialLayout::snapDirection(m_blocks.at(slot).theta);
    }
    geom.direction = direction;

    const bool vertical = (direction == QLatin1String("UP") || direction == QLatin1String("DOWN"));
    const bool reverse = (direction == QLatin1String("LEFT") || direction == QLatin1String("UP"));
    const QString flow = parent->drawerFlow.isEmpty() ? QStringLiteral("LINE") : parent->drawerFlow;
    const qreal spacing = m_design.itemSpacing * scale;

    const int cellW = dropdown ? m_design.dropdownCellWidth : m_design.drawerCellWidth;
    const int cellH = dropdown ? m_design.dropdownCellHeight : m_design.drawerCellHeight;

    QVector<QRectF> localCells;
    QSizeF panelSize;

    if (flow == QLatin1String("GRID")) {
        QVector<qreal> spans;
        spans.reserve(parent->children.size());
        for (const DhRadialItemSP &kid : parent->children) spans.append(kid ? kid->colSpan : 1.0);

        const int columns = parent->columns > 0 ? parent->columns : m_design.gridColumns;
        const qreal cw = (parent->cellWidth > 0 ? parent->cellWidth
                          : (cellW > 0 ? cellW : m_design.gridCellWidth)) * scale;
        const qreal ch = (parent->cellHeight > 0 ? parent->cellHeight
                          : (cellH > 0 ? cellH : m_design.gridCellHeight)) * scale;
        localCells = DhRadialLayout::gridMetrics(spans, columns, cw, ch, spacing, &panelSize);
    } else {
        // A ragged strip of auto-fit label widths looks like a bug report, so
        // every cell in a strip shares one size - the shared width a dropdown
        // wants anyway, and cheap enough for a drawer not to be missed.
        QVector<QSizeF> sizes;
        sizes.reserve(parent->children.size());
        const qreal w = (cellW > 0 ? cellW : m_design.buttonWidth) * scale;
        const qreal h = (cellH > 0 ? cellH : m_design.buttonHeight) * scale;
        for (int i = 0; i < parent->children.size(); ++i) sizes.append(QSizeF(w, h));
        localCells = DhRadialLayout::lineMetrics(sizes, vertical, spacing, reverse, &panelSize);
    }

    const QPointF topLeft = DhRadialLayout::placePanel(
        parentRect, panelSize.width(), panelSize.height(), direction,
        dropdown ? QStringLiteral("START") : QStringLiteral("CENTER"),
        m_design.panelGap * scale);

    geom.cells.reserve(localCells.size());
    for (const QRectF &local : localCells) {
        geom.cells.append(local.translated(topLeft));
    }
    return geom;
}

void DhRadialMenu::layoutStripPanel(int slot, const DhRadialItemSP &parent)
{
    const PanelGeometry geom = computeStripPanel(slot, parent);
    if (geom.cells.size() != parent->children.size()) return;

    for (int c = 0; c < geom.cells.size(); ++c) {
        Cell cell;
        cell.rect = geom.cells.at(c);
        cell.item = parent->children.at(c);
        cell.slot = slot;
        cell.child = c;
        cell.circle = isCircle(cell.item);
        m_panelCells.append(cell);
    }

    m_panelSlot = slot;
    m_panelDir = (parent->drawerFlow.toUpper() == QLatin1String("GRID"))
        ? QStringLiteral("GRID") : QStringLiteral("LINE");
}

void DhRadialMenu::closePanel()
{
    m_panelCells.clear();
    m_panelSlot = -1;
    m_panelDir = QStringLiteral("FAN");
    m_hoverPanel = -1;
    m_pendingPanel = -1;
    m_panelTimer.stop();
}

void DhRadialMenu::slotPanelTimeout()
{
    if (m_pendingPanel < 0) return;
    openPanel(m_pendingPanel);
    m_pendingPanel = -1;
    update();
}

// ---------------------------------------------------------------------------
// Popup
// ---------------------------------------------------------------------------

QRectF DhRadialMenu::contentRect() const
{
    QRectF box;

    if (m_design.sliceStyle == DhRadialDesign::Ring) {
        box = QRectF(-m_outerRadius, -m_outerRadius,
                     m_outerRadius * 2.0, m_outerRadius * 2.0);
    } else {
        for (const Cell &cell : m_cells) {
            box = box.isNull() ? cell.rect : box.united(cell.rect);
        }
    }

    // The hub is drawn whether or not a slot reaches over it.
    if (m_design.showCenter && m_hubRadius > 0.0) {
        const QRectF hub(-m_hubRadius, -m_hubRadius, m_hubRadius * 2.0, m_hubRadius * 2.0);
        box = box.isNull() ? hub : box.united(hub);
    }

    // Reserve the reach of every panel that COULD open, not just the one that
    // is open now. Sizing the window when a drawer appears would mean resizing
    // a visible popup mid-gesture, and a panel that opens outside its own
    // window is simply invisible.
    const QList<DhRadialItemSP> items = currentItems();
    const qreal scale = m_design.uiScale;
    const qreal fanReach = (m_design.sproutRadius + m_design.sproutSize) * scale;

    for (int i = 0; i < items.size() && i < m_blocks.size(); ++i) {
        const DhRadialItemSP &item = items.at(i);
        if (!isPanelSlot(item)) continue;

        if (item->slotLayout == DhRadialItem::Sprout) {
            if (item->children.size() > m_design.sproutMaxFan) continue;
            const QPointF c = m_blocks.at(i).centre;
            box = box.united(QRectF(c.x() - fanReach, c.y() - fanReach,
                                    fanReach * 2.0, fanReach * 2.0));
        } else {
            const PanelGeometry geom = computeStripPanel(i, item);
            for (const QRectF &cell : geom.cells) {
                box = box.united(cell);
            }
        }
    }

    if (box.isNull()) {
        box = QRectF(-1.0, -1.0, 2.0, 2.0);
    }
    return box;
}

void DhRadialMenu::fitToScreen(const QSize &available)
{
    // Content bigger than the screen cannot be clamped into view - clamping
    // only slides the window, and whatever is past the edge stays past it. The
    // only honest fix is to make the menu smaller, so do that rather than
    // quietly amputating the far side of it.
    const qreal floorScale = 0.45;

    for (int pass = 0; pass < 4; ++pass) {
        const QRectF box = contentRect();
        const qreal needW = box.width() + WIDGET_MARGIN * 2;
        const qreal needH = box.height() + WIDGET_MARGIN * 2;

        if (needW <= available.width() && needH <= available.height()) return;
        if (m_design.uiScale <= floorScale) return;

        const qreal shrink = qMin(available.width() / qMax(1.0, needW),
                                  available.height() / qMax(1.0, needH));
        // A hair under the exact fit: rounding and stroke widths both add a
        // pixel or two after the fact.
        m_design.uiScale = qMax(floorScale, m_design.uiScale * shrink * 0.98);
        rebuildLayout();
    }
}

void DhRadialMenu::popupAt(const QPoint &globalPos)
{
    reloadConfig();

    m_hoverCell = -1;
    m_hoverCentre = false;
    closePanel();

    rebuildLayout();

    QScreen *screen = QGuiApplication::screenAt(globalPos);
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect area = screen ? screen->availableGeometry() : QRect(0, 0, 800, 600);

    fitToScreen(area.size());

    const QRectF box = contentRect();
    const int width  = qCeil(box.width())  + WIDGET_MARGIN * 2;
    const int height = qCeil(box.height()) + WIDGET_MARGIN * 2;

    // The centre is placed where the content says it should be, not in the
    // middle of the window. Those are the same point only when the layout
    // happens to be symmetric, and a grid or a fan on one side makes sure it
    // never is.
    m_centre = QPointF(WIDGET_MARGIN - box.left(), WIDGET_MARGIN - box.top());

    // The widget is only as big as the content. An earlier version covered the
    // whole screen and dimmed it, which read as a modal takeover rather than a
    // menu - and gave a stale frame something to ghost onto.
    QRect frame(0, 0, width, height);
    frame.moveTopLeft(globalPos - m_centre.toPoint());

    // Keep it fully on screen; a button you cannot reach is a button you
    // cannot use. Because m_centre is measured from the frame's own top-left,
    // sliding the frame moves the whole menu with it and nothing is lost.
    if (frame.left()   < area.left())   frame.moveLeft(area.left());
    if (frame.top()    < area.top())    frame.moveTop(area.top());
    if (frame.right()  > area.right())  frame.moveRight(area.right());
    if (frame.bottom() > area.bottom()) frame.moveBottom(area.bottom());

    setGeometry(frame);

    if (m_design.openAnimation && m_design.animMs > 0) {
        m_animT = 0.0;
        m_animClock.start();
        m_animTimer.start();
    } else {
        m_animT = 1.0;
        m_animTimer.stop();
    }

    show();
    raise();
    activateWindow();
    setFocus();
    update();
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------

bool DhRadialMenu::isInCentre(const QPointF &pos) const
{
    if (!m_design.showCenter) return false;
    const QPointF delta = pos - m_centre;
    return std::hypot(delta.x(), delta.y()) <= m_hubRadius;
}

int DhRadialMenu::panelCellAt(const QPointF &pos) const
{
    for (int i = 0; i < m_panelCells.size(); ++i) {
        if (m_panelCells.at(i).rect.translated(m_centre).contains(pos)) return i;
    }
    return -1;
}

int DhRadialMenu::cellAt(const QPointF &pos) const
{
    if (m_cells.isEmpty()) return -1;

    const QPointF delta = pos - m_centre;
    const qreal distance = std::hypot(delta.x(), delta.y());
    const qreal scale = m_design.uiScale;

    if (distance < m_design.deadZone * scale) return -1;

    const qreal fromTop = DhRadialLayout::angleOf(delta.x(), delta.y());

    if (m_design.sliceStyle == DhRadialDesign::Ring) {
        if (distance < m_innerRadius || distance > m_outerRadius) return -1;
        const int index = int(fromTop / (360.0 / m_cells.size()));
        return qBound(0, index, m_cells.size() - 1);
    }

    // Exact hit first: the pointer is genuinely inside a button.
    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells.at(i).rect.translated(m_centre).contains(pos)) return i;
    }

    // Otherwise flick-select: past the dead zone, the nearest cell by angle
    // arms. This is what makes the menu usable as a gesture rather than a
    // target-acquisition exercise - throw the cursor and let go.
    //
    // Angle alone is not enough once grids exist: every cell of a grid points
    // roughly the same way, so a pure angular winner would be an arbitrary one
    // of them. Cells within a degree of each other are separated by plain
    // distance instead, which picks the one actually nearest the cursor.
    if (!m_design.flickSelect) return -1;

    int best = -1;
    qreal bestAngle = 361.0;
    qreal bestDistance = 0.0;

    for (int i = 0; i < m_cells.size(); ++i) {
        const QPointF c = m_cells.at(i).rect.center();
        const qreal d = DhRadialLayout::angleDelta(fromTop,
                                                   DhRadialLayout::angleOf(c.x(), c.y()));
        const QPointF away = pos - (m_centre + c);
        const qreal dist = std::hypot(away.x(), away.y());

        const bool better = (best < 0)
            || (d < bestAngle - 1.0)
            || (qAbs(d - bestAngle) <= 1.0 && dist < bestDistance);

        if (better) {
            bestAngle = qMin(d, bestAngle);
            bestDistance = dist;
            best = i;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

QColor DhRadialMenu::tintFor(const DhRadialItemSP &item, int index,
                             const DhRadialItemSP &parent) const
{
    if (item && item->color.isValid())     return item->color;
    if (parent && parent->color.isValid()) return parent->color;
    return paletteColour(index);
}

QColor DhRadialMenu::restingFill(const DhRadialItemSP &item, int index,
                                 const DhRadialItemSP &parent) const
{
    QColor base = m_design.buttonColor;
    if (m_design.tintStrength <= 0.0) return base;

    // An item's own colour tints its resting fill rather than replacing it, so
    // a colourful config still reads as one coherent menu instead of a bag of
    // sweets. tint_strength 0 ignores item colours, 1 makes the button BE the
    // item colour.
    const QColor tint = tintFor(item, index, parent);
    const qreal t = m_design.tintStrength;

    QColor out(
        qRound(base.red()   * (1.0 - t) + tint.red()   * t),
        qRound(base.green() * (1.0 - t) + tint.green() * t),
        qRound(base.blue()  * (1.0 - t) + tint.blue()  * t),
        base.alpha());
    return out;
}

void DhRadialMenu::paintHub(QPainter &painter)
{
    if (!m_design.showCenter) return;

    const QRectF hub(m_centre.x() - m_hubRadius, m_centre.y() - m_hubRadius,
                     m_hubRadius * 2.0, m_hubRadius * 2.0);

    painter.setPen(QPen(m_design.centerBorderColor, m_design.borderWidth * m_design.uiScale));
    painter.setBrush(m_hoverCentre ? m_design.centerColor.lighter(135) : m_design.centerColor);
    painter.drawEllipse(hub);

    if (!m_design.centerLabel) return;

    QFont font(m_design.fontFamily,
               qMax(1, qRound(m_design.centerFontSize * m_design.uiScale)));
    painter.setFont(font);
    painter.setPen(m_design.centerTextColor);

    QString text = centreLabel();
    if (!m_stackLabels.isEmpty()) {
        text = m_stackLabels.last() + QLatin1Char('\n') + text;
    }
    painter.drawText(hub, Qt::AlignCenter | Qt::TextWordWrap, text);
}

void DhRadialMenu::paintGlow(QPainter &painter, const QPainterPath &path)
{
    if (!m_design.glowEnabled) return;
    const qreal scale = m_design.uiScale;
    const int steps = 3;
    painter.setBrush(Qt::NoBrush);
    for (int k = steps; k >= 1; --k) {
        const qreal width = m_design.glowSize * scale * (qreal(k) / steps);
        QColor glow = m_design.glowColor;
        glow.setAlphaF(qBound(0.0, glow.alphaF() * (0.55 / k), 1.0));
        QPen pen(glow, width);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.drawPath(path);
    }
}

void DhRadialMenu::paintCell(QPainter &painter, const Cell &cell, bool hovered)
{
    const qreal scale = m_design.uiScale;
    const DhRadialItemSP item = cell.item;
    const QRectF rect = cell.rect.translated(m_centre);

    // A captioned circle is a circle with a label parked underneath it, so the
    // shape and the text want different rectangles out of the one cell.
    QRectF shapeRect = rect;
    QRectF captionRect;

    if (cell.circle) {
        const qreal side = qMin(rect.width(), rect.height());
        shapeRect = QRectF(rect.center().x() - side / 2.0, rect.top(), side, side);
        if (rect.bottom() - shapeRect.bottom() > 1.0) {
            captionRect = QRectF(rect.left(), shapeRect.bottom(),
                                 rect.width(), rect.bottom() - shapeRect.bottom());
        }
    }

    QPainterPath path;
    if (cell.circle) {
        path.addEllipse(shapeRect);
    } else {
        const qreal radius = m_design.cornerRadius * scale;
        path.addRoundedRect(shapeRect, radius, radius);
    }

    if (m_design.textShadow && m_design.shadowColor.alpha() > 0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_design.shadowColor);
        painter.drawPath(path.translated(0.0, 2.0 * scale));
    }

    if (hovered) paintGlow(painter, path);

    // A cell with child >= 0 is drawn inline under its parent, so it inherits
    // the parent's colour when it has none of its own.
    const DhRadialItemSP cellParent =
        (cell.child >= 0) ? currentItems().value(cell.slot) : DhRadialItemSP();

    painter.setBrush(hovered ? m_design.buttonHoverColor
                             : restingFill(item, cell.slot, cellParent));
    painter.setPen(QPen(hovered ? m_design.borderHoverColor : m_design.borderColor,
                        m_design.borderWidth * scale));
    painter.drawPath(path);

    QString text = item ? item->label : QString();
    if (item && item->isSubmenu() && cell.child < 0) {
        // Drill-down always shows its glyph; a drawer/dropdown/sprout marker
        // is gated by panel_arrow since it is decoration, not navigation.
        if (item->slotLayout == DhRadialItem::Submenu) {
            text += QStringLiteral(" ▸");
        } else if (m_design.panelArrow) {
            if (item->slotLayout == DhRadialItem::Sprout)        text += QStringLiteral(" ⁙");
            else if (item->slotLayout == DhRadialItem::Dropdown) text += QStringLiteral(" ▾");
            else                                                 text += QStringLiteral(" ▸");
        }
    }

    const QIcon icon = iconFor(item);
    const qreal iconPx = m_design.iconSize * scale;

    QRectF iconRect;
    QRectF textRect = captionRect.isValid() ? captionRect : shapeRect;

    if (!icon.isNull() && iconPx >= 1.0) {
        if (m_design.iconSide == DhRadialDesign::IconOnly) {
            iconRect = QRectF(0, 0, iconPx, iconPx);
            iconRect.moveCenter(shapeRect.center());
            textRect = QRectF();
        } else if (captionRect.isValid()) {
            // The glyph owns the circle, the label owns the caption. This is
            // already an icon-over-text layout, whichever side was asked for.
            iconRect = QRectF(0, 0, iconPx, iconPx);
            iconRect.moveCenter(shapeRect.center());
        } else if (m_design.iconSide == DhRadialDesign::IconTop) {
            const qreal pad = 3.0 * scale;
            iconRect = QRectF(shapeRect.center().x() - iconPx / 2.0,
                              shapeRect.top() + pad, iconPx, iconPx);
            textRect = QRectF(shapeRect.left(), iconRect.bottom(),
                              shapeRect.width(), shapeRect.bottom() - iconRect.bottom());
        } else {
            const qreal pad = 7.0 * scale;
            const qreal gap = 4.0 * scale;
            iconRect = QRectF(shapeRect.left() + pad,
                              shapeRect.center().y() - iconPx / 2.0, iconPx, iconPx);
            textRect = QRectF(iconRect.right() + gap, shapeRect.top(),
                              shapeRect.right() - iconRect.right() - gap - pad,
                              shapeRect.height());
        }
    }

    if (iconRect.isValid() && !icon.isNull()) {
        const qreal wasOpaque = painter.opacity();
        painter.setOpacity(wasOpaque * qBound(0.0, m_design.iconOpacity, 1.0));
        icon.paint(&painter, iconRect.toAlignedRect(), Qt::AlignCenter,
                   hovered ? QIcon::Active : QIcon::Normal);
        painter.setOpacity(wasOpaque);
    }

    if (text.isEmpty() || !textRect.isValid() || textRect.width() < 6.0 * scale) return;

    QFont font(m_design.fontFamily, qMax(1, qRound(m_design.fontSize * scale)));
    font.setBold(hovered);
    painter.setFont(font);
    painter.setPen(hovered ? m_design.textHoverColor : m_design.textColor);

    const QFontMetricsF metrics(font);
    const int align = (iconRect.isValid() && m_design.iconSide == DhRadialDesign::IconLeft
                       && !captionRect.isValid())
        ? (Qt::AlignLeft | Qt::AlignVCenter)
        : (Qt::AlignCenter | Qt::TextWordWrap);

    painter.drawText(textRect, align,
                     metrics.elidedText(text, Qt::ElideRight,
                                        qRound(textRect.width() - 2.0 * scale)));
}

void DhRadialMenu::paintButtons(QPainter &painter)
{
    const QList<DhRadialItemSP> items = currentItems();
    const qreal scale = m_design.uiScale;

    // Grid headers belong to the slot, not to any one of its cells, so they are
    // drawn before the cells rather than by them. Without one an inline grid is
    // just a loose cluster of buttons with nothing saying what they are.
    if (m_design.gridHeader) {
        QFont font(m_design.fontFamily, qMax(1, qRound(m_design.fontSize * scale)));
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(m_design.textColor);

        const QFontMetricsF metrics(font);

        for (int i = 0; i < items.size() && i < m_blocks.size() && i < m_sizes.size(); ++i) {
            const DhRadialItemSP item = items.at(i);
            if (!item || !item->isSubmenu() || item->slotLayout != DhRadialItem::Grid) continue;

            QRectF block(QPointF(0, 0), m_sizes.at(i));
            block.moveCenter(m_centre + m_blocks.at(i).centre);

            const QRectF header(block.left(), block.top(),
                                block.width(), m_design.fontSize * scale * 1.9);
            painter.drawText(header, Qt::AlignCenter,
                             metrics.elidedText(item->label, Qt::ElideRight,
                                                qRound(header.width())));
        }
    }

    for (int i = 0; i < m_cells.size(); ++i) {
        paintCell(painter, m_cells.at(i), i == m_hoverCell && m_hoverPanel < 0);
    }
}

void DhRadialMenu::paintPanel(QPainter &painter)
{
    if (m_panelCells.isEmpty()) return;
    if (m_panelSlot < 0 || m_panelSlot >= m_blocks.size()) return;

    const qreal scale = m_design.uiScale;

    if (m_panelDir == QLatin1String("FAN")) {
        // A fan is a scatter of separate satellites, so a backing plate
        // around it would just be a big empty box. Optional hairlines back to
        // the parent instead - without them it reads as a second, unrelated
        // cluster of buttons that appeared out of nowhere.
        if (m_design.sproutConnectors) {
            const QPointF from = m_centre + m_blocks.at(m_panelSlot).centre;
            painter.setPen(QPen(m_design.borderColor, 1.0 * scale));
            painter.setBrush(Qt::NoBrush);
            for (const Cell &cell : m_panelCells) {
                painter.drawLine(from, m_centre + cell.rect.center());
            }
        }
    } else {
        // A drawer/dropdown gets a backing plate so its cells read as one
        // block instead of a handful of buttons that happen to have landed
        // in a row.
        QRectF region;
        for (const Cell &cell : m_panelCells) {
            region = region.isNull() ? cell.rect : region.united(cell.rect);
        }
        if (region.isValid()) {
            const qreal pad = 5.0 * scale;
            QRectF plate = region.translated(m_centre).adjusted(-pad, -pad, pad, pad);
            const qreal radius = m_design.cornerRadius * scale;

            if (m_design.shadowColor.alpha() > 0) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(m_design.shadowColor);
                painter.drawRoundedRect(plate.translated(0, 3.0 * scale), radius, radius);
            }

            QColor accent = restingFill(currentItems().value(m_panelSlot), m_panelSlot);
            painter.setPen(QPen(accent, qMax(0.8, 1.0 * scale)));
            QColor plateFill = m_design.centerColor;
            plateFill.setAlphaF(qBound(0.0, plateFill.alphaF() * 0.96 + 0.04, 1.0));
            painter.setBrush(plateFill);
            painter.drawRoundedRect(plate, radius, radius);
        }
    }

    for (int i = 0; i < m_panelCells.size(); ++i) {
        paintCell(painter, m_panelCells.at(i), i == m_hoverPanel);
    }
}

void DhRadialMenu::paintRing(QPainter &painter)
{
    const QList<DhRadialItemSP> items = currentItems();
    if (items.isEmpty()) return;

    const qreal sweep = 360.0 / items.size();
    const QRectF outerRect(m_centre.x() - m_outerRadius, m_centre.y() - m_outerRadius,
                           m_outerRadius * 2.0, m_outerRadius * 2.0);
    const QRectF innerRect(m_centre.x() - m_innerRadius, m_centre.y() - m_innerRadius,
                           m_innerRadius * 2.0, m_innerRadius * 2.0);

    QFont font(m_design.fontFamily,
               qMax(1, qRound(m_design.fontSize * m_design.uiScale)));
    painter.setFont(font);
    const QFontMetricsF metrics(font);

    for (int i = 0; i < items.size(); ++i) {
        const DhRadialItemSP item = items.at(i);
        const bool hovered = (i == m_hoverCell);

        // Qt angles run counter-clockwise from three o'clock.
        const qreal startAngle = 90.0 - (i + 1) * sweep + m_design.ringGapDeg / 2.0;
        const qreal spanAngle = sweep - m_design.ringGapDeg;

        QPainterPath path;
        path.arcMoveTo(outerRect, startAngle);
        path.arcTo(outerRect, startAngle, spanAngle);
        path.arcTo(innerRect, startAngle + spanAngle, -spanAngle);
        path.closeSubpath();

        painter.setPen(QPen(hovered ? m_design.borderHoverColor : m_design.borderColor,
                            m_design.borderWidth * m_design.uiScale));
        painter.setBrush(hovered ? m_design.buttonHoverColor : restingFill(item, i));
        painter.drawPath(path);

        const qreal midAngle = qDegreesToRadians(startAngle + spanAngle / 2.0);
        const qreal labelRadius = (m_innerRadius + m_outerRadius) / 2.0;
        const QPointF labelCentre(m_centre.x() + labelRadius * std::cos(midAngle),
                                  m_centre.y() - labelRadius * std::sin(midAngle));

        QString text = item ? item->label : QString();
        if (item && item->isSubmenu()) text += QStringLiteral(" ▸");

        const qreal maxWidth = (m_outerRadius - m_innerRadius) * 1.6;
        const QRectF textRect(labelCentre.x() - maxWidth / 2.0,
                              labelCentre.y() - metrics.height(),
                              maxWidth, metrics.height() * 2.0);

        painter.setPen(hovered ? m_design.textHoverColor : m_design.textColor);
        painter.drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap,
                         metrics.elidedText(text, Qt::ElideRight, qRound(maxWidth)));
    }
}

void DhRadialMenu::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);

    // Explicitly blow away the previous frame. On a translucent popup the
    // buffer is not cleared for us, and the leftovers show up as ghosting.
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (m_design.bgColor.alpha() > 0) {
        painter.fillRect(rect(), m_design.bgColor);
    }

    // Open animation: a quick scale-and-fade in from the popup point, eased
    // out so it settles rather than snapping. m_animT is driven by a 16ms
    // timer started in popupAt() and clamped to 1.0 once it finishes, so a
    // slow first paint never leaves the menu stuck half-transparent.
    const bool animating = m_design.openAnimation && m_animT < 1.0;
    if (animating) {
        const qreal t = 1.0 - qPow(1.0 - m_animT, 3.0);   // ease-out cubic
        painter.setOpacity(qBound(0.0, t, 1.0));
        painter.translate(m_centre);
        painter.scale(0.85 + 0.15 * t, 0.85 + 0.15 * t);
        painter.translate(-m_centre);
    }

    if (m_design.sliceStyle == DhRadialDesign::Ring) {
        paintRing(painter);
    } else {
        paintButtons(painter);
    }

    paintPanel(painter);
    paintHub(painter);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool DhRadialMenu::isPanelReady(const DhRadialItemSP &item) const
{
    // Sprout has its own crowding cap; drawers and dropdowns take any number
    // of children because they grow into open screen instead of a fixed arc.
    if (!isPanelSlot(item)) return false;
    if (item->slotLayout == DhRadialItem::Sprout) return item->children.size() <= m_design.sproutMaxFan;
    return true;
}

void DhRadialMenu::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = eventPos(event);

    // An open panel wins over whatever is behind it: its cells overlap the
    // slots either side of the parent, and the thing you can see is the thing
    // you meant to touch.
    const int panelHit = panelCellAt(pos);
    if (panelHit >= 0) {
        if (panelHit != m_hoverPanel) {
            m_hoverPanel = panelHit;
            update();
        }
        return;
    }

    const int cell = cellAt(pos);
    const bool centre = isInCentre(pos);

    const int slot = (cell >= 0 && cell < m_cells.size()) ? m_cells.at(cell).slot : -1;
    const QList<DhRadialItemSP> items = currentItems();
    const DhRadialItemSP slotItem =
        (slot >= 0 && slot < items.size() && m_cells.at(cell).child < 0)
        ? items.at(slot) : DhRadialItemSP();

    const bool clickOnly = slotItem && slotItem->slotLayout != DhRadialItem::Sprout
        && m_design.panelTrigger == DhRadialDesign::PanelClick;

    if (isPanelReady(slotItem) && !clickOnly) {
        // Dwell rather than open on contact: sweeping past a slot on the way to
        // its neighbour should not fire a panel in your face.
        const int delay = slotItem->slotLayout == DhRadialItem::Sprout
            ? m_design.sproutDelayMs : m_design.panelDelayMs;
        if (slot != m_panelSlot && slot != m_pendingPanel) {
            m_pendingPanel = slot;
            m_panelTimer.start(qMax(0, delay));
        }
    } else if (!clickOnly && (m_panelSlot >= 0 || m_pendingPanel >= 0)) {
        closePanel();
        update();
    }

    if (cell != m_hoverCell || centre != m_hoverCentre || m_hoverPanel >= 0) {
        m_hoverCell = cell;
        m_hoverCentre = centre;
        m_hoverPanel = -1;
        update();
    }
}

void DhRadialMenu::mousePressEvent(QMouseEvent *event)
{
    mouseMoveEvent(event);
}

void DhRadialMenu::mouseReleaseEvent(QMouseEvent *event)
{
    const QPointF pos = eventPos(event);

    const int sprout = panelCellAt(pos);
    if (sprout >= 0) {
        activateItem(m_panelCells.at(sprout).item);
        return;
    }

    if (isInCentre(pos)) {
        goBack();
        return;
    }

    const int cell = cellAt(pos);
    if (cell < 0) {
        // Released on empty space: treat it as "never mind", the same as
        // tapping away from any other popup.
        close();
        return;
    }

    activate(cell);
}

void DhRadialMenu::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        goBack();
        return;
    }
    QWidget::keyPressEvent(event);
}

void DhRadialMenu::activate(int index)
{
    if (index < 0 || index >= m_cells.size()) return;

    const Cell cell = m_cells.at(index);
    if (!cell.item) return;

    // Tapping a SPROUT slot outright opens its fan there and then, instead of
    // making you hover and wait for a dwell that touch has no way to express.
    if (cell.child < 0 && isPanelReady(cell.item) && m_panelSlot != cell.slot) {
        m_pendingPanel = -1;
        m_panelTimer.stop();
        openPanel(cell.slot);
        update();
        return;
    }

    activateItem(cell.item);
}

void DhRadialMenu::activateItem(const DhRadialItemSP &item)
{
    if (!item) return;

    if (item->isSubmenu()) {
        // Only SUBMENU slots get here: a GRID's children are already cells of
        // their own, and drawer/dropdown/sprout children are handled by the
        // open panel.
        m_stack.append(item->children);
        m_stackLabels.append(item->label);
        m_hoverCell = -1;
        m_hoverCentre = false;
        closePanel();
        rebuildLayout();
        update();
        return;
    }

    const QString actionId = item->action;
    close();

    if (!actionId.isEmpty()) {
        triggerAction(actionId);
    }
}

void DhRadialMenu::goBack()
{
    if (m_panelSlot >= 0) {
        // A fan is a level in its own right as far as the user is concerned.
        closePanel();
        update();
        return;
    }

    if (m_stack.isEmpty()) {
        close();
        return;
    }

    m_stack.removeLast();
    if (!m_stackLabels.isEmpty()) m_stackLabels.removeLast();

    m_hoverCell = -1;
    m_hoverCentre = false;
    rebuildLayout();
    update();
}

void DhRadialMenu::triggerAction(const QString &actionId)
{
    if (!KisPart::exists()) return;

    KisMainWindow *window = KisPart::instance()->currentMainwindow();
    if (!window) return;

    KisKActionCollection *collection = window->actionCollection();
    if (!collection) return;

    // "krita:foo" is what the Python config uses; accept it so a desktop
    // config.json can be dropped in without a search and replace.
    QString id = actionId;
    if (id.startsWith(QLatin1String("krita:"))) {
        id = id.mid(6);
    }

    // The desktop config also references the Python plugin's own scripts by
    // filename. Those cannot exist here - there is no interpreter - but the
    // dhtools plugin provides native equivalents, so map them across rather
    // than leaving dead wedges in a menu copied over from the desktop.
    if (id.endsWith(QLatin1String(".py"))) {
        static const QHash<QString, QString> scriptEquivalents = {
            { QStringLiteral("hide_selected.py"),   QStringLiteral("dh_hide_selected") },
            { QStringLiteral("layer_above.py"),     QStringLiteral("dh_layer_above") },
            { QStringLiteral("layer_below.py"),     QStringLiteral("dh_layer_below") },
            { QStringLiteral("open_current_dir.py"), QStringLiteral("dh_open_current_dir") },
        };
        const QString mapped = scriptEquivalents.value(QFileInfo(id).fileName());
        if (mapped.isEmpty()) {
            warnKrita << "DH radial menu: no native equivalent for script" << id;
            return;
        }
        id = mapped;
    }

    if (QAction *action = collection->action(id)) {
        action->trigger();
        return;
    }

    // Fall back to matching the visible menu text, which is how you reach
    // actions registered by other plugins whose object names you do not know.
    for (QAction *action : collection->actions()) {
        if (KLocalizedString::removeAcceleratorMarker(action->text()).compare(
                id, Qt::CaseInsensitive) == 0) {
            action->trigger();
            return;
        }
    }

    warnKrita << "DH radial menu: no action matches" << actionId;
}
