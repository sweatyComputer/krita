/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "DhRadialIcons.h"

#include <QAction>
#include <QFileInfo>
#include <QHash>

#include <kactioncollection.h>
#include <KisPart.h>
#include <KisMainWindow.h>

namespace {

/// An icon is only usable if it is non-null AND reports sizes it can render.
bool usable(const QIcon &icon)
{
    return !icon.isNull() && !icon.availableSizes().isEmpty();
}

QIcon bundled(const QString &name)
{
    const QString path = QStringLiteral(":/dhtools/icons/%1.svg").arg(name);
    QIcon icon(path);
    return usable(icon) ? icon : QIcon();
}

QIcon themed(const QString &name)
{
    // hasThemeIcon first: fromTheme on a missing name returns the theme's
    // placeholder, not a null icon. See the header comment.
    if (!QIcon::hasThemeIcon(name)) return QIcon();
    const QIcon icon = QIcon::fromTheme(name);
    return usable(icon) ? icon : QIcon();
}

QIcon fromAction(const QString &actionId)
{
    if (actionId.isEmpty() || !KisPart::exists()) return QIcon();

    KisMainWindow *window = KisPart::instance()->currentMainwindow();
    if (!window || !window->actionCollection()) return QIcon();

    QString id = actionId;
    if (id.startsWith(QLatin1String("krita:"))) id = id.mid(6);

    QAction *action = window->actionCollection()->action(id);
    if (!action) return QIcon();

    return usable(action->icon()) ? action->icon() : QIcon();
}

QIcon fromFile(const QString &path)
{
    if (!QFileInfo::exists(path)) return QIcon();
    QIcon icon(path);
    return usable(icon) ? icon : QIcon();
}

} // namespace

QIcon DhRadialIcons::resolve(const QString &spec, const QString &actionId, bool autoIcons)
{
    // Resolving hits the filesystem and the theme engine; the menu repaints on
    // every hover, so cache by the pair that determines the result.
    static QHash<QString, QIcon> cache;
    const QString key = spec + QLatin1Char('\n') + actionId + (autoIcons ? QStringLiteral("\n1")
                                                                        : QStringLiteral("\n0"));
    const auto cached = cache.constFind(key);
    if (cached != cache.constEnd()) return cached.value();

    QIcon result;

    if (spec.isEmpty()) {
        if (autoIcons) result = fromAction(actionId);
    } else if (spec.startsWith(QLatin1String("dh:"))) {
        result = bundled(spec.mid(3));
    } else if (spec.startsWith(QLatin1String("theme:"))) {
        result = themed(spec.mid(6));
    } else if (spec.startsWith(QLatin1String("krita:"))) {
        result = fromAction(spec);
    } else if (spec.contains(QLatin1Char('/')) || spec.contains(QLatin1Char('\\'))) {
        result = fromFile(spec);
    } else {
        // Bare name: bundled, then theme, then the action's own.
        result = bundled(spec);
        if (result.isNull()) result = themed(spec);
        if (result.isNull() && autoIcons) result = fromAction(actionId);
    }

    cache.insert(key, result);
    return result;
}
