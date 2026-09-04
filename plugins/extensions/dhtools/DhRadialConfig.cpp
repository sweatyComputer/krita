/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "DhRadialConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QCoreApplication>

#include <klocalizedstring.h>
#include <kis_debug.h>

namespace {

DhRadialItemSP parseItem(const QJsonObject &object)
{
    DhRadialItemSP item(new DhRadialItem());
    item->raw = object;
    item->label = object.value(QStringLiteral("label")).toString();
    item->action = object.value(QStringLiteral("action")).toString();

    const QString colorName = object.value(QStringLiteral("color")).toString();
    if (!colorName.isEmpty()) {
        const QColor color(colorName);
        if (color.isValid()) {
            item->color = color;
        }
    }

    // ----- v2 presentation keys ------------------------------------------
    item->icon    = object.value(QStringLiteral("icon")).toString();
    item->tooltip = object.value(QStringLiteral("tooltip")).toString();
    item->shape   = object.value(QStringLiteral("shape")).toString().toUpper();

    const QString layout = object.value(QStringLiteral("layout")).toString().toUpper();
    if (layout == QLatin1String("GRID"))     item->slotLayout = DhRadialItem::Grid;
    if (layout == QLatin1String("DRAWER"))   item->slotLayout = DhRadialItem::Drawer;
    if (layout == QLatin1String("DROPDOWN")) item->slotLayout = DhRadialItem::Dropdown;
    if (layout == QLatin1String("SPROUT"))   item->slotLayout = DhRadialItem::Sprout;

    item->columns    = object.value(QStringLiteral("columns")).toInt(0);
    item->colSpan    = object.value(QStringLiteral("col_span")).toDouble(1.0);
    item->cellWidth  = object.value(QStringLiteral("cell_width")).toInt(0);
    item->cellHeight = object.value(QStringLiteral("cell_height")).toInt(0);

    item->drawerDirection = object.value(QStringLiteral("drawer_direction"))
                                 .toString(QStringLiteral("AUTO")).toUpper();
    item->drawerFlow = object.value(QStringLiteral("drawer_flow"))
                            .toString(QStringLiteral("LINE")).toUpper();
    item->drawerAlign = object.value(QStringLiteral("drawer_align"))
                             .toString(QStringLiteral("AUTO")).toUpper();

    const QJsonArray children = object.value(QStringLiteral("children")).toArray();
    for (const QJsonValue &value : children) {
        if (!value.isObject()) continue;
        DhRadialItemSP child = parseItem(value.toObject());
        if (child) item->children.append(child);
    }

    return item;
}

QList<DhRadialItemSP> parseDocument(const QByteArray &json, DhRadialDesign *design = nullptr)
{
    QList<DhRadialItemSP> result;

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError) {
        warnKrita << "DH radial menu: config is not valid JSON:" << error.errorString();
        return result;
    }

    // A v1 file has no "design" block at all; the defaults already in
    // DhRadialDesign are exactly the migration the desktop plugin performs.
    if (design) {
        design->read(document.object().value(QStringLiteral("design")).toObject());
    }

    const QJsonArray items = document.object().value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        if (!value.isObject()) continue;
        result.append(parseItem(value.toObject()));
    }

    return result;
}

QList<DhRadialItemSP> builtinFallback()
{
    // Deliberately tiny. If we are here the config is missing or broken, and a
    // menu with four things that definitely work beats an empty circle.
    struct Entry { QString label; QString action; QString color; };
    const QList<Entry> entries = {
        { i18n("Undo"),        QStringLiteral("edit_undo"),           QStringLiteral("#F1C40F") },
        { i18n("Redo"),        QStringLiteral("edit_redo"),           QStringLiteral("#F1C40F") },
        { i18n("Paint Layer"), QStringLiteral("add_new_paint_layer"), QStringLiteral("#4A8FD4") },
        { i18n("Save"),        QStringLiteral("file_save"),           QStringLiteral("#7F8C8D") },
    };

    QList<DhRadialItemSP> result;
    for (const Entry &entry : entries) {
        DhRadialItemSP item(new DhRadialItem());
        item->label = entry.label;
        item->action = entry.action;
        item->color = QColor(entry.color);
        result.append(item);
    }
    return result;
}

} // namespace

