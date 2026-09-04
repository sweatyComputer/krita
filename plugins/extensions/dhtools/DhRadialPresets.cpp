/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "DhRadialPresets.h"

QStringList DhRadialPresets::names()
{
    return QStringList()
        << QStringLiteral("Midnight")
        << QStringLiteral("Ink")
        << QStringLiteral("Marking")
        << QStringLiteral("Neon")
        << QStringLiteral("Daylight")
        << QStringLiteral("Chunky Circles");
}

DhRadialDesign DhRadialPresets::apply(const QString &name)
{
    DhRadialDesign d;   // Midnight = the plain defaults

    if (name == QLatin1String("Ink")) {
        d.buttonColor       = QColor(0x1B, 0x1B, 0x1B, 0xEE);
        d.buttonHoverColor  = QColor(0xE8, 0xE8, 0xE8);
        d.buttonActiveColor = QColor(0x8A, 0x8A, 0x8A);
        d.borderColor       = QColor(0x3A, 0x3A, 0x3A, 0xC8);
        d.borderHoverColor  = QColor(0xFF, 0xFF, 0xFF);
        d.textColor         = QColor(0xB4, 0xB4, 0xB4);
        d.textHoverColor    = QColor(0x10, 0x10, 0x10);
        d.centerColor       = QColor(0x10, 0x10, 0x10, 0xEE);
        d.centerBorderColor = QColor(0x3A, 0x3A, 0x3A);
        d.trailColor        = QColor(0xFF, 0xFF, 0xFF, 0x55);
        d.tintStrength      = 0.0;
        d.cornerRadius      = 3.0;

    } else if (name == QLatin1String("Marking")) {
        // Maya-flavoured: flat, tight, no hub, pure flick.
        d.buttonColor      = QColor(0x1E, 0x1E, 0x1E, 0xF0);
        d.buttonHoverColor = QColor(0x52, 0x85, 0xC5);
        d.borderColor      = QColor(0, 0, 0, 0);
        d.borderHoverColor = QColor(0x8F, 0xB6, 0xE8);
        d.cornerRadius     = 2.0;
        d.borderWidth      = 1.0;
        d.pieRadius        = 104;
        d.buttonHeight     = 30;
        d.buttonWidth      = 104;
        d.fontSize         = 9;
        d.showCenter       = false;
        d.centerRadius     = 30;
        d.glowEnabled      = false;
        d.tintStrength     = 0.0;
        d.trailColor       = QColor(0xFF, 0xFF, 0xFF, 0x88);
        d.drawerCellHeight   = 26;
        d.dropdownCellHeight = 24;
        d.panelGap         = 4;
        d.sproutShape      = QStringLiteral("RECTANGLE");

    } else if (name == QLatin1String("Neon")) {
        d.buttonColor       = QColor(0x12, 0x14, 0x1C, 0xF2);
        d.buttonHoverColor  = QColor(0x00, 0xE5, 0xB0);
        d.buttonActiveColor = QColor(0x00, 0x80, 0x6A);
        d.borderColor       = QColor(0x00, 0xE5, 0xB0, 0x55);
        d.borderHoverColor  = QColor(0x7C, 0xFF, 0xE2);
        d.textColor         = QColor(0x8F, 0xA3, 0xA0);
        d.textHoverColor    = QColor(0x06, 0x20, 0x18);
        d.centerColor       = QColor(0x0A, 0x0C, 0x12, 0xF2);
        d.centerBorderColor = QColor(0x00, 0xE5, 0xB0, 0x77);
        d.centerTextColor   = QColor(0x8C, 0xFF, 0xE0);
        d.trailColor        = QColor(0x00, 0xE5, 0xB0, 0xAA);
        d.glowColor         = QColor(0x00, 0xE5, 0xB0, 0x3A);
        d.glowSize          = 22.0;
        d.cornerRadius      = 12.0;
        d.tintStrength      = 0.18;

    } else if (name == QLatin1String("Daylight")) {
        d.buttonColor       = QColor(0xF2, 0xF3, 0xF6, 0xFA);
        d.buttonHoverColor  = QColor(0x3C, 0x7D, 0xD9);
        d.buttonActiveColor = QColor(0xA8, 0xC6, 0xEE);
        d.borderColor       = QColor(0xC3, 0xC7, 0xD2);
        d.borderHoverColor  = QColor(0x2A, 0x5F, 0xA8);
        d.textColor         = QColor(0x3A, 0x3F, 0x4B);
        d.textHoverColor    = QColor(0xFF, 0xFF, 0xFF);
        d.centerColor       = QColor(0xFF, 0xFF, 0xFF, 0xF7);
        d.centerBorderColor = QColor(0xC3, 0xC7, 0xD2);
        d.centerTextColor   = QColor(0x3A, 0x3F, 0x4B);
        d.trailColor        = QColor(0, 0, 0, 0x55);
        d.shadowColor       = QColor(0, 0, 0, 0x22);
        d.glowColor         = QColor(0, 0, 0, 0x22);
        d.tintStrength      = 0.16;

    } else if (name == QLatin1String("Chunky Circles")) {
        // Icon-forward: big round buttons on a wide ring, labels under them.
        d.buttonShape     = DhRadialDesign::Circle;
        d.circleDiameter  = 76;
        d.pieRadius       = 150;
        d.iconSide        = DhRadialDesign::IconTop;
        d.iconSize        = 26;
        d.fontSize        = 9;
        d.cornerRadius    = 10.0;
        d.tintStrength    = 0.35;
        d.glowSize        = 20.0;
        d.gridCellWidth   = 60;
        d.gridCellHeight  = 56;
    }

    return d;
}
