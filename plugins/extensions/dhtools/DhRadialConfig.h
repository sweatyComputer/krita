/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DHRADIALCONFIG_H
#define DHRADIALCONFIG_H

#include <QColor>
#include <QList>
#include <QSharedPointer>
#include <QJsonObject>
#include <QString>
#include <QMetaType>

#include "DhRadialDesign.h"

class DhRadialItem;
typedef QSharedPointer<DhRadialItem> DhRadialItemSP;
Q_DECLARE_METATYPE(DhRadialItemSP)

/**
 * One wedge of the radial menu. Either it runs an action or it opens a
 * submenu; an item with children ignores its action.
 */
class DhRadialItem
{
public:
    /// How a slot with children reveals them. Mirrors the desktop plugin.
    enum SlotLayout {
        Submenu,   ///< replaces the ring with the children's own ring
        Grid,      ///< children laid out inline as a packed grid
        Drawer,    ///< children slide out of the parent in a strip (or grid)
        Dropdown,  ///< a drawer that always opens below the parent, edge-aligned
        Sprout     ///< children fan out around the parent on hover
    };

    QString label;
    QString action;   ///< action objectName, or its menu text as a fallback
    QColor color;     ///< invalid means "pick from the default palette"
    QList<DhRadialItemSP> children;

    // ----- v2 presentation keys -------------------------------------------
    QString icon;                 ///< "" = borrow the Krita action's own icon
    QString tooltip;
    QString shape;                ///< "" = inherit design.buttonShape
    SlotLayout slotLayout{Submenu};
    int columns{0};               ///< grid only; 0 = design.gridColumns
    qreal colSpan{1.0};           ///< grid cell width in columns (0.5 / 1 / 2)
    int cellWidth{0};             ///< px override, 0 = auto
    int cellHeight{0};

    // ----- drawer / dropdown only -------------------------------------------
    QString drawerDirection{QStringLiteral("AUTO")};  ///< AUTO|RIGHT|LEFT|UP|DOWN|RADIAL
    QString drawerFlow{QStringLiteral("LINE")};        ///< LINE (a strip) | GRID
    QString drawerAlign{QStringLiteral("AUTO")};       ///< AUTO|START|CENTER|END

    /**
     * The item exactly as it appeared in the file.
     *
     * Saving re-emits this and overwrites only the fields this build actually
     * understands, so editing one wedge on a tablet cannot silently delete
     * icons, grid spans or anything a newer desktop plugin has added. Round-
     * tripping data you do not understand is cheaper than losing it.
     */
    QJsonObject raw;

    bool isSubmenu() const { return !children.isEmpty(); }
};

/**
 * Loads the radial menu layout from JSON, in the same shape the Python plugin
 * used, so an existing config.json can be dropped in unchanged.
 *
 * Lookup order:
 *   1. <writable app data>/dhtools/dh_radial_menu.json  (user's own copy)
 *   2. the copy installed alongside Krita
 *   3. a small built-in fallback, so the menu is never empty
 */
namespace DhRadialConfig
{
    /**
     * Load the menu. Handles both the v1 schema ({"items": [...]}) and v2
     * (adds a "design" block and per-item presentation keys) - a v1 file just
     * picks up defaults, exactly as the desktop plugin's migration does.
     *
     * @param design filled in from the "design" block when non-null.
     */
    QList<DhRadialItemSP> load(DhRadialDesign *design = nullptr);

    /// The layout Krita ships with, for "Reset to Defaults".
    QList<DhRadialItemSP> loadDefaults();

    /// Write a layout back out. @p design, when non-null, replaces the saved
    /// design block; when null the existing block on disk is preserved
    /// verbatim, which is what a build with no design editor wants.
    /// @p error is filled in on failure.
    bool save(const QList<DhRadialItemSP> &items, const DhRadialDesign *design = nullptr,
              QString *error = nullptr);

    /// Absolute path the user's own config is read from and written to.
    QString userConfigPath();

    /// Deep copy, so an editor can work on a scratch tree and discard it.
    QList<DhRadialItemSP> clone(const QList<DhRadialItemSP> &items);
}

#endif // DHRADIALCONFIG_H
