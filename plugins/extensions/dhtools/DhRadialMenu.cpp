/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "DhRadialMenu.h"

#include <QAction>
#include <QApplication>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QScreen>
#include <QtMath>

#include <klocalizedstring.h>
#include <kconfiggroup.h>
#include <ksharedconfig.h>
#include <kactioncollection.h>
#include <KisPart.h>
#include <KisMainWindow.h>
#include <kis_debug.h>

namespace {

// Logical pixels, overridable via kritarc [DhRadialMenu]. Sized to sit under a
// hand without swallowing the canvas -- the first version scaled to the screen,
// which on a tablet meant a pie menu the size of the tablet.
const qreal DEFAULT_OUTER_RADIUS = 170.0;
const qreal DEFAULT_INNER_RATIO  = 0.38;
const qreal MIN_OUTER_RADIUS     = 70.0;

// Breathing room around the ring for the drop shadow.
const int   WIDGET_MARGIN = 12;

// Leave a sliver between wedges so they read as separate targets on a small
// screen rather than one continuous doughnut.
const qreal WEDGE_GAP_DEGREES = 1.5;

// Used when an item does not name its own colour.
const char *DEFAULT_PALETTE[] = {
    "#4A8FD4", "#1ABC9C", "#9B59B6", "#E67E22",
    "#2ECC71", "#F1C40F", "#E74C3C", "#7F8C8D"
};
const int DEFAULT_PALETTE_SIZE = 8;

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

    reloadConfig();
}

DhRadialMenu::~DhRadialMenu()
{
}

void DhRadialMenu::reloadConfig()
{
    m_root = DhRadialConfig::load();
    m_stack.clear();
    m_stackLabels.clear();
}

QList<DhRadialItemSP> DhRadialMenu::currentItems() const
{
    return m_stack.isEmpty() ? m_root : m_stack.last();
}

void DhRadialMenu::popupAt(const QPoint &globalPos)
{
    reloadConfig();

    m_hoverIndex = -1;
    m_hoverCentre = false;

    KConfigGroup cfg = KSharedConfig::openConfig()->group("DhRadialMenu");
    m_outerRadius = qMax(MIN_OUTER_RADIUS, cfg.readEntry("outerRadius", DEFAULT_OUTER_RADIUS));
    m_innerRadius = m_outerRadius * qBound(0.15, cfg.readEntry("innerRatio", DEFAULT_INNER_RATIO), 0.75);

    QScreen *screen = QGuiApplication::screenAt(globalPos);
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect area = screen ? screen->availableGeometry() : QRect(0, 0, 800, 600);

    // If the ring will not fit on this screen, shrink it rather than clipping.
    const qreal fitRadius = (qMin(area.width(), area.height()) / 2.0) - WIDGET_MARGIN - 4.0;
    if (m_outerRadius > fitRadius) {
        const qreal ratio = m_innerRadius / m_outerRadius;
        m_outerRadius = qMax(MIN_OUTER_RADIUS, fitRadius);
        m_innerRadius = m_outerRadius * ratio;
    }

    // The widget is only as big as the ring. The first version covered the whole
    // screen and dimmed it, which is why it read as a modal takeover instead of
    // a menu -- and why a stale frame had something to ghost onto.
    const int side = qRound(m_outerRadius * 2.0) + WIDGET_MARGIN * 2;
    m_centre = QPointF(side / 2.0, side / 2.0);

    QRect frame(0, 0, side, side);
    frame.moveCenter(globalPos);

    // Keep it fully on screen; a wedge you cannot reach is a wedge you cannot use.
    if (frame.left()   < area.left())   frame.moveLeft(area.left());
    if (frame.top()    < area.top())    frame.moveTop(area.top());
    if (frame.right()  > area.right())  frame.moveRight(area.right());
    if (frame.bottom() > area.bottom()) frame.moveBottom(area.bottom());

    setGeometry(frame);

    show();
    raise();
    activateWindow();
    setFocus();
    update();
}

