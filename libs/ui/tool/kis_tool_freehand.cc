/*
 *  kis_tool_freehand.cc - part of Krita
 *
 *  SPDX-FileCopyrightText: 2003-2007 Boudewijn Rempt <boud@valdyas.org>
 *  SPDX-FileCopyrightText: 2004 Bart Coppens <kde@bartcoppens.be>
 *  SPDX-FileCopyrightText: 2007, 2008, 2010 Cyrille Berger <cberger@cberger.net>
 *  SPDX-FileCopyrightText: 2009 Lukáš Tvrdý <lukast.dev@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_tool_freehand.h"
#include <QPainter>
#include <QRect>
#include <QThreadPool>
#include <QApplication>
#include <QtMath>
#include <QGuiApplication>
#include <QScreen>

#include <Eigen/Core>

#include <kis_icon.h>
#include <KoPointerEvent.h>
#include <KoViewConverter.h>
#include <KoCanvasController.h>

//pop up palette
#include <kis_canvas_resource_provider.h>

// Krita/image
#include <kis_layer.h>
#include <kis_paint_layer.h>
#include <kis_painter.h>
#include <brushengine/kis_paintop.h>
#include <kis_selection.h>
#include <brushengine/kis_paintop_preset.h>
#include <brushengine/KisOptimizedBrushOutline.h>


// Krita/ui
#include "kis_abstract_perspective_grid.h"
#include "kis_config.h"
#include <kconfiggroup.h>
#include <ksharedconfig.h>
#include "kis_config_notifier.h"
#include "kis_image_config.h"
#include "canvas/kis_canvas2.h"
#include "kis_cursor.h"
#include <KisViewManager.h>
#include <kis_painting_assistants_decoration.h>
#include "kis_painting_information_builder.h"
#include "kis_tool_freehand_helper.h"
#include "strokes/freehand_stroke.h"
#include "kis_tool_utils.h"

using namespace std::placeholders; // For _1 placeholder


KisToolFreehand::KisToolFreehand(KoCanvasBase * canvas, const QCursor & cursor,
                                 const KUndo2MagicString &transactionText, bool useSavedSmoothing)
    : KisToolPaint(canvas, cursor),
      m_brushResizeCompressor(200, std::bind(&KisToolFreehand::slotDoResizeBrush, this, _1))
{

    setSupportOutline(true);
    updateMaskSyntheticEventsFromTouch();
    connect(KisConfigNotifier::instance(), SIGNAL(touchPaintingChanged()),
            SLOT(updateMaskSyntheticEventsFromTouch()));

    m_infoBuilder = new KisToolFreehandPaintingInformationBuilder(this);
    m_helper = new KisToolFreehandHelper(m_infoBuilder, canvas->resourceManager(), transactionText,
                                         new KisSmoothingOptions(useSavedSmoothing));

    connect(m_helper, SIGNAL(requestExplicitUpdateOutline()), SLOT(explicitUpdateOutline()));

    connect(qobject_cast<KisCanvas2*>(canvas)->viewManager(), SIGNAL(brushOutlineToggled()), SLOT(explicitUpdateOutline()));

    KisCanvasResourceProvider *provider = qobject_cast<KisCanvas2*>(canvas)->viewManager()->canvasResourceProvider();

    connect(provider, SIGNAL(sigEffectiveCompositeOpChanged()), SLOT(explicitUpdateOutline()));
    connect(provider, SIGNAL(sigEffectiveCompositeOpChanged()), SLOT(resetCursorStyle()));
    connect(provider, SIGNAL(sigPaintOpPresetChanged(KisPaintOpPresetSP)), SLOT(explicitUpdateOutline()));
    connect(provider, SIGNAL(sigPaintOpPresetChanged(KisPaintOpPresetSP)), SLOT(resetCursorStyle()));
}

KisToolFreehand::~KisToolFreehand()
{
    delete m_helper;
    delete m_infoBuilder;
}

void KisToolFreehand::mouseMoveEvent(KoPointerEvent *event)
{
    KisToolPaint::mouseMoveEvent(event);
    m_helper->cursorMoved(convertToPixelCoord(event));
}

KisSmoothingOptionsSP KisToolFreehand::smoothingOptions() const
{
    return m_helper->smoothingOptions();
}

void KisToolFreehand::resetCursorStyle()
{
    KisConfig cfg(true);

    bool useSeparateEraserCursor = cfg.separateEraserCursor() && isEraser();

    switch (useSeparateEraserCursor ? cfg.eraserCursorStyle() : cfg.newCursorStyle()) {
    case CURSOR_STYLE_NO_CURSOR:
        useCursor(KisCursor::blankCursor());
        break;
    case CURSOR_STYLE_POINTER:
        useCursor(KisCursor::arrowCursor());
        break;
    case CURSOR_STYLE_SMALL_ROUND:
        useCursor(KisCursor::roundCursor());
        break;
    case CURSOR_STYLE_CROSSHAIR:
        useCursor(KisCursor::crossCursor());
        break;
    case CURSOR_STYLE_TRIANGLE_RIGHTHANDED:
        useCursor(KisCursor::triangleRightHandedCursor());
        break;
    case CURSOR_STYLE_TRIANGLE_LEFTHANDED:
        useCursor(KisCursor::triangleLeftHandedCursor());
        break;
    case CURSOR_STYLE_BLACK_PIXEL:
        useCursor(KisCursor::pixelBlackCursor());
        break;
    case CURSOR_STYLE_WHITE_PIXEL:
        useCursor(KisCursor::pixelWhiteCursor());
        break;
    case CURSOR_STYLE_ERASER:
        useCursor(KisCursor::eraserCursor());
        break;
    case CURSOR_STYLE_TOOLICON:
    default:
        KisToolPaint::resetCursorStyle();
        break;
    }
}

KisPaintingInformationBuilder* KisToolFreehand::paintingInformationBuilder() const
{
    return m_infoBuilder;
}

void KisToolFreehand::resetHelper(KisToolFreehandHelper *helper)
{
    delete m_helper;
    m_helper = helper;
}

bool KisToolFreehand::supportsPaintingAssistants() const
{
    return true;
}

int KisToolFreehand::flags() const
{
    return KisTool::FLAG_USES_CUSTOM_COMPOSITEOP|KisTool::FLAG_USES_CUSTOM_PRESET
           |KisTool::FLAG_USES_CUSTOM_SIZE;
}

void KisToolFreehand::activate(const QSet<KoShape*> &shapes)
{
    KisToolPaint::activate(shapes);
}

void KisToolFreehand::deactivate()
{
    if (mode() == PAINT_MODE) {
        endStroke();
        setMode(KisTool::HOVER_MODE);
    }
    KisToolPaint::deactivate();
}

void KisToolFreehand::initStroke(KoPointerEvent *event)
{
    m_helper->initPaint(event,
                        convertToPixelCoord(event),
                        image(),
                        currentNode(),
                        image().data());
}

void KisToolFreehand::doStroke(KoPointerEvent *event)
{
    m_helper->paintEvent(event);
}

void KisToolFreehand::endStroke()
{
    m_helper->endPaint();
    bool paintOpIgnoredEvent = currentPaintOpPreset()->settings()->mouseReleaseEvent();
    Q_UNUSED(paintOpIgnoredEvent);
}

bool KisToolFreehand::primaryActionSupportsHiResEvents() const
{
    return true;
}

void KisToolFreehand::beginPrimaryAction(KoPointerEvent *event)
{
    // FIXME: workaround for the Duplicate Op
    trySampleByPaintOp(event, SampleFgImage);

    requestUpdateOutline(event->point, event);

    NodePaintAbility paintability = nodePaintAbility();
    // XXX: move this to KisTool and make it work properly for clone layers: for clone layers, the shape paint tools don't work either
    if (!nodeEditable() || paintability != PAINT) {
        if (paintability == KisToolPaint::VECTOR || paintability == KisToolPaint::CLONE){
            KisCanvas2 * kiscanvas = static_cast<KisCanvas2*>(canvas());
            QString message = i18n("The brush tool cannot paint on this layer.  Please select a paint layer or mask.");
            kiscanvas->viewManager()->showFloatingMessage(message, koIcon("object-locked"));
        }
        else if (paintability == MYPAINTBRUSH_UNPAINTABLE) {
            KisCanvas2 * kiscanvas = static_cast<KisCanvas2*>(canvas());
            QString message = i18n("The MyPaint Brush Engine is not available for this colorspace");
            kiscanvas->viewManager()->showFloatingMessage(message, koIcon("object-locked"));
        }
        event->ignore();

        return;
    }

    KIS_SAFE_ASSERT_RECOVER_RETURN(!m_helper->isRunning());

    setMode(KisTool::PAINT_MODE);

    KisCanvas2 *canvas2 = dynamic_cast<KisCanvas2 *>(canvas());
    if (canvas2) {
        canvas2->viewManager()->disableControls();
    }

    initStroke(event);
}

void KisToolFreehand::continuePrimaryAction(KoPointerEvent *event)
{
    CHECK_MODE_SANITY_OR_RETURN(KisTool::PAINT_MODE);

    requestUpdateOutline(event->point, event);

    /**
     * Actual painting
     */
    doStroke(event);
}

