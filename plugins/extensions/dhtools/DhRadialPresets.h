/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DHRADIALPRESETS_H
#define DHRADIALPRESETS_H

#include <QStringList>

#include "DhRadialDesign.h"

/**
 * The six built-in looks from the desktop plugin's design.py, ported field
 * for field. Each preset is a sparse set of overrides on top of the plain
 * DhRadialDesign() defaults - "Midnight" is exactly those defaults, the rest
 * change only what makes them look different.
 *
 * Unlike the desktop version there is nowhere sensible to keep user-authored
 * presets on a tablet (no folder a person browses to drop files into), so
 * only the built-ins are here. The design itself is still saved in full in
 * config.json either way - a preset is just a starting point, never a
 * dependency.
 */
namespace DhRadialPresets
{
    /// Display names, in the same order as the desktop plugin.
    QStringList names();

    /// Defaults, then this preset's overrides. Unknown names return the
    /// plain defaults, same as "Midnight".
    DhRadialDesign apply(const QString &name);
}

#endif // DHRADIALPRESETS_H
