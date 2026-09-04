/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "DhRadialDesign.h"

#include <QJsonObject>
#include <QJsonValue>

namespace {

/**
 * Colours are "#RRGGBB" or "#RRGGBBAA" in the JSON - readable by eye and
 * round-tripped losslessly by a colour picker. QColor::setNamedColor only
 * understands #AARRGGBB, so the alpha has to be moved by hand.
 */
QColor readColour(const QJsonObject &object, const QString &key, const QColor &fallback)
{
    const QString text = object.value(key).toString();
    if (text.isEmpty() || !text.startsWith(QLatin1Char('#'))) return fallback;

    const QString digits = text.mid(1);
    bool ok = false;

    if (digits.size() == 8) {
        const uint value = digits.toUInt(&ok, 16);
        if (!ok) return fallback;
        return QColor(int((value >> 24) & 0xFF), int((value >> 16) & 0xFF),
                      int((value >> 8) & 0xFF), int(value & 0xFF));
    }

    if (digits.size() == 6) {
        const uint value = digits.toUInt(&ok, 16);
        if (!ok) return fallback;
        return QColor(int((value >> 16) & 0xFF), int((value >> 8) & 0xFF),
                      int(value & 0xFF));
    }

    const QColor named(text);
    return named.isValid() ? named : fallback;
}

int readInt(const QJsonObject &o, const QString &key, int fallback)
{
    return o.contains(key) ? o.value(key).toInt(fallback) : fallback;
}

qreal readReal(const QJsonObject &o, const QString &key, qreal fallback)
{
    return o.contains(key) ? o.value(key).toDouble(fallback) : fallback;
}

bool readBool(const QJsonObject &o, const QString &key, bool fallback)
{
    return o.contains(key) ? o.value(key).toBool(fallback) : fallback;
}

QString readEnum(const QJsonObject &o, const QString &key)
{
    return o.value(key).toString().toUpper();
}

} // namespace

