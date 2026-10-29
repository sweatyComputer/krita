/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DH_WETPAINT_PLUGIN_H
#define DH_WETPAINT_PLUGIN_H

#include <QObject>
#include <QVariant>

class DhWetPaintPlugin : public QObject
{
    Q_OBJECT
public:
    DhWetPaintPlugin(QObject *parent, const QVariantList &);
    ~DhWetPaintPlugin() override;
};

#endif // DH_WETPAINT_PLUGIN_H
