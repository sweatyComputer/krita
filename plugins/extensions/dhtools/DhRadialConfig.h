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
#include <QString>

class DhRadialItem;
typedef QSharedPointer<DhRadialItem> DhRadialItemSP;

/**
 * One wedge of the radial menu. Either it runs an action or it opens a
 * submenu; an item with children ignores its action.
 */
class DhRadialItem
{
public:
    QString label;
    QString action;   ///< action objectName, or its menu text as a fallback
    QColor color;     ///< invalid means "pick from the default palette"
    QList<DhRadialItemSP> children;

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
    QList<DhRadialItemSP> load();

    /// The layout Krita ships with, for "Reset to Defaults".
    QList<DhRadialItemSP> loadDefaults();

    /// Write a layout back out. @p error is filled in on failure.
    bool save(const QList<DhRadialItemSP> &items, QString *error = nullptr);

    /// Absolute path the user's own config is read from and written to.
    QString userConfigPath();

    /// Deep copy, so an editor can work on a scratch tree and discard it.
    QList<DhRadialItemSP> clone(const QList<DhRadialItemSP> &items);
}

#endif // DHRADIALCONFIG_H
