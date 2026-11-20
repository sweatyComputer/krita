/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "DhComfyClient.h"

#include <QDateTime>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUuid>

namespace DhComfy {

Client::Client(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_clientId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}

void Client::setServer(const QString &address)
{
    QString a = address.trimmed();
    while (a.endsWith('/')) a.chop(1);
    if (!a.contains("://")) a.prepend("http://");
    QUrl u(a);
    if (u.port() < 0 && u.scheme() == "http") u.setPort(8188);
    m_base = u;
}

QUrl Client::url(const QString &path, const QUrlQuery &query) const
{
    QUrl u = m_base;
    u.setPath(m_base.path() + path);
    if (!query.isEmpty()) u.setQuery(query);
    return u;
}

void Client::track(QNetworkReply *reply, Callback cb, int timeoutMs)
{
    QTimer *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [reply]() {
        reply->setProperty("dhTimedOut", true);
        reply->abort();
    });
    timer->start(timeoutMs);

    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        Reply r;
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.body = reply->readAll();
        if (reply->property("dhTimedOut").toBool()) {
            r.error = QStringLiteral("The server didn't answer in time.");
        } else if (reply->error() != QNetworkReply::NoError) {
            r.error = r.status ? QStringLiteral("HTTP %1").arg(r.status) : reply->errorString();
        }
        reply->deleteLater();
        if (cb) cb(r);
    });
}

void Client::get(const QString &path, const QUrlQuery &query, Callback cb, int timeoutMs)
{
    QNetworkRequest req(url(path, query));
    track(m_nam->get(req), cb, timeoutMs);
}

void Client::postJson(const QString &path, const QJsonObject &body, Callback cb, int timeoutMs)
{
    QNetworkRequest req(url(path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    track(m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact)), cb, timeoutMs);
}

void Client::uploadImage(const QByteArray &png, const QString &name, Callback cb)
{
    QHttpMultiPart *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart file;
    file.setHeader(QNetworkRequest::ContentTypeHeader, "image/png");
    file.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"image\"; filename=\"%1\"").arg(name));
    file.setBody(png);
    multi->append(file);

    auto field = [multi](const char *key, const char *value) {
        QHttpPart p;
        p.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"%1\"").arg(key));
        p.setBody(value);
        multi->append(p);
    };
    field("overwrite", "true");
    field("type", "input");

    QNetworkRequest req(url(QStringLiteral("/upload/image")));
    QNetworkReply *reply = m_nam->post(req, multi);
    multi->setParent(reply);
    track(reply, cb, 120000);
}

void Client::getUserData(const QString &path, Callback cb)
{
    // the whole path is one URL segment: "/" must go over the wire as %2F
    const QByteArray enc = QUrl::toPercentEncoding(path);
    QUrl u = QUrl::fromEncoded(m_base.toEncoded(QUrl::RemovePath | QUrl::RemoveQuery) + m_base.path().toUtf8()
                               + "/userdata/" + enc);
    QNetworkRequest req(u);
    track(m_nam->get(req), cb, 20000);
}

// ---------------------------------------------------------------------------

Job::Job(Client *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
    qRegisterMetaType<QVector<DhComfy::Result>>();
    m_pollTimer.setInterval(800);
    connect(&m_pollTimer, &QTimer::timeout, this, &Job::poll);
}

void Job::start(const QVector<Upload> &uploads, const QJsonObject &prompt, const QVector<OutputNode> &outputs)
{
    m_uploads = uploads;
    m_prompt = prompt;
    m_outputs = outputs;
    m_promptId.clear();
    m_results.clear();
    m_pending.clear();
    m_pollFailures = 0;
    m_running = true;
    m_startedMs = QDateTime::currentMSecsSinceEpoch();
    uploadNext();
}

void Job::fail(const QString &message)
{
    m_pollTimer.stop();
    m_running = false;
    Q_EMIT failed(message);
}

void Job::uploadNext()
{
    if (!m_running) return;
    if (m_uploads.isEmpty()) {
        queuePrompt();
        return;
    }
    const Upload u = m_uploads.takeFirst();
    Q_EMIT status(QStringLiteral("Sending %1…").arg(u.name));
    m_client->uploadImage(u.png, u.name, [this](const Reply &r) {
        if (!m_running) return;
        if (!r.ok()) {
            fail(QStringLiteral("Upload failed: %1").arg(r.error));
            return;
        }
        uploadNext();
    });
}

void Job::queuePrompt()
{
    Q_EMIT status(QStringLiteral("Queueing…"));
    const QJsonObject body {{"prompt", m_prompt}, {"client_id", m_client->clientId()}};
    m_client->postJson(QStringLiteral("/prompt"), body, [this](const Reply &r) {
        if (!m_running) return;
        const QJsonObject o = r.json().object();
        if (!r.ok() || o.contains("error")) {
            fail(o.isEmpty() ? QStringLiteral("Couldn't queue the workflow: %1").arg(r.error) : describePromptError(o));
            return;
        }
        m_promptId = o.value("prompt_id").toString();
        if (m_promptId.isEmpty()) {
            fail(QStringLiteral("The server accepted the workflow but returned no id."));
            return;
        }
        m_pollTimer.start();
        poll();
    });
}

