/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DH_COMFY_CLIENT_H
#define DH_COMFY_CLIENT_H

#include <QByteArray>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>
#include <functional>

#include "DhComfyWorkflow.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace DhComfy {

struct Reply {
    int status {0};       ///< HTTP status, 0 = no answer
    QByteArray body;
    QString error;        ///< network / timeout error ("" when the server answered 2xx)
    QJsonDocument json() const { return QJsonDocument::fromJson(body); }
    bool ok() const { return error.isEmpty(); }
};

/**
 * Plain HTTP access to a ComfyUI server (QtNetwork only: the Android build
 * has no Qt WebSockets).
 */
class Client : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const Reply &)>;

    explicit Client(QObject *parent = nullptr);

    /// "192.168.1.20:8188", "http://pc:8188", ... (http:// and :8188 are added if missing)
    void setServer(const QString &address);
    QUrl server() const { return m_base; }
    QString clientId() const { return m_clientId; }

    void get(const QString &path, const QUrlQuery &query, Callback cb, int timeoutMs = 20000);
    void postJson(const QString &path, const QJsonObject &body, Callback cb, int timeoutMs = 30000);
    void uploadImage(const QByteArray &png, const QString &name, Callback cb);

    /// GET /userdata/<encoded path>, e.g. "workflows/sub/foo.json"
    void getUserData(const QString &path, Callback cb);

private:
    QUrl url(const QString &path, const QUrlQuery &query = QUrlQuery()) const;
    void track(QNetworkReply *reply, Callback cb, int timeoutMs);

    QNetworkAccessManager *m_nam;
    QUrl m_base;
    QString m_clientId;
};

/// One generated image and where it goes on the canvas.
struct Result {
    QImage image;
    OutputNode output;
};

/**
 * One run: upload the inputs, queue the prompt, poll until it finishes and
 * download the images.
 */
class Job : public QObject
{
    Q_OBJECT
public:
    struct Upload {
        QString name;
        QByteArray png;
    };

    Job(Client *client, QObject *parent = nullptr);

    void start(const QVector<Upload> &uploads, const QJsonObject &prompt, const QVector<OutputNode> &outputs);
    void cancel();
    bool isRunning() const { return m_running; }
    QString promptId() const { return m_promptId; }

Q_SIGNALS:
    void status(const QString &text);
    void finished(const QVector<DhComfy::Result> &results);
    void failed(const QString &message);

private:
    void uploadNext();
    void queuePrompt();
    void poll();
    void download(const QJsonObject &outputs);
    void downloadNext();
    void fail(const QString &message);

    Client *m_client;
    QVector<Upload> m_uploads;
    QJsonObject m_prompt;
    QVector<OutputNode> m_outputs;
    QString m_promptId;
    QTimer m_pollTimer;
    qint64 m_startedMs {0};
    bool m_running {false};
    bool m_pollBusy {false};
    int m_pollFailures {0};

    struct Pending {
        QString filename, subfolder, type;
        OutputNode output;
    };
    QVector<Pending> m_pending;
    QVector<Result> m_results;
};

} // namespace DhComfy

Q_DECLARE_METATYPE(QVector<DhComfy::Result>)

#endif // DH_COMFY_CLIENT_H
