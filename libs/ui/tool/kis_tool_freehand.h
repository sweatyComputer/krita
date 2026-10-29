/*
 *  SPDX-FileCopyrightText: 2003-2008 Boudewijn Rempt <boud@valdyas.org>
 *  SPDX-FileCopyrightText: 2026 Ayanami Kaine <personal@ayanamikaine.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOOL_FREEHAND_H_
#define KIS_TOOL_FREEHAND_H_

#include <brushengine/kis_paint_information.h>
#include <brushengine/kis_paintop_settings.h>
#include <kis_distance_information.h>

#include <Qt>

#include "kis_types.h"
#include "kis_tool_paint.h"
#include "kis_smoothing_options.h"
#include "kis_signal_compressor_with_param.h"
#include "KoPointerEvent.h"

#include "kritaui_export.h"

class KoPointerEvent;
class KoCanvasBase;

class KisPaintingInformationBuilder;
class KisToolFreehandHelper;


class KRITAUI_EXPORT KisToolFreehand : public KisToolPaint
{

    Q_OBJECT

public:
    KisToolFreehand(KoCanvasBase * canvas, const QCursor & cursor, const KUndo2MagicString &transactionText,
                    bool useSavedSmoothing = true);
    ~KisToolFreehand() override;
    int flags() const override;
    void mouseMoveEvent(KoPointerEvent *event) override;
    

public Q_SLOTS:
    void activate(const QSet<KoShape*> &shapes) override;
    void deactivate() override;

protected:
    bool trySampleByPaintOp(KoPointerEvent *event, AlternateAction action);

    bool primaryActionSupportsHiResEvents() const override;
    void beginPrimaryAction(KoPointerEvent *event) override;
    void continuePrimaryAction(KoPointerEvent *event) override;
    void endPrimaryAction(KoPointerEvent *event) override;

    void activateAlternateAction(AlternateAction action) override;
    void deactivateAlternateAction(AlternateAction action) override;

    void beginAlternateAction(KoPointerEvent *event, AlternateAction action) override;
    void continueAlternateAction(KoPointerEvent *event, AlternateAction action) override;
    void endAlternateAction(KoPointerEvent *event, AlternateAction action) override;

    bool wantsAutoScroll() const override;


    virtual void initStroke(KoPointerEvent *event);
    virtual void doStroke(KoPointerEvent *event);
    virtual void endStroke();



    KisOptimizedBrushOutline getOutlinePath(const QPointF &documentPos,
                                            const KoPointerEvent *event,
                                            KisPaintOpSettings::OutlineMode outlineMode) override;


    KisPaintingInformationBuilder* paintingInformationBuilder() const;
    void resetHelper(KisToolFreehandHelper *helper);

    bool supportsPaintingAssistants() const override;

protected Q_SLOTS:

    void explicitUpdateOutline();
    void resetCursorStyle() override;
    void setAssistant(bool assistant);
    void setOnlyOneAssistantSnap(bool assistant);
    void setSnapEraser(bool assistant);
    void slotDoResizeBrush(qreal newSize);

private:
    friend class KisToolFreehandPaintingInformationBuilder;

    /**
     * Adjusts a coordinates according to a KisPaintingAssistant,
     * if available.
     */
    QPointF adjustPosition(const QPointF& point, const QPointF& strokeBegin);
    void updateTemporaryStraightRulerPreview(const QPointF &documentPoint, Qt::KeyboardModifiers modifiers);
    void lockTemporaryStraightRuler(const QPointF &documentPoint, Qt::KeyboardModifiers modifiers);
    bool temporaryStraightRulerActive() const;
    QPointF temporaryStraightRulerOutlinePosition(const QPointF &documentPoint);

    /**
     * Calculates a coefficient for KisPaintInformation
     * according to perspective grid values
     */
    qreal calculatePerspective(const QPointF &documentPoint);

private Q_SLOTS:
    void updateMaskSyntheticEventsFromTouch();

protected:
    friend class KisViewManager;
    friend class KisView;
    KisSmoothingOptionsSP smoothingOptions() const;
    bool m_assistant {false};
    double m_magnetism {1.0};
    bool m_only_one_assistant {true};
    bool m_eraser_snapping {false};

private:
    KisPaintingInformationBuilder *m_infoBuilder {nullptr};
    KisToolFreehandHelper *m_helper {nullptr};

    QPointF m_initialGestureDocPoint;
    QPointF m_lastDocumentPoint;
    qreal m_lastPaintOpSize {0.0};
    qreal m_lastPaintOpacity {1.0};

    // AdjustBrush commits to an axis once the drag clears the dead zone, so a
    // wobbly finger cannot flip you from size to opacity mid-stroke.
    bool m_adjustAxisResolved {false};
    bool m_adjustAxisVertical {false};

    // How much of the screen's width a drag must cover to sweep opacity from 0
    // to 1. Read from kritarc at the start of each gesture so it can be tuned
    // without a rebuild.
    qreal m_opacityDragFraction {0.25};

    // Scroll-tracker feel, mirroring Shortcut Composer's defaults:
    // pixels_in_unit = 50 / sensitivity_scale, so 25px per size unit and
    // 5px per 1% of opacity. Both read from kritarc at gesture start.
    qreal m_scrollSizePixels {25.0};
    qreal m_scrollOpacityPixels {5.0};
    QPoint m_initialGestureGlobalPoint;

    // Where the AdjustBrush size/opacity indicator is drawn. On a touch
    // screen the gesture's own position is under the hand, which hides the
    // very thing the gesture exists to show. Read from kritarc
    // [AdjustBrushGesture] at gesture start:
    //   indicatorAnchor    = above (default) | centre | touch
    //   indicatorClearance = gap in widget px between the touch point and the
    //                        bottom of the circle, for "above" (default 90)
    enum IndicatorAnchor { AnchorTouch, AnchorAbove, AnchorCentre };
    IndicatorAnchor m_indicatorAnchor {AnchorAbove};
    qreal m_indicatorClearance {90.0};
    AlternateAction m_gestureAction {NONE};

    // Once "above" runs out of room and falls back to the canvas centre, it
    // stays there for the rest of the gesture. Without this, a brush resized
    // right at the boundary would flick between the two spots every frame.
    bool m_indicatorFellBack {false};

    /// The document point to draw the outline at for the running gesture.
    /// Anything other than AdjustBrush keeps the original behaviour, so the
    /// desktop Shift+drag resize still draws at the cursor.
    QPointF gestureOutlineDocPoint();

    bool m_paintopBasedSamplingInAction {false};
    KisSignalCompressorWithParam<qreal> m_brushResizeCompressor;

    std::optional<KoPointerEventWrapper> m_beginAlternateActionEvent;
};



#endif // KIS_TOOL_FREEHAND_H_