void DhRadialDesign::read(const QJsonObject &object)
{
    if (object.isEmpty()) return;

    const QString style = readEnum(object, QStringLiteral("slice_style"));
    if (style == QLatin1String("RING"))    sliceStyle = Ring;
    if (style == QLatin1String("BUTTONS")) sliceStyle = Buttons;

    uiScale = qBound(0.25, readReal(object, QStringLiteral("ui_scale"), uiScale), 4.0);

    pieRadius     = readInt(object, QStringLiteral("pie_radius"), pieRadius);
    buttonWidth   = readInt(object, QStringLiteral("button_width"), buttonWidth);
    autoWidth     = readBool(object, QStringLiteral("auto_width"), autoWidth);
    buttonHeight  = readInt(object, QStringLiteral("button_height"), buttonHeight);
    circleDiameter = readInt(object, QStringLiteral("circle_diameter"), circleDiameter);
    circleCaption = readBool(object, QStringLiteral("circle_caption"), circleCaption);
    cornerRadius  = readReal(object, QStringLiteral("corner_radius"), cornerRadius);
    borderWidth   = readReal(object, QStringLiteral("border_width"), borderWidth);
    itemSpacing   = readInt(object, QStringLiteral("item_spacing"), itemSpacing);
    gridColumns   = readInt(object, QStringLiteral("grid_columns"), gridColumns);
    gridCellWidth = readInt(object, QStringLiteral("grid_cell_width"), gridCellWidth);
    gridCellHeight = readInt(object, QStringLiteral("grid_cell_height"), gridCellHeight);
    gridHeader    = readBool(object, QStringLiteral("grid_header"), gridHeader);
    slotPadding   = readInt(object, QStringLiteral("slot_padding"), slotPadding);

    const QString shape = readEnum(object, QStringLiteral("button_shape"));
    if (shape == QLatin1String("CIRCLE"))    buttonShape = Circle;
    if (shape == QLatin1String("RECTANGLE")) buttonShape = Rectangle;

    const QString ring = readEnum(object, QStringLiteral("ring_mode"));
    if (ring == QLatin1String("COMPACT")) ringMode = Compact;
    if (ring == QLatin1String("UNIFORM")) ringMode = Uniform;

    ringInnerRadius = readInt(object, QStringLiteral("ring_inner_radius"), ringInnerRadius);
    ringOuterRadius = readInt(object, QStringLiteral("ring_outer_radius"), ringOuterRadius);
    ringGapDeg      = readReal(object, QStringLiteral("ring_gap_deg"), ringGapDeg);

    const QString trigger = readEnum(object, QStringLiteral("panel_trigger"));
    if (trigger == QLatin1String("HOVER")) panelTrigger = PanelHover;
    if (trigger == QLatin1String("CLICK")) panelTrigger = PanelClick;
    panelDelayMs = readInt(object, QStringLiteral("panel_delay_ms"), panelDelayMs);
    panelGap     = readInt(object, QStringLiteral("panel_gap"), panelGap);
    panelArrow   = readBool(object, QStringLiteral("panel_arrow"), panelArrow);

    drawerCellWidth    = readInt(object, QStringLiteral("drawer_cell_width"), drawerCellWidth);
    drawerCellHeight   = readInt(object, QStringLiteral("drawer_cell_height"), drawerCellHeight);
    dropdownCellWidth  = readInt(object, QStringLiteral("dropdown_cell_width"), dropdownCellWidth);
    dropdownCellHeight = readInt(object, QStringLiteral("dropdown_cell_height"), dropdownCellHeight);

    sproutShape      = object.value(QStringLiteral("sprout_shape")).toString(sproutShape).toUpper();
    sproutRadius     = readInt(object, QStringLiteral("sprout_radius"), sproutRadius);
    sproutSize       = readInt(object, QStringLiteral("sprout_size"), sproutSize);
    sproutSpreadDeg  = readReal(object, QStringLiteral("sprout_spread_deg"), sproutSpreadDeg);
    sproutMaxFan     = readInt(object, QStringLiteral("sprout_max_fan"), sproutMaxFan);
    sproutDelayMs    = readInt(object, QStringLiteral("sprout_delay_ms"), sproutDelayMs);
    sproutConnectors = readBool(object, QStringLiteral("sprout_connectors"), sproutConnectors);

    showCenter   = readBool(object, QStringLiteral("show_center"), showCenter);
    centerRadius = readInt(object, QStringLiteral("center_radius"), centerRadius);
    centerLabel  = readBool(object, QStringLiteral("center_label"), centerLabel);

    showTrail   = readBool(object, QStringLiteral("show_trail"), showTrail);
    trailWidth  = readReal(object, QStringLiteral("trail_width"), trailWidth);
    deadZone    = readInt(object, QStringLiteral("dead_zone"), deadZone);
    flickSelect = readBool(object, QStringLiteral("flick_select"), flickSelect);

    fontFamily     = object.value(QStringLiteral("font_family")).toString(fontFamily);
    fontSize       = readInt(object, QStringLiteral("font_size"), fontSize);
    centerFontSize = readInt(object, QStringLiteral("center_font_size"), centerFontSize);
    textShadow     = readBool(object, QStringLiteral("text_shadow"), textShadow);

    iconSize    = readInt(object, QStringLiteral("icon_size"), iconSize);
    iconOpacity = readReal(object, QStringLiteral("icon_opacity"), iconOpacity);
    autoIcons   = readBool(object, QStringLiteral("auto_icons"), autoIcons);

    const QString side = readEnum(object, QStringLiteral("icon_side"));
    if (side == QLatin1String("LEFT")) iconSide = IconLeft;
    if (side == QLatin1String("TOP"))  iconSide = IconTop;
    if (side == QLatin1String("ONLY")) iconSide = IconOnly;

    bgColor           = readColour(object, QStringLiteral("bg_color"), bgColor);
    buttonColor       = readColour(object, QStringLiteral("button_color"), buttonColor);
    buttonHoverColor  = readColour(object, QStringLiteral("button_hover_color"), buttonHoverColor);
    buttonActiveColor = readColour(object, QStringLiteral("button_active_color"), buttonActiveColor);
    borderColor       = readColour(object, QStringLiteral("border_color"), borderColor);
    borderHoverColor  = readColour(object, QStringLiteral("border_hover_color"), borderHoverColor);
    textColor         = readColour(object, QStringLiteral("text_color"), textColor);
    textHoverColor    = readColour(object, QStringLiteral("text_hover_color"), textHoverColor);
    centerColor       = readColour(object, QStringLiteral("center_color"), centerColor);
    centerBorderColor = readColour(object, QStringLiteral("center_border_color"), centerBorderColor);
    centerTextColor   = readColour(object, QStringLiteral("center_text_color"), centerTextColor);
    trailColor        = readColour(object, QStringLiteral("trail_color"), trailColor);
    shadowColor       = readColour(object, QStringLiteral("shadow_color"), shadowColor);

    tintStrength = qBound(0.0, readReal(object, QStringLiteral("tint_strength"), tintStrength), 1.0);

    glowEnabled  = readBool(object, QStringLiteral("glow_enabled"), glowEnabled);
    glowColor    = readColour(object, QStringLiteral("glow_color"), glowColor);
    glowSize     = readReal(object, QStringLiteral("glow_size"), glowSize);
    openAnimation = readBool(object, QStringLiteral("open_animation"), openAnimation);
    animMs        = readInt(object, QStringLiteral("anim_ms"), animMs);
}