void KisToolFreehand::endPrimaryAction(KoPointerEvent *event)
{
    Q_UNUSED(event);
    CHECK_MODE_SANITY_OR_RETURN(KisTool::PAINT_MODE);

    endStroke();

    if (m_assistant && static_cast<KisCanvas2*>(canvas())->paintingAssistantsDecoration()) {
        static_cast<KisCanvas2*>(canvas())->paintingAssistantsDecoration()->endStroke();
    }

    KisCanvas2 *canvas2 = dynamic_cast<KisCanvas2 *>(canvas());
    if (canvas2) {
        canvas2->viewManager()->enableControls();
    }

    setMode(KisTool::HOVER_MODE);
}

bool KisToolFreehand::trySampleByPaintOp(KoPointerEvent *event, AlternateAction action)
{
    if (action != SampleFgNode && action != SampleFgImage) return false;

    /**
     * FIXME: we need some better way to implement modifiers
     * for a paintop level. This method is used in DuplicateOp only!
     */
    QPointF pos = adjustPosition(event->point, event->point);
    qreal perspective = calculatePerspective(pos);
    if (!currentPaintOpPreset()) {
        return false;
    }
    KisPaintInformation info(convertToPixelCoord(event->point),
                             m_infoBuilder->pressureToCurve(event->pressure()),
                             event->xTilt(), event->yTilt(),
                             event->rotation(),
                             event->tangentialPressure(),
                             perspective, 0, 0);
    info.setRandomSource(new KisRandomSource());
    info.setPerStrokeRandomSource(new KisPerStrokeRandomSource());

    bool paintOpIgnoredEvent = currentPaintOpPreset()->settings()->mousePressEvent(info,
                                                                                   event->modifiers(),
                                                                                   currentNode());
    // DuplicateOP during the sampling of new source point (origin)
    // is the only paintop that returns "false" here
    return !paintOpIgnoredEvent;
}

