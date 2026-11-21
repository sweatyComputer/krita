/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "DhComfySocket.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QtEndian>

namespace DhComfy {

namespace {
const QByteArray kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
const int kMaxFrame = 64 * 1024 * 1024; // sanity limit (previews are small JPEGs)
}

Socket::Socket(QObject *parent)
    : QObject(parent)
    , m_tcp(new QTcpSocket(this))
{
    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, &Socket::connectNow);
    connect(m_tcp, &QTcpSocket::connected, this, &Socket::onConnected);
    connect(m_tcp, &QTcpSocket::readyRead, this, &Socket::onReadyRead);
    connect(m_tcp, &QTcpSocket::disconnected, this, &Socket::onDisconnected);
    connect(m_tcp, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred), this,
            [this](QAbstractSocket::SocketError) {
                if (m_state != Open) onDisconnected();
            });
}

Socket::~Socket()
{
    m_wanted = false;
    m_tcp->abort();
}

void Socket::open(const QUrl &server, const QString &clientId)
{
    m_server = server;
    m_clientId = clientId;
    m_wanted = true;
    m_retries = 0;
    m_tcp->abort();
    m_state = Closed;
    connectNow();
}

void Socket::close()
{
    m_wanted = false;
    m_reconnect.stop();
    if (m_state == Open) sendFrame(0x8, QByteArray());
    m_tcp->abort();
    m_state = Closed;
}

void Socket::connectNow()
{
    if (!m_wanted) return;
    m_buffer.clear();
    m_fragments.clear();
    m_state = Connecting;
    m_tcp->abort();
    m_tcp->connectToHost(m_server.host(), quint16(m_server.port(8188)));
}

void Socket::onConnected()
{
    QByteArray rnd(16, '\0');
    for (int i = 0; i < 16; i++) rnd[i] = char(QRandomGenerator::global()->bounded(256));
    m_key = rnd.toBase64();

    QByteArray path = m_server.path().toUtf8();
    while (path.endsWith('/')) path.chop(1);
    path += "/ws?clientId=" + QUrl::toPercentEncoding(m_clientId);

    QByteArray req;
    req += "GET " + path + " HTTP/1.1\r\n";
    req += "Host: " + m_server.host().toUtf8() + ":" + QByteArray::number(m_server.port(8188)) + "\r\n";
    req += "Upgrade: websocket\r\nConnection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: " + m_key + "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n\r\n";
    m_state = Handshake;
    m_tcp->write(req);
}

void Socket::onReadyRead()
{
    m_buffer += m_tcp->readAll();
    if (m_state == Handshake) {
        const int end = m_buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            if (m_buffer.size() > 16384) m_tcp->abort();
            return;
        }
        const QByteArray head = m_buffer.left(end);
        m_buffer.remove(0, end + 4);
        const QByteArray accept = QCryptographicHash::hash(m_key + kGuid, QCryptographicHash::Sha1).toBase64();
        if (!head.startsWith("HTTP/1.1 101") || !head.contains(accept)) {
            m_tcp->abort();
            return;
        }
        m_state = Open;
        m_retries = 0;
        Q_EMIT connected();
    }
    if (m_state == Open) parseFrames();
}

void Socket::parseFrames()
{
    for (;;) {
        if (m_buffer.size() < 2) return;
        const uchar b0 = uchar(m_buffer[0]), b1 = uchar(m_buffer[1]);
        const bool fin = b0 & 0x80;
        const int opcode = b0 & 0x0F;
        const bool masked = b1 & 0x80;
        qint64 len = b1 & 0x7F;
        int pos = 2;
        if (len == 126) {
            if (m_buffer.size() < 4) return;
            len = qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(m_buffer.constData() + 2));
            pos = 4;
        } else if (len == 127) {
            if (m_buffer.size() < 10) return;
            len = qint64(qFromBigEndian<quint64>(reinterpret_cast<const uchar *>(m_buffer.constData() + 2)));
            pos = 10;
        }
        if (len < 0 || len > kMaxFrame) { m_tcp->abort(); return; }
        QByteArray mask;
        if (masked) {
            if (m_buffer.size() < pos + 4) return;
            mask = m_buffer.mid(pos, 4);
            pos += 4;
        }
        if (m_buffer.size() < pos + len) return;
        QByteArray payload = m_buffer.mid(pos, int(len));
        m_buffer.remove(0, pos + int(len));
        if (masked) {
            for (int i = 0; i < payload.size(); i++) payload[i] = char(payload[i] ^ mask[i % 4]);
        }

        if (opcode == 0x0) { // continuation
            m_fragments += payload;
            if (fin) {
                handleFrame(m_fragmentOpcode, m_fragments);
                m_fragments.clear();
            }
        } else if (opcode < 0x8 && !fin) { // first fragment
            m_fragmentOpcode = opcode;
            m_fragments = payload;
        } else {
            handleFrame(opcode, payload);
        }
        if (m_state != Open) return;
    }
}

void Socket::handleFrame(int opcode, const QByteArray &payload)
{
    switch (opcode) {
    case 0x1: { // text
        const QJsonObject o = QJsonDocument::fromJson(payload).object();
        Q_EMIT message(o.value("type").toString(), o.value("data").toObject());
        break;
    }
    case 0x2: { // binary: 4-byte event type, then for previews a 4-byte format and the image
        if (payload.size() < 8) break;
        const quint32 event = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(payload.constData()));
        if (event == 1) {
            QImage img = QImage::fromData(payload.mid(8));
            if (!img.isNull()) Q_EMIT preview(img);
        }
        break;
    }
    case 0x8: // close
        sendFrame(0x8, QByteArray());
        m_tcp->disconnectFromHost();
        break;
    case 0x9: // ping
        sendFrame(0xA, payload);
        break;
    default:
        break;
    }
}

void Socket::sendFrame(int opcode, const QByteArray &payload)
{
    if (m_tcp->state() != QAbstractSocket::ConnectedState) return;
    QByteArray f;
    f += char(0x80 | opcode);
    const int n = payload.size();
    if (n < 126) {
        f += char(0x80 | n);
    } else {
        f += char(0x80 | 126);
        f += char((n >> 8) & 0xFF);
        f += char(n & 0xFF);
    }
    QByteArray mask(4, '\0');
    for (int i = 0; i < 4; i++) mask[i] = char(QRandomGenerator::global()->bounded(256));
    f += mask;
    for (int i = 0; i < n; i++) f += char(payload[i] ^ mask[i % 4]);
    m_tcp->write(f);
}

void Socket::onDisconnected()
{
    const bool wasOpen = m_state == Open;
    m_state = Closed;
    if (wasOpen) Q_EMIT disconnected();
    scheduleReconnect();
}

void Socket::scheduleReconnect()
{
    if (!m_wanted || m_reconnect.isActive()) return;
    const int delays[] = {1000, 2000, 5000, 10000};
    m_reconnect.start(delays[qMin(m_retries++, 3)]);
}

} // namespace DhComfy