QColor DhRadialMenu::wedgeColour(const DhRadialItemSP &item, int index) const
{
    if (item && item->color.isValid()) {
        return item->color;
    }
    return QColor(QString::fromLatin1(DEFAULT_PALETTE[index % DEFAULT_PALETTE_SIZE]));
}

QString DhRadialMenu::centreLabel() const
{
    if (m_stack.isEmpty()) {
        return i18n("Close");
    }
    return i18n("Back");
}

int DhRadialMenu::indexAt(const QPointF &pos) const
{
    const QList<DhRadialItemSP> items = currentItems();
    if (items.isEmpty()) return -1;

    const QPointF delta = pos - m_centre;
    const qreal distance = std::hypot(delta.x(), delta.y());

    if (distance < m_innerRadius || distance > m_outerRadius) {
        return -1;
    }

    // Screen y grows downward, so negate it to get a normal maths angle, then
    // rotate so wedge 0 starts at twelve o'clock and they run clockwise.
    qreal degrees = qRadiansToDegrees(std::atan2(-delta.y(), delta.x()));
    qreal fromTop = 90.0 - degrees;
    while (fromTop < 0.0) fromTop += 360.0;
    while (fromTop >= 360.0) fromTop -= 360.0;

    const int index = static_cast<int>(fromTop / (360.0 / items.size()));
    return qBound(0, index, items.size() - 1);
}

bool DhRadialMenu::isInCentre(const QPointF &pos) const
{
    const QPointF delta = pos - m_centre;
    return std::hypot(delta.x(), delta.y()) <= m_innerRadius;
}

void DhRadialMenu::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    const QList<DhRadialItemSP> items = currentItems();

    QPainter painter(this);

    // Explicitly blow away the previous frame. On a translucent popup the buffer
    // is not cleared for us, and the leftovers are what showed up as ghosting.
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    painter.setRenderHint(QPainter::Antialiasing, true);

    if (items.isEmpty()) return;

    // A soft disc just behind the ring instead of dimming the whole screen: it
    // still separates the menu from a busy canvas, without hiding your artwork.
    QRadialGradient backdrop(m_centre, m_outerRadius + WIDGET_MARGIN);
    backdrop.setColorAt(0.0,  QColor(0, 0, 0, 90));
    backdrop.setColorAt(0.85, QColor(0, 0, 0, 70));
    backdrop.setColorAt(1.0,  QColor(0, 0, 0, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(backdrop);
    painter.drawEllipse(m_centre, m_outerRadius + WIDGET_MARGIN, m_outerRadius + WIDGET_MARGIN);

    const qreal sweep = 360.0 / items.size();
    const QRectF outerRect(m_centre.x() - m_outerRadius, m_centre.y() - m_outerRadius,
                           m_outerRadius * 2.0, m_outerRadius * 2.0);
    const QRectF innerRect(m_centre.x() - m_innerRadius, m_centre.y() - m_innerRadius,
                           m_innerRadius * 2.0, m_innerRadius * 2.0);

    QFont labelFont = font();
    labelFont.setPointSizeF(qMax(9.0, m_outerRadius * 0.075));
    labelFont.setBold(true);
    painter.setFont(labelFont);
    const QFontMetrics metrics(labelFont);

    for (int i = 0; i < items.size(); ++i) {
        const DhRadialItemSP item = items.at(i);
        const bool hovered = (i == m_hoverIndex);

        // Qt angles are counter-clockwise from three o'clock, in 1/16 degree.
        const qreal startAngle = 90.0 - (i + 1) * sweep + WEDGE_GAP_DEGREES / 2.0;
        const qreal spanAngle = sweep - WEDGE_GAP_DEGREES;

        QPainterPath path;
        path.arcMoveTo(outerRect, startAngle);
        path.arcTo(outerRect, startAngle, spanAngle);
        path.arcTo(innerRect, startAngle + spanAngle, -spanAngle);
        path.closeSubpath();

        QColor colour = wedgeColour(item, i);
        colour.setAlpha(hovered ? 245 : 190);
        if (hovered) colour = colour.lighter(118);

        painter.setPen(QPen(QColor(0, 0, 0, 140), hovered ? 2.0 : 1.0));
        painter.setBrush(colour);
        painter.drawPath(path);

        // Label, laid out horizontally at the middle of the wedge. Rotated
        // text looks clever and is miserable to read at arm's length.
        const qreal midAngle = qDegreesToRadians(startAngle + spanAngle / 2.0);
        const qreal labelRadius = (m_innerRadius + m_outerRadius) / 2.0;
        const QPointF labelCentre(m_centre.x() + labelRadius * std::cos(midAngle),
                                  m_centre.y() - labelRadius * std::sin(midAngle));

        QString text = item ? item->label : QString();
        if (item && item->isSubmenu()) {
            text += QStringLiteral(" \u25B8");
        }

        const qreal maxWidth = (m_outerRadius - m_innerRadius) * 1.6;
        text = metrics.elidedText(text, Qt::ElideRight, qRound(maxWidth));

        const QRectF textRect(labelCentre.x() - maxWidth / 2.0,
                              labelCentre.y() - metrics.height(),
                              maxWidth, metrics.height() * 2.0);

        painter.setPen(colour.lightness() > 145 ? QColor(20, 20, 20) : QColor(250, 250, 250));
        painter.drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap, text);
    }

    // Centre hub: back out of a submenu, or dismiss.
    QColor hubColour = m_hoverCentre ? QColor(70, 70, 70, 240) : QColor(40, 40, 40, 215);
    painter.setPen(QPen(QColor(0, 0, 0, 160), 1.5));
    painter.setBrush(hubColour);
    painter.drawEllipse(innerRect);

    painter.setPen(QColor(235, 235, 235));
    QString hubText = centreLabel();
    if (!m_stackLabels.isEmpty()) {
        hubText = m_stackLabels.last() + QLatin1Char('\n') + hubText;
    }
    painter.drawText(innerRect, Qt::AlignCenter | Qt::TextWordWrap, hubText);
}