void KisToolFreehand::activateAlternateAction(AlternateAction action)
{
    if (action != ChangeSize && action != ChangeSizeSnap &&
        action != AdjustBrush && action != ScrollBrush) {
        KisToolPaint::activateAlternateAction(action);
        return;
    }

    useCursor(KisCursor::blankCursor());
    setOutlineVisible(true);
}

void KisToolFreehand::deactivateAlternateAction(AlternateAction action)
{
    if (action != ChangeSize && action != ChangeSizeSnap &&
        action != AdjustBrush && action != ScrollBrush) {
        KisToolPaint::deactivateAlternateAction(action);
        return;
    }

    resetCursorStyle();
    setOutlineVisible(false);
}

void KisToolFreehand::beginAlternateAction(KoPointerEvent *event, AlternateAction action)
{
    if (trySampleByPaintOp(event, action)) {
        m_paintopBasedSamplingInAction = true;
        return;
    }

    if (action != ChangeSize && action != ChangeSizeSnap &&
        action != AdjustBrush && action != ScrollBrush) {
        KisToolPaint::beginAlternateAction(event, action);
        return;
    }

    setMode(GESTURE_MODE);
    m_initialGestureDocPoint = event->point;
    m_initialGestureGlobalPoint = event->globalPos();

    m_lastDocumentPoint = event->point;
    m_lastPaintOpSize = currentPaintOpPreset()->settings()->paintOpSize();
    m_lastPaintOpacity = canvas()->resourceManager()->resource(KoCanvasResource::Opacity).toReal();
    m_adjustAxisResolved = false;
    m_adjustAxisVertical = false;

    {
        // Brush size gets half the screen width because that is Krita's
        // established feel for it. Opacity is a 0..1 range, not a 1..1000 one,
        // so the same distance makes it crawl -- a quarter of the screen is
        // much closer to how the gesture used to behave.
        KConfigGroup gestureCfg = KSharedConfig::openConfig()->group("AdjustBrushGesture");
        m_opacityDragFraction =
            qBound(0.05, gestureCfg.readEntry("opacityDragFraction", 0.25), 2.0);
        m_scrollSizePixels =
            qBound(1.0, gestureCfg.readEntry("scrollSizePixelsPerUnit", 25.0), 500.0);
        m_scrollOpacityPixels =
            qBound(0.5, gestureCfg.readEntry("scrollOpacityPixelsPerPercent", 5.0), 500.0);

        const QString anchor =
            gestureCfg.readEntry("indicatorAnchor", QStringLiteral("above")).toLower();
        m_indicatorAnchor = anchor == QLatin1String("touch")  ? AnchorTouch
                          : (anchor == QLatin1String("centre") ||
                             anchor == QLatin1String("center")) ? AnchorCentre
                          : AnchorAbove;
        m_indicatorClearance =
            qBound(0.0, gestureCfg.readEntry("indicatorClearance", 90.0), 1000.0);
    }

    m_gestureAction = action;
    m_indicatorFellBack = false;
    m_beginAlternateActionEvent = event->deepCopyEvent();
    requestUpdateOutline(gestureOutlineDocPoint(), &m_beginAlternateActionEvent->event);
}