namespace {

QString layoutToString(DhRadialItem::SlotLayout layout)
{
    switch (layout) {
    case DhRadialItem::Grid:     return QStringLiteral("GRID");
    case DhRadialItem::Drawer:   return QStringLiteral("DRAWER");
    case DhRadialItem::Dropdown: return QStringLiteral("DROPDOWN");
    case DhRadialItem::Sprout:   return QStringLiteral("SPROUT");
    case DhRadialItem::Submenu:
    default:                     return QStringLiteral("SUBMENU");
    }
}

QJsonObject itemToJson(const DhRadialItemSP &item)
{
    QJsonObject object;
    if (!item) return object;

    // Start from whatever was in the file so keys this build does not
    // understand survive a round trip. Every key this build DOES understand
    // is then overwritten from the live object below, so an edit in the
    // settings UI is never shadowed by a stale copy left over in `raw`.
    object = item->raw;
    object.remove(QStringLiteral("children"));
    object.remove(QStringLiteral("action"));
    object.remove(QStringLiteral("color"));

    object.insert(QStringLiteral("label"), item->label);

    if (item->isSubmenu()) {
        QJsonArray children;
        for (const DhRadialItemSP &child : item->children) {
            children.append(itemToJson(child));
        }
        object.insert(QStringLiteral("children"), children);
    } else if (!item->action.isEmpty()) {
        object.insert(QStringLiteral("action"), item->action);
    }

    if (item->color.isValid()) {
        object.insert(QStringLiteral("color"), item->color.name());
    }

    // v2 presentation keys: only written when they differ from the default,
    // so a config that never touches this stuff stays readable rather than
    // turning into a wall of "shape": "" on every leaf.
    if (!item->icon.isEmpty())    object.insert(QStringLiteral("icon"), item->icon);
    else                          object.remove(QStringLiteral("icon"));

    if (!item->tooltip.isEmpty()) object.insert(QStringLiteral("tooltip"), item->tooltip);
    else                          object.remove(QStringLiteral("tooltip"));

    if (!item->shape.isEmpty())   object.insert(QStringLiteral("shape"), item->shape);
    else                          object.remove(QStringLiteral("shape"));

    if (item->isSubmenu() && item->slotLayout != DhRadialItem::Submenu) {
        object.insert(QStringLiteral("layout"), layoutToString(item->slotLayout));
    } else {
        object.remove(QStringLiteral("layout"));
    }

    if (item->columns > 0)  object.insert(QStringLiteral("columns"), item->columns);
    else                     object.remove(QStringLiteral("columns"));

    if (!qFuzzyCompare(item->colSpan, 1.0)) object.insert(QStringLiteral("col_span"), item->colSpan);
    else                                     object.remove(QStringLiteral("col_span"));

    if (item->cellWidth > 0)  object.insert(QStringLiteral("cell_width"), item->cellWidth);
    else                       object.remove(QStringLiteral("cell_width"));
    if (item->cellHeight > 0) object.insert(QStringLiteral("cell_height"), item->cellHeight);
    else                       object.remove(QStringLiteral("cell_height"));

    const bool isPanelChild = item->slotLayout == DhRadialItem::Drawer
        || item->slotLayout == DhRadialItem::Dropdown;
    if (isPanelChild && item->drawerDirection != QLatin1String("AUTO")) {
        object.insert(QStringLiteral("drawer_direction"), item->drawerDirection);
    } else {
        object.remove(QStringLiteral("drawer_direction"));
    }
    if (isPanelChild && item->drawerFlow != QLatin1String("LINE")) {
        object.insert(QStringLiteral("drawer_flow"), item->drawerFlow);
    } else {
        object.remove(QStringLiteral("drawer_flow"));
    }
    if (isPanelChild && item->drawerAlign != QLatin1String("AUTO")) {
        object.insert(QStringLiteral("drawer_align"), item->drawerAlign);
    } else {
        object.remove(QStringLiteral("drawer_align"));
    }

    return object;
}

} // namespace

