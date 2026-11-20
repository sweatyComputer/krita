/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DH_COMFY_WORKFLOW_H
#define DH_COMFY_WORKFLOW_H

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>

/**
 * ComfyUI workflow handling, Qt Core only (no Krita types) so it can be
 * tested on its own against a real server.
 *
 *  - convertUiToApi(): the editor ("UI") format ComfyUI saves to disk ->
 *    the "API" format POST /prompt wants. Handles widget values, primitive
 *    nodes, reroutes, muted / bypassed nodes and subgraphs.
 *  - analyse(): what the workflow needs from Krita and which knobs to show.
 *  - prepare(): plug the canvas, selection and knob values in, and swap
 *    the Krita placeholder nodes (Acly's comfyui-tooling-nodes) for core
 *    nodes, so the result can be fetched over plain HTTP.
 */
namespace DhComfy {

struct Control {
    enum Kind {
        Prompt,         ///< multi-line text
        NegativePrompt, ///< multi-line text feeding a "negative" input
        Text,           ///< single line text
        Int,
        Float,
        Bool,
        Choice,
        Seed,           ///< int + "random" toggle
        ImageInput,     ///< LoadImage: canvas or the file it already has
        MaskInput,      ///< LoadImageMask: selection or its file
        CanvasSize,     ///< toggle: size latents to the canvas / selection
    };

    Kind kind {Text};
    QString key;         ///< stable id, used to remember values
    QString label;
    QString group;       ///< section in the docker
    QString nodeId;      ///< API node id ("" for placeholder parameters)
    QString input;       ///< input name on that node
    QJsonValue value;    ///< default / current value
    QStringList options; ///< Choice / ImageInput / MaskInput
    double min {0.0};
    double max {0.0};
    double step {0.0};
    bool hasRange {false};
    int order {0};
};

/// Everything Krita supplies for one run.
struct KritaInputs {
    QString canvasFile;        ///< uploaded canvas (RGBA png), ComfyUI input name
    QString canvasMaskFile;    ///< canvas alpha as a white=opaque mask png
    QString selectionMaskFile; ///< selection, white=selected (or all white)
    bool hasSelection {false};
    QRect rect;                ///< canvas area sent (x/y offset, w/h)
    qint64 seed {0};           ///< fresh random seed for this run
};

/// Where a result goes.
struct OutputNode {
    QString nodeId;
    int offsetX {0};
    int offsetY {0};
    QString name;
};

struct Prepared {
    QJsonObject prompt;            ///< ready for POST /prompt
    QVector<OutputNode> outputs;   ///< read these from /history
    QString error;
};

/// The server's node list (GET /object_info) plus the key order of every
/// JSON object in it: QJsonObject sorts keys alphabetically, but widget
/// values are matched to inputs by position.
struct NodeInfo {
    QJsonObject info;
    QHash<QString, QStringList> keyOrder; ///< object path ("\x1f"-joined keys) -> keys in file order

    static NodeInfo fromJson(const QByteArray &raw);
    bool isEmpty() const { return info.isEmpty(); }
    int size() const { return info.size(); }
};

bool isUiFormat(const QJsonObject &workflow);
bool isApiFormat(const QJsonObject &workflow);

/// UI -> API. Returns an empty object and sets error on failure.
QJsonObject convertUiToApi(const QJsonObject &ui, const NodeInfo &nodes, QString *error);

/// Either format -> API (API passes through).
QJsonObject toApi(const QJsonObject &workflow, const NodeInfo &nodes, QString *error);

/// Controls to show for an API-format workflow.
QVector<Control> analyse(const QJsonObject &api, const NodeInfo &nodes);

/// Does this workflow use the canvas / selection at all?
bool needsCanvas(const QJsonObject &api, const QVector<Control> &controls, const QHash<QString, QJsonValue> &values);
bool needsSelectionMask(const QJsonObject &api, const QVector<Control> &controls, const QHash<QString, QJsonValue> &values);

/// values: control key -> value from the docker.
Prepared prepare(const QJsonObject &api,
                 const QVector<Control> &controls,
                 const QHash<QString, QJsonValue> &values,
                 const KritaInputs &krita);

/// Readable text from a /prompt error response or a /history error status.
QString describePromptError(const QJsonObject &response);
QString describeHistoryError(const QJsonObject &historyEntry);

} // namespace DhComfy

#endif // DH_COMFY_WORKFLOW_H
