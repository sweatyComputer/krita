/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "DhRadialIcons.h"

#include <QAction>
#include <QByteArray>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QSet>

#include <kactioncollection.h>
#include <KisPart.h>
#include <KisMainWindow.h>

namespace {

/// A cheap, stable fingerprint of a rendered icon: an 8x8 downscale reduced to
/// one coarse luminance bucket per pixel. Two different icons practically
/// never collide; the same placeholder always does. Ported from the desktop
/// plugin's icons.py (_image_key) - see the header comment for why this
/// exists at all. An empty result means nothing was drawn (fully transparent).
QByteArray fingerprint(const QImage &img)
{
    if (img.isNull()) return QByteArray();
    const QImage small = img.scaled(8, 8, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_ARGB32);
    QByteArray bits;
    bits.reserve(64);
    int opaque = 0;
    for (int y = 0; y < small.height(); ++y) {
        for (int x = 0; x < small.width(); ++x) {
            const QRgb px = small.pixel(x, y);
            if (qAlpha(px) > 8) ++opaque;
            bits.append(char((qRed(px) + qGreen(px) + qBlue(px)) / 48));
        }
    }
    if (opaque == 0) return QByteArray();
    return bits;
}

/// Fingerprints of whatever this platform draws for a guaranteed-missing
/// icon, so a placeholder that sneaks through QIcon::fromTheme() (or that
/// Krita's own action loader already baked into a QAction's icon before we
/// ever saw it) gets recognised and rejected instead of painted.
const QSet<QByteArray> &placeholderFingerprints()
{
    static QSet<QByteArray> keys;
    static bool built = false;
    if (!built) {
        built = true;
        const QStringList probes = {
            QStringLiteral("dh-radial-definitely-not-an-icon-9f2a"),
            QStringLiteral("image-missing"),
            QStringLiteral("unknown"),
            QStringLiteral("gtk-missing-image")
        };
        for (const QString &probe : probes) {
            if (!QIcon::hasThemeIcon(probe)) continue;
            const QByteArray key = fingerprint(QIcon::fromTheme(probe).pixmap(32, 32).toImage());
            if (!key.isEmpty()) keys.insert(key);
        }
    }
    return keys;
}

/// An icon is only usable if it is non-null, reports sizes it can render, AND
/// actually rasterises to something other than the platform's "image missing"
/// placeholder.
///
/// THE X-IN-A-BOX PROBLEM (read before simplifying this back down):
/// isNull() + availableSizes() alone are NOT enough. QIcon::fromTheme() on a
/// missing name does not return a null icon - it hands back the theme's own
/// "image-missing" graphic, which is non-null and reports real sizes. Worse,
/// on a platform with no icon theme (Android has none), Krita's own action
/// loader may have already resolved an action's icon through that same
/// fromTheme() fallback before DhRadialIcons ever sees it via
/// action->icon() - so the placeholder can arrive already baked into a
/// perfectly "usable"-looking QIcon. Rendering the icon and fingerprinting
/// the pixels is the only point left to catch that.
bool usable(const QIcon &icon)
{
    if (icon.isNull() || icon.availableSizes().isEmpty()) return false;
    const QByteArray key = fingerprint(icon.pixmap(32, 32).toImage());
    if (key.isEmpty()) return false;                          // fully transparent
    if (placeholderFingerprints().contains(key)) return false; // the X in a box
    return true;
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
