/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DHRADIALDESIGN_H
#define DHRADIALDESIGN_H

#include <QColor>
#include <QString>

class QJsonObject;

/**
 * The radial menu's whole look, as data.
 *
 * A direct port of design.py from the desktop plugin, field for field, so one
 * config.json drives both. Authoring happens on desktop where the settings
 * dialog lives; the tablet reads the same file.
 *
 * The two sizing knobs are deliberately separate, and this is the point of the
 * whole redesign:
 *
 *   uiScale   multiplies everything - radius, buttons, fonts, icons. "Make the
 *             menu bigger on this screen."
 *   pieRadius is the distance from the centre to the INNER edge of the ring,
 *             on its own. "Push the buttons further out so they breathe."
 *
 * Conflating them is what a wedge-based pie forces on you: radius and button
 * size are the same number, so spreading things out fattens them. Discrete
 * buttons placed at a radius separate the two.
 */
struct DhRadialDesign
{
    enum SliceStyle { Buttons, Ring };
    enum ButtonShape { Rectangle, Circle };
    enum RingMode { Uniform, Compact };
    enum IconSide { IconLeft, IconTop, IconOnly };
    enum PanelTrigger { PanelHover, PanelClick };

    // ----- overall style ---------------------------------------------------
    SliceStyle sliceStyle{Buttons};
    qreal uiScale{1.0};

    // ----- pie geometry ----------------------------------------------------
    int pieRadius{128};          ///< centre -> INNER edge of the ring
    int buttonWidth{118};
    bool autoWidth{true};
    int buttonHeight{38};
    ButtonShape buttonShape{Rectangle};
    int circleDiameter{62};
    bool circleCaption{true};
    qreal cornerRadius{8.0};
    qreal borderWidth{1.4};
    int itemSpacing{5};
    RingMode ringMode{Uniform};
    int gridColumns{2};
    int gridCellWidth{96};
    int gridCellHeight{32};
    bool gridHeader{true};
    int slotPadding{12};

    // ----- ring style ------------------------------------------------------
    int ringInnerRadius{62};
    int ringOuterRadius{160};
    qreal ringGapDeg{2.0};

    // ----- panels: drawers, dropdowns and sprouts (children revealed on
    // demand). All three share the same open / dwell / close behaviour; only
    // where the children land differs. -------------------------------------
    PanelTrigger panelTrigger{PanelHover};
    int panelDelayMs{80};        ///< hover dwell before a drawer/dropdown opens
    int panelGap{8};             ///< px between the parent button and the panel
    bool panelArrow{true};       ///< draw a direction glyph on the parent

    int drawerCellWidth{0};      ///< 0 = fit each item's own label
    int drawerCellHeight{30};
    int dropdownCellWidth{0};    ///< 0 = one shared width, fitted to the widest
    int dropdownCellHeight{28};

    // Sprout: children land on an arc around the parent rather than a strip.
    QString sproutShape{QStringLiteral("CIRCLE")};
    int sproutRadius{78};        ///< parent centre -> satellite centre
    int sproutSize{46};          ///< satellite diameter
    qreal sproutSpreadDeg{104.0};///< total arc the fan occupies
    int sproutMaxFan{5};         ///< more children than this fall back to a sub-ring
    int sproutDelayMs{80};       ///< hover dwell before it opens (0 = instant)
    bool sproutConnectors{false};///< hairlines from the parent to each satellite

    // ----- centre hub ------------------------------------------------------
    bool showCenter{true};
    int centerRadius{44};
    bool centerLabel{true};

    // ----- pointer feedback ------------------------------------------------
    bool showTrail{false};
    qreal trailWidth{1.6};
    int deadZone{24};
    bool flickSelect{true};

    // ----- text ------------------------------------------------------------
    QString fontFamily{QStringLiteral("Segoe UI")};
    int fontSize{10};
    int centerFontSize{10};
    bool textShadow{true};

    // ----- icons -----------------------------------------------------------
    int iconSize{18};
    IconSide iconSide{IconLeft};
    qreal iconOpacity{1.0};
    bool autoIcons{true};

    // ----- colours ---------------------------------------------------------
    QColor bgColor{QColor(0, 0, 0, 0)};
    QColor buttonColor{QColor(0x26, 0x2A, 0x34, 0xE6)};
    QColor buttonHoverColor{QColor(0x4A, 0x8F, 0xD4)};
    QColor buttonActiveColor{QColor(0x2D, 0x5A, 0x9E)};
    QColor borderColor{QColor(0x46, 0x4C, 0x5C, 0xBE)};
    QColor borderHoverColor{QColor(0x9C, 0xC4, 0xF0)};
    QColor textColor{QColor(0xAF, 0xB4, 0xC3)};
    QColor textHoverColor{QColor(0xFF, 0xFF, 0xFF)};
    QColor centerColor{QColor(0x16, 0x19, 0x20, 0xE6)};
    QColor centerBorderColor{QColor(0x3C, 0x41, 0x50, 0xC8)};
    QColor centerTextColor{QColor(0xD2, 0xD7, 0xE1)};
    QColor trailColor{QColor(0xFF, 0xFF, 0xFF, 0x66)};
    QColor shadowColor{QColor(0, 0, 0, 0x55)};

    /// How strongly an item's own colour tints its resting fill. 0..1.
    qreal tintStrength{0.28};

    // ----- hover glow and open animation ------------------------------------
    bool glowEnabled{true};
    QColor glowColor{QColor(0, 0, 0, 0xAA)};
    qreal glowSize{14.0};
    bool openAnimation{true};
    int animMs{110};

    /// Read a "design" block. Unknown keys are ignored, missing keys keep the
    /// default, so a config written by a newer desktop plugin still loads.
    void read(const QJsonObject &object);

    /// Write every field back out, in the same key spelling as the desktop
    /// plugin's design.py, so a design saved here loads there unchanged.
    void write(QJsonObject &object) const;

    // Everything below is uiScale already applied, which is what painting wants.
    qreal scaled(qreal value) const { return value * uiScale; }
    int scaledInt(qreal value) const { return qRound(value * uiScale); }
};

#endif // DHRADIALDESIGN_H
