/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DH_COMFY_PLUGIN_H
#define DH_COMFY_PLUGIN_H

#include <QObject>
#include <QVariant>

class DhComfyPlugin : public QObject
{
    Q_OBJECT
public:
    DhComfyPlugin(QObject *parent, const QVariantList &);
    ~DhComfyPlugin() override;
};

#endif
