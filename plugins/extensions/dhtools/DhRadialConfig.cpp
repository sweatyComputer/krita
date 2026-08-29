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
    item->label = object.value(QStringLiteral("label")).toString();
    item->action = object.value(QStringLiteral("action")).toString();

    const QString colorName = object.value(QStringLiteral("color")).toString();
    if (!colorName.isEmpty()) {
        const QColor color(colorName);
        if (color.isValid()) {
            item->color = color;
        }
    }

    const QJsonArray children = object.value(QStringLiteral("children")).toArray();
    for (const QJsonValue &value : children) {
        if (!value.isObject()) continue;
        DhRadialItemSP child = parseItem(value.toObject());
        if (child) item->children.append(child);
    }

    return item;
}

QList<DhRadialItemSP> parseDocument(const QByteArray &json)
{
    QList<DhRadialItemSP> result;

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError) {
        warnKrita << "DH radial menu: config is not valid JSON:" << error.errorString();
        return result;
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

QJsonObject itemToJson(const DhRadialItemSP &item)
{
    QJsonObject object;
    if (!item) return object;

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

    return object;
}

} // namespace

QList<DhRadialItemSP> DhRadialConfig::clone(const QList<DhRadialItemSP> &items)
{
    QList<DhRadialItemSP> copy;
    for (const DhRadialItemSP &item : items) {
        if (!item) continue;
        DhRadialItemSP dup(new DhRadialItem());
        dup->label = item->label;
        dup->action = item->action;
        dup->color = item->color;
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

bool DhRadialConfig::save(const QList<DhRadialItemSP> &items, QString *error)
{
    QJsonArray array;
    for (const DhRadialItemSP &item : items) {
        array.append(itemToJson(item));
    }

    QJsonObject root;
    root.insert(QStringLiteral("_comment"),
                QStringLiteral("Written by Krita: Settings > Configure Krita > DH Radial Menu."));
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

QList<DhRadialItemSP> DhRadialConfig::load()
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

        const QList<DhRadialItemSP> items = parseDocument(file.readAll());
        file.close();

        if (!items.isEmpty()) {
            dbgKrita << "DH radial menu: loaded" << items.size() << "top-level items from" << path;
            return items;
        }
    }

    warnKrita << "DH radial menu: no usable config found, using the built-in fallback";
    return builtinFallback();
}