void KisToolFreehand::continueAlternateAction(KoPointerEvent *event, AlternateAction action)
{
    if (trySampleByPaintOp(event, action) || m_paintopBasedSamplingInAction) return;

    if (action != ChangeSize && action != ChangeSizeSnap &&
        action != AdjustBrush && action != ScrollBrush) {
        KisToolPaint::continueAlternateAction(event, action);
        return;
    }

    QPointF lastWidgetPosition = convertDocumentToWidget(m_lastDocumentPoint);
    QPointF actualWidgetPosition = convertDocumentToWidget(event->point);

    QPointF offset = actualWidgetPosition - lastWidgetPosition;

    if (action == ScrollBrush) {
        // A 2D tracker rather than an axis-locked drag: horizontal drives size,
        // vertical drives opacity, and both move at once. This mirrors Shortcut
        // Composer's "Scroll brush size or opacity", reimplemented natively so
        // it survives that plugin updating -- and so it exists at all on
        // Android, where there is no Python to run it.
        const QPointF total =
            actualWidgetPosition - convertDocumentToWidget(m_initialGestureDocPoint);

        // Size uses an exponent of 2, so the value is tracked in sqrt space and
        // squared back. Small brushes crawl, large ones move quickly -- which is
        // what you want when the range runs from 1px to hundreds.
        const qreal startInterpolated = qSqrt(qMax(1.0, m_lastPaintOpSize));
        const qreal interpolated = qMax(1.0, startInterpolated + total.x() / m_scrollSizePixels);
        const qreal maxBrushSize = KisImageConfig(true).maxBrushSize();
        const qreal newSize = qBound(0.01, interpolated * interpolated, maxBrushSize);
        currentPaintOpPreset()->settings()->setPaintOpSize(newSize);

        // Opacity is linear over 10..100%, dragging up for more.
        const qreal startPercent = m_lastPaintOpacity * 100.0;
        const qreal percent =
            qBound(10.0, startPercent - total.y() / m_scrollOpacityPixels, 100.0);
        canvas()->resourceManager()->setResource(KoCanvasResource::Opacity, percent / 100.0);

        m_showOpacityInOutline = true;
        requestUpdateOutline(
            gestureOutlineDocPoint(),
            m_beginAlternateActionEvent.has_value() ? &m_beginAlternateActionEvent->event : nullptr);
        return;
    }

    if (action == AdjustBrush) {
        const QPointF total =
            actualWidgetPosition - convertDocumentToWidget(m_initialGestureDocPoint);

        // Wait until the drag has clearly gone somewhere before deciding what it
        // means, so a three-finger tap does not nudge anything.
        if (!m_adjustAxisResolved) {
            const qreal deadZone = 12.0;
            if (qMax(qAbs(total.x()), qAbs(total.y())) < deadZone) return;
            m_adjustAxisVertical = qAbs(total.y()) >= qAbs(total.x());
            m_adjustAxisResolved = true;
        }

        if (!m_adjustAxisVertical) {
            // Horizontal: opacity. Screen-right means more. Scaled to the screen
            // so the gesture feels the same on a phone and a 4K tablet.
            const qreal range = m_opacityDragFraction
                * QGuiApplication::primaryScreen()->availableVirtualGeometry().width();
            const qreal opacity = qBound(0.0, m_lastPaintOpacity + total.x() / range, 1.0);

            canvas()->resourceManager()->setResource(KoCanvasResource::Opacity, opacity);
            m_showOpacityInOutline = true;

            // Repaint the outline so the opacity fill drawn by KisToolPaint
            // tracks the drag.
            requestUpdateOutline(
                gestureOutlineDocPoint(),
                m_beginAlternateActionEvent.has_value() ? &m_beginAlternateActionEvent->event : nullptr);
            return;
        }

        // Vertical: size. Feed the vertical delta in where the existing
        // ChangeSize code expects the horizontal one, negated so dragging up
        // makes the brush bigger. Identical feel, no duplicated maths.
        offset = QPointF(-offset.y(), 0.0);
        m_showOpacityInOutline = false;
    }

    KisCanvas2 *canvas2 = dynamic_cast<KisCanvas2 *>(canvas());
    KIS_SAFE_ASSERT_RECOVER_RETURN(canvas2);
    QRect screenRect = QGuiApplication::primaryScreen()->availableVirtualGeometry();

    qreal scaleX = 0;
    qreal scaleY = 0;
    canvas2->coordinatesConverter()->imageScale(&scaleX, &scaleY);

    const qreal maxBrushSize = KisImageConfig(true).maxBrushSize();
    const qreal effectiveMaxDragSize = 0.5 * screenRect.width();
    const qreal effectiveMaxBrushSize = qMin(maxBrushSize, effectiveMaxDragSize / scaleX);

    const qreal scaleCoeff = effectiveMaxBrushSize / effectiveMaxDragSize;
    const qreal sizeDiff = scaleCoeff * offset.x() ;

    if (qAbs(sizeDiff) > 0.01) {
        KisPaintOpSettingsSP settings = currentPaintOpPreset()->settings();

        qreal newSize = m_lastPaintOpSize + sizeDiff;

        if (action == ChangeSizeSnap) {
            newSize = qMax(qRound(newSize), 1);
        }

        newSize = qBound(0.01, newSize, maxBrushSize);

        settings->setPaintOpSize(newSize);

        requestUpdateOutline(
            gestureOutlineDocPoint(),
            m_beginAlternateActionEvent.has_value() ? &m_beginAlternateActionEvent->event : nullptr);
        //m_brushResizeCompressor.start(newSize);

        m_lastDocumentPoint = event->point;
        m_lastPaintOpSize = newSize;
    }
}

