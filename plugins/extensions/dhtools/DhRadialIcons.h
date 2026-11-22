/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DHRADIALICONS_H
#define DHRADIALICONS_H

#include <QIcon>
#include <QString>
#include <QStringList>

/**
 * Resolves an item's icon spec to a QIcon. Ported from the desktop plugin's
 * icons.py, same sources in the same order:
 *
 *   "dh:brush"        one of the SVGs bundled with this plugin
 *   "theme:edit-copy" a Qt/Breeze theme icon
 *   "krita:<id>"      the icon Krita already attached to that action
 *   "/path/to.png"    any image file
 *   "brush"           bare name: bundled, then theme, then Krita action
 *
 * AUTO MODE is the point of it. An item whose action is
 * "krita:add_new_paint_layer" already names a command Krita has drawn an icon
 * for, so with design.auto_icons on we borrow it and the whole menu gets
 * theme-matched icons with no configuration.
 *
 * THE X-IN-A-BOX PROBLEM (read before simplifying any of this):
 * QIcon::fromTheme() on a missing name does NOT return a null icon. Depending
 * on the platform theme it hands back the theme's own "image-missing"
 * graphic - the classic X in a box - and isNull() is false for it, so a naive
 * check happily paints a broken-image glyph onto a button. Every theme lookup
 * here is guarded by hasThemeIcon() first, and every result must report
 * available sizes. A rejected icon resolves to a null QIcon, and a null icon
 * means the button just draws its label. A missing icon should look like a
 * decision, never like a bug.
 */
namespace DhRadialIcons
{
    /**
     * @param spec    the item's "icon" value; may be empty
     * @param actionId the item's action, used for auto mode when spec is empty
     * @param autoIcons whether to fall back to the action's own icon
     */
    QIcon resolve(const QString &spec, const QString &actionId, bool autoIcons);

    /// Every icon name in Krita's built-in library (the compiled-in
    /// ":/dark_x.svg" / ":/16_light_x.svg" resources), prefixes stripped, so
    /// each is what KisIconUtils::loadIcon() / a "kicon:" spec wants. Sorted.
    QStringList kritaLibraryNames();

    /// Names of the SVGs bundled in dhtools.qrc (":/dhtools/icons"), sorted.
    QStringList bundledNames();
}

#endif // DHRADIALICONS_H