namespace {
QString writeColour(const QColor &c)
{
    // #RRGGBBAA - the same spelling design.py reads and writes, so a file
    // saved here needs no translation to open on desktop.
    return QStringLiteral("#%1%2%3%4")
        .arg(c.red(),   2, 16, QLatin1Char('0'))
        .arg(c.green(), 2, 16, QLatin1Char('0'))
        .arg(c.blue(),  2, 16, QLatin1Char('0'))
        .arg(c.alpha(), 2, 16, QLatin1Char('0'));
}
} // namespace

void DhRadialDesign::write(QJsonObject &object) const
{
    object.insert(QStringLiteral("slice_style"), sliceStyle == Ring ? QStringLiteral("RING") : QStringLiteral("BUTTONS"));
    object.insert(QStringLiteral("ui_scale"), uiScale);

    object.insert(QStringLiteral("pie_radius"), pieRadius);
    object.insert(QStringLiteral("button_width"), buttonWidth);
    object.insert(QStringLiteral("auto_width"), autoWidth);
    object.insert(QStringLiteral("button_height"), buttonHeight);
    object.insert(QStringLiteral("button_shape"), buttonShape == Circle ? QStringLiteral("CIRCLE") : QStringLiteral("RECTANGLE"));
    object.insert(QStringLiteral("circle_diameter"), circleDiameter);
    object.insert(QStringLiteral("circle_caption"), circleCaption);
    object.insert(QStringLiteral("corner_radius"), cornerRadius);
    object.insert(QStringLiteral("border_width"), borderWidth);
    object.insert(QStringLiteral("item_spacing"), itemSpacing);
    object.insert(QStringLiteral("ring_mode"), ringMode == Compact ? QStringLiteral("COMPACT") : QStringLiteral("UNIFORM"));
    object.insert(QStringLiteral("grid_columns"), gridColumns);
    object.insert(QStringLiteral("grid_cell_width"), gridCellWidth);
    object.insert(QStringLiteral("grid_cell_height"), gridCellHeight);
    object.insert(QStringLiteral("grid_header"), gridHeader);
    object.insert(QStringLiteral("slot_padding"), slotPadding);

    object.insert(QStringLiteral("ring_inner_radius"), ringInnerRadius);
    object.insert(QStringLiteral("ring_outer_radius"), ringOuterRadius);
    object.insert(QStringLiteral("ring_gap_deg"), ringGapDeg);

    object.insert(QStringLiteral("panel_trigger"), panelTrigger == PanelClick ? QStringLiteral("CLICK") : QStringLiteral("HOVER"));
    object.insert(QStringLiteral("panel_delay_ms"), panelDelayMs);
    object.insert(QStringLiteral("panel_gap"), panelGap);
    object.insert(QStringLiteral("panel_arrow"), panelArrow);
    object.insert(QStringLiteral("drawer_cell_width"), drawerCellWidth);
    object.insert(QStringLiteral("drawer_cell_height"), drawerCellHeight);
    object.insert(QStringLiteral("dropdown_cell_width"), dropdownCellWidth);
    object.insert(QStringLiteral("dropdown_cell_height"), dropdownCellHeight);

    object.insert(QStringLiteral("sprout_shape"), sproutShape);
    object.insert(QStringLiteral("sprout_radius"), sproutRadius);
    object.insert(QStringLiteral("sprout_size"), sproutSize);
    object.insert(QStringLiteral("sprout_spread_deg"), sproutSpreadDeg);
    object.insert(QStringLiteral("sprout_max_fan"), sproutMaxFan);
    object.insert(QStringLiteral("sprout_delay_ms"), sproutDelayMs);
    object.insert(QStringLiteral("sprout_connectors"), sproutConnectors);

    object.insert(QStringLiteral("show_center"), showCenter);
    object.insert(QStringLiteral("center_radius"), centerRadius);
    object.insert(QStringLiteral("center_label"), centerLabel);

    object.insert(QStringLiteral("show_trail"), showTrail);
    object.insert(QStringLiteral("trail_width"), trailWidth);
    object.insert(QStringLiteral("dead_zone"), deadZone);
    object.insert(QStringLiteral("flick_select"), flickSelect);

    object.insert(QStringLiteral("font_family"), fontFamily);
    object.insert(QStringLiteral("font_size"), fontSize);
    object.insert(QStringLiteral("center_font_size"), centerFontSize);
    object.insert(QStringLiteral("text_shadow"), textShadow);

    object.insert(QStringLiteral("icon_size"), iconSize);
    object.insert(QStringLiteral("icon_side"),
                  iconSide == IconTop ? QStringLiteral("TOP")
                  : iconSide == IconOnly ? QStringLiteral("ONLY") : QStringLiteral("LEFT"));
    object.insert(QStringLiteral("icon_opacity"), iconOpacity);
    object.insert(QStringLiteral("auto_icons"), autoIcons);

    object.insert(QStringLiteral("bg_color"), writeColour(bgColor));
    object.insert(QStringLiteral("button_color"), writeColour(buttonColor));
    object.insert(QStringLiteral("button_hover_color"), writeColour(buttonHoverColor));
    object.insert(QStringLiteral("button_active_color"), writeColour(buttonActiveColor));
    object.insert(QStringLiteral("border_color"), writeColour(borderColor));
    object.insert(QStringLiteral("border_hover_color"), writeColour(borderHoverColor));
    object.insert(QStringLiteral("text_color"), writeColour(textColor));
    object.insert(QStringLiteral("text_hover_color"), writeColour(textHoverColor));
    object.insert(QStringLiteral("center_color"), writeColour(centerColor));
    object.insert(QStringLiteral("center_border_color"), writeColour(centerBorderColor));
    object.insert(QStringLiteral("center_text_color"), writeColour(centerTextColor));
    object.insert(QStringLiteral("trail_color"), writeColour(trailColor));
    object.insert(QStringLiteral("shadow_color"), writeColour(shadowColor));

    object.insert(QStringLiteral("tint_strength"), tintStrength);

    object.insert(QStringLiteral("glow_enabled"), glowEnabled);
    object.insert(QStringLiteral("glow_color"), writeColour(glowColor));
    object.insert(QStringLiteral("glow_size"), glowSize);
    object.insert(QStringLiteral("open_animation"), openAnimation);
    object.insert(QStringLiteral("anim_ms"), animMs);
}