void KisToolFreehand::endAlternateAction(KoPointerEvent *event, AlternateAction action)
{
    if (trySampleByPaintOp(event, action) || m_paintopBasedSamplingInAction) {
        m_paintopBasedSamplingInAction = false;
        return;
    }

    if (action != ChangeSize && action != ChangeSizeSnap &&
        action != AdjustBrush && action != ScrollBrush) {
        KisToolPaint::endAlternateAction(event, action);
        return;
    }

    KisToolUtils::setCursorPos(m_initialGestureGlobalPoint);
    requestUpdateOutline(m_initialGestureDocPoint, 0);

    m_adjustAxisResolved = false;
    m_showOpacityInOutline = false;
    m_gestureAction = NONE;
    m_indicatorFellBack = false;
    setMode(HOVER_MODE);

    m_beginAlternateActionEvent.reset();
}

QPointF KisToolFreehand::gestureOutlineDocPoint()
{
    if (m_gestureAction != AdjustBrush || m_indicatorAnchor == AnchorTouch) {
        return m_initialGestureDocPoint;
    }

    KisCanvas2 *canvas2 = dynamic_cast<KisCanvas2 *>(canvas());
    if (!canvas2) return m_initialGestureDocPoint;
    const KisCoordinatesConverter *converter = canvas2->coordinatesConverter();

    // Everything below is in canvas-widget pixels, so "above" means up the
    // screen even when the canvas itself is rotated or mirrored.
    const QSizeF widgetSize = converter->getCanvasWidgetSize();
    const QPointF widgetCentre(widgetSize.width() / 2.0, widgetSize.height() / 2.0);

    if (m_indicatorAnchor == AnchorCentre || m_indicatorFellBack) {
        return converter->widgetToDocument(widgetCentre);
    }

    // AnchorAbove: rest the circle's BOTTOM edge a fixed gap above the touch
    // point. As the brush grows the circle extends upward, away from the
    // hand, instead of spreading back down over the fingers.
    qreal scaleX = 1.0;
    qreal scaleY = 1.0;
    converter->imageScale(&scaleX, &scaleY);
    const qreal radius = 0.5 * currentPaintOpPreset()->settings()->paintOpSize() * scaleX;

    const QPointF touch = converter->documentToWidget(m_initialGestureDocPoint);
    QPointF centre(touch.x(), touch.y() - m_indicatorClearance - radius);

    // No room above - gesture near the top edge, or a brush bigger than the
    // space left. The canvas centre is the one spot guaranteed to fit and
    // unlikely to be under the hand.
    const qreal margin = 8.0;
    if (centre.y() - radius < margin) {
        m_indicatorFellBack = true;
        return converter->widgetToDocument(widgetCentre);
    }

    // Keep it on-screen sideways, e.g. a gesture started at the very edge.
    if (2.0 * (radius + margin) <= widgetSize.width()) {
        centre.setX(qBound(radius + margin, centre.x(), widgetSize.width() - radius - margin));
    } else {
        centre.setX(widgetCentre.x());
    }

    return converter->widgetToDocument(centre);
}