namespace {
inline QPointF eventPos(const QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
}
}

void DhRadialMenu::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = eventPos(event);
    const int index = indexAt(pos);
    const bool centre = isInCentre(pos);

    if (index != m_hoverIndex || centre != m_hoverCentre) {
        m_hoverIndex = index;
        m_hoverCentre = centre;
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

    if (isInCentre(pos)) {
        goBack();
        return;
    }

    const int index = indexAt(pos);
    if (index < 0) {
        // Released on the dimmed area outside the ring: treat it as "never
        // mind", the same as tapping away from any other popup.
        close();
        return;
    }

    activate(index);
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
    const QList<DhRadialItemSP> items = currentItems();
    if (index < 0 || index >= items.size()) return;

    const DhRadialItemSP item = items.at(index);
    if (!item) return;

    if (item->isSubmenu()) {
        m_stack.append(item->children);
        m_stackLabels.append(item->label);
        m_hoverIndex = -1;
        m_hoverCentre = false;
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
    if (m_stack.isEmpty()) {
        close();
        return;
    }

    m_stack.removeLast();
    if (!m_stackLabels.isEmpty()) m_stackLabels.removeLast();

    m_hoverIndex = -1;
    m_hoverCentre = false;
    update();
}

void DhRadialMenu::triggerAction(const QString &actionId)
{
    if (!KisPart::exists()) return;

    KisMainWindow *window = KisPart::instance()->currentMainwindow();
    if (!window) return;

    KisKActionCollection *collection = window->actionCollection();
    if (!collection) return;

    // "krita:foo" is what the Python config used; accept it so an existing
    // config.json can be dropped in without a search and replace.
    QString id = actionId;
    if (id.startsWith(QLatin1String("krita:"))) {
        id = id.mid(6);
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
