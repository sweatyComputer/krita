/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DHBRUSHHUD_H
#define DHBRUSHHUD_H

#include <QColor>
#include <QElapsedTimer>
#include <QPointF>
#include <QTimer>

#include <kis_canvas_decoration.h>
#include <brushengine/KisOptimizedBrushOutline.h>

/**
 * On-canvas readout for brush size and opacity.
 *
 * Deliberately a KisCanvasDecoration listening to the canvas resource manager
 * rather than something bolted onto the Android gesture path. The resource
 * manager is where every route to "the brush got bigger" converges -- three
 * finger drag, Shift+drag, the toolbar slider, a shortcut, another plugin -- so
 * hanging the HUD off it means one implementation serves all of them, on
 * desktop as well as Android, with nothing platform-specific in here.
 *
 * Size draws a brush-sized ring. Opacity draws that ring filled with the
 * foreground colour at the actual alpha, which is the Photoshop behaviour: you
 * judge the value by looking at it rather than by reading a number.
 */
class DhBrushHud : public KisCanvasDecoration
{
    Q_OBJECT
public:
    static const QString ID;

    explicit DhBrushHud(QPointer<KisView> view);
    ~DhBrushHud() override;

    /// Re-read kritarc [DhBrushHud].
    void reloadSettings();

protected:
    void drawDecoration(QPainter &gc, const QRectF &updateArea,
                        const KisCoordinatesConverter *converter,
                        KisCanvas2 *canvas) override;

private Q_SLOTS:
    void slotCanvasResourceChanged(int key, const QVariant &value);
    void slotFadeTick();

private:
    enum Readout { NoReadout, SizeReadout, OpacityReadout };

    void trigger(Readout what);
    void recomputeOutline();
    qreal currentAlpha() const;
    QPointF anchorPoint(KisCanvas2 *canvas) const;

    Readout m_showing{NoReadout};
    QElapsedTimer m_since;
    QTimer m_repaint;

    qreal m_size{0.0};
    qreal m_opacity{1.0};

    // The real paintop outline, in image pixels centred on the origin. Computed
    // when the brush actually changes, not per frame -- this is engine code and
    // the fade repaints ~30 times a second.
    KisOptimizedBrushOutline m_outline;

    bool m_useBrushOutline{true};
    bool m_enabled{true};
    bool m_showForSize{true};
    bool m_showForOpacity{true};
    int m_holdMs{450};
    int m_fadeMs{350};

    // Set false once the first resource change arrives, so simply opening a
    // document does not flash the HUD at you.
    bool m_priming{true};
};

#endif // DHBRUSHHUD_H