void Job::poll()
{
    if (!m_running || m_pollBusy || m_promptId.isEmpty()) return;
    m_pollBusy = true;
    m_client->get(QStringLiteral("/history/") + m_promptId, QUrlQuery(), [this](const Reply &r) {
        m_pollBusy = false;
        if (!m_running) return;
        if (!r.ok()) {
            if (++m_pollFailures >= 8) fail(QStringLiteral("Lost the connection to ComfyUI: %1").arg(r.error));
            return;
        }
        m_pollFailures = 0;
        const QJsonObject entry = r.json().object().value(m_promptId).toObject();
        const int secs = int((QDateTime::currentMSecsSinceEpoch() - m_startedMs) / 1000);
        if (entry.isEmpty()) {
            // still queued or running: ask the queue which one
            m_client->get(QStringLiteral("/queue"), QUrlQuery(), [this, secs](const Reply &q) {
                if (!m_running || !q.ok()) return;
                const QJsonObject qo = q.json().object();
                bool running = false;
                for (const QJsonValue &item : qo.value("queue_running").toArray()) {
                    if (item.toArray().at(1).toString() == m_promptId) running = true;
                }
                int ahead = 0;
                if (!running) {
                    for (const QJsonValue &item : qo.value("queue_pending").toArray()) {
                        if (item.toArray().at(1).toString() == m_promptId) break;
                        ahead++;
                    }
                    ahead += qo.value("queue_running").toArray().size();
                }
                Q_EMIT status(running ? QStringLiteral("Generating… %1s").arg(secs)
                                      : QStringLiteral("Waiting in queue (%1 ahead)… %2s").arg(ahead).arg(secs));
            }, 10000);
            return;
        }
        const QJsonObject st = entry.value("status").toObject();
        if (st.value("status_str").toString() == "error") {
            fail(describeHistoryError(entry));
            return;
        }
        if (!st.value("completed").toBool(true)) return;
        m_pollTimer.stop();
        download(entry.value("outputs").toObject());
    }, 15000);
}

void Job::download(const QJsonObject &outputs)
{
    for (const OutputNode &o : m_outputs) {
        for (const QJsonValue &img : outputs.value(o.nodeId).toObject().value("images").toArray()) {
            const QJsonObject io = img.toObject();
            m_pending.append({io.value("filename").toString(), io.value("subfolder").toString(),
                              io.value("type").toString("output"), o});
        }
    }
    if (m_pending.isEmpty()) {
        // an output node we didn't recognise: take whatever images the run produced
        for (auto it = outputs.begin(); it != outputs.end(); ++it) {
            for (const QJsonValue &img : it.value().toObject().value("images").toArray()) {
                const QJsonObject io = img.toObject();
                OutputNode o;
                o.nodeId = it.key();
                m_pending.append({io.value("filename").toString(), io.value("subfolder").toString(),
                                  io.value("type").toString("output"), o});
            }
        }
    }
    if (m_pending.isEmpty()) {
        fail(QStringLiteral("The workflow finished but produced no images."));
        return;
    }
    downloadNext();
}

void Job::downloadNext()
{
    if (!m_running) return;
    if (m_pending.isEmpty()) {
        m_running = false;
        Q_EMIT finished(m_results);
        return;
    }
    const Pending p = m_pending.takeFirst();
    Q_EMIT status(QStringLiteral("Downloading result…"));
    QUrlQuery q;
    q.addQueryItem("filename", p.filename);
    q.addQueryItem("subfolder", p.subfolder);
    q.addQueryItem("type", p.type);
    m_client->get(QStringLiteral("/view"), q, [this, p](const Reply &r) {
        if (!m_running) return;
        if (!r.ok()) {
            fail(QStringLiteral("Couldn't download %1: %2").arg(p.filename, r.error));
            return;
        }
        QImage img = QImage::fromData(r.body);
        if (img.isNull()) {
            fail(QStringLiteral("%1 isn't an image Qt can read.").arg(p.filename));
            return;
        }
        m_results.append({img, p.output});
        downloadNext();
    }, 120000);
}

void Job::cancel()
{
    if (!m_running) return;
    m_running = false;
    m_pollTimer.stop();
    if (!m_promptId.isEmpty()) {
        m_client->postJson(QStringLiteral("/queue"), QJsonObject {{"delete", QJsonArray {m_promptId}}}, nullptr);
        m_client->postJson(QStringLiteral("/interrupt"), QJsonObject {{"prompt_id", m_promptId}}, nullptr);
    }
    Q_EMIT failed(QStringLiteral("Cancelled."));
}

} // namespace DhComfy