QList<DhRadialItemSP> DhRadialConfig::clone(const QList<DhRadialItemSP> &items)
{
    QList<DhRadialItemSP> copy;
    for (const DhRadialItemSP &item : items) {
        if (!item) continue;
        DhRadialItemSP dup(new DhRadialItem());
        dup->raw = item->raw;
        dup->label = item->label;
        dup->action = item->action;
        dup->color = item->color;
        dup->icon = item->icon;
        dup->tooltip = item->tooltip;
        dup->shape = item->shape;
        dup->slotLayout = item->slotLayout;
        dup->columns = item->columns;
        dup->colSpan = item->colSpan;
        dup->cellWidth = item->cellWidth;
        dup->cellHeight = item->cellHeight;
        dup->drawerDirection = item->drawerDirection;
        dup->drawerFlow = item->drawerFlow;
        dup->drawerAlign = item->drawerAlign;
        dup->children = clone(item->children);
        copy.append(dup);
    }
    return copy;
}

QList<DhRadialItemSP> DhRadialConfig::loadDefaults()
{
    QFile file(QStringLiteral(":/dhtools/dh_radial_menu.json"));
    if (file.open(QIODevice::ReadOnly)) {
        const QList<DhRadialItemSP> items = parseDocument(file.readAll());
        file.close();
        if (!items.isEmpty()) return items;
    }
    return builtinFallback();
}

bool DhRadialConfig::save(const QList<DhRadialItemSP> &items, const DhRadialDesign *design,
                          QString *error)
{
    QJsonArray array;
    for (const DhRadialItemSP &item : items) {
        array.append(itemToJson(item));
    }

    QJsonObject root;

    if (design) {
        // The editor has a live design to save: write it out in full.
        QJsonObject block;
        design->write(block);
        root.insert(QStringLiteral("design"), block);
    } else {
        // No design was handed in: keep whatever is on disk byte-for-byte,
        // so an items-only save can never quietly drop somebody else's design.
        QFile existing(userConfigPath());
        if (existing.open(QIODevice::ReadOnly)) {
            const QJsonObject previous =
                QJsonDocument::fromJson(existing.readAll()).object();
            existing.close();
            if (previous.contains(QStringLiteral("design"))) {
                root.insert(QStringLiteral("design"), previous.value(QStringLiteral("design")));
            }
        }
    }

    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("_comment"),
                QStringLiteral("Menu items and design edited in Krita: Settings > Configure Krita > DH Radial Menu."));
    root.insert(QStringLiteral("items"), array);

    const QString path = userConfigPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    // Write to a sibling first and rename over the top, so a failure halfway
    // through leaves the previous menu intact rather than an empty file.
    const QString tempPath = path + QStringLiteral(".new");
    QFile file(tempPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = file.errorString();
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();

    QFile::remove(path);
    if (!QFile::rename(tempPath, path)) {
        if (error) *error = i18n("Could not replace %1", path);
        QFile::remove(tempPath);
        return false;
    }

    return true;
}

QString DhRadialConfig::userConfigPath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).absoluteFilePath(QStringLiteral("dhtools/dh_radial_menu.json"));
}

QList<DhRadialItemSP> DhRadialConfig::load(DhRadialDesign *design)
{
    const QString userPath = userConfigPath();

    // First run: drop the shipped default next to the user's other Krita data
    // so it can be edited on the device with any text editor. Chasing install
    // prefixes is a losing game on Android, where the app data dir is the only
    // location we can rely on being both readable and writable.
    if (!QFile::exists(userPath)) {
        QDir().mkpath(QFileInfo(userPath).absolutePath());
        if (QFile::copy(QStringLiteral(":/dhtools/dh_radial_menu.json"), userPath)) {
            QFile written(userPath);
            written.setPermissions(written.permissions()
                                   | QFileDevice::WriteOwner | QFileDevice::ReadOwner);
        } else {
            warnKrita << "DH radial menu: could not seed the default config at" << userPath;
        }
    }

    const QStringList candidates = { userPath, QStringLiteral(":/dhtools/dh_radial_menu.json") };

    for (const QString &path : candidates) {
        QFile file(path);
        if (!file.exists() || !file.open(QIODevice::ReadOnly)) continue;

        const QList<DhRadialItemSP> items = parseDocument(file.readAll(), design);
        file.close();

        if (!items.isEmpty()) {
            dbgKrita << "DH radial menu: loaded" << items.size() << "top-level items from" << path;
            return items;
        }
    }

    warnKrita << "DH radial menu: no usable config found, using the built-in fallback";
    return builtinFallback();
}