bool KisToolFreehand::wantsAutoScroll() const
{
    return false;
}

void KisToolFreehand::setAssistant(bool assistant)
{
    m_assistant = assistant;
}

void KisToolFreehand::setOnlyOneAssistantSnap(bool assistant)
{
    m_only_one_assistant = assistant;
}

void KisToolFreehand::setSnapEraser(bool assistant)
{
    m_eraser_snapping = assistant;
}

void KisToolFreehand::slotDoResizeBrush(qreal newSize)
{
    KisPaintOpSettingsSP settings = currentPaintOpPreset()->settings();

    settings->setPaintOpSize(newSize);
    requestUpdateOutline(m_initialGestureDocPoint, 0);

}

QPointF KisToolFreehand::adjustPosition(const QPointF& point, const QPointF& strokeBegin)
{
    if (m_assistant && static_cast<KisCanvas2*>(canvas())->paintingAssistantsDecoration()) {
        KisCanvas2* c = static_cast<KisCanvas2*>(canvas());
        c->paintingAssistantsDecoration()->setOnlyOneAssistantSnap(m_only_one_assistant);
        c->paintingAssistantsDecoration()->setEraserSnap(m_eraser_snapping);
        QPointF ap = c->paintingAssistantsDecoration()->adjustPosition(point, strokeBegin);
        QPointF fp = (1.0 - m_magnetism) * point + m_magnetism * ap;
        // Report the final position back to the assistant so the guides
        // can follow the brush
        c->paintingAssistantsDecoration()->setAdjustedBrushPosition(fp);
        return fp;
    }
    return point;
}

qreal KisToolFreehand::calculatePerspective(const QPointF &documentPoint)
{
    qreal perspective = 1.0;
    Q_FOREACH (const KisPaintingAssistantSP assistant, static_cast<KisCanvas2*>(canvas())->paintingAssistantsDecoration()->assistants()) {
        QPointer<KisAbstractPerspectiveGrid> grid = dynamic_cast<KisAbstractPerspectiveGrid*>(assistant.data());
        if (grid && grid->isActive() && grid->contains(documentPoint)) {
            perspective = grid->distance(documentPoint);
            break;
        }
    }
    return perspective;
}

void KisToolFreehand::updateMaskSyntheticEventsFromTouch()
{
    setMaskSyntheticEvents(KisConfig(true).disableTouchOnCanvas());
}

void KisToolFreehand::explicitUpdateOutline()
{
    requestUpdateOutline(m_outlineDocPoint, 0);
}

KisOptimizedBrushOutline KisToolFreehand::getOutlinePath(const QPointF &documentPos,
                                             const KoPointerEvent *event,
                                             KisPaintOpSettings::OutlineMode outlineMode)
{
    if (currentPaintOpPreset())
        return m_helper->paintOpOutline(convertToPixelCoord(documentPos),
                                        event,
                                        currentPaintOpPreset()->settings(),
                                        outlineMode);
    else
        return KisOptimizedBrushOutline();
}


