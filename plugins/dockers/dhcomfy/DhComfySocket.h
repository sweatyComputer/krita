/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DH_COMFY_SOCKET_H
#define DH_COMFY_SOCKET_H

#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QUrl>

class QTcpSocket;

namespace DhComfy {

/**
 * Minimal WebSocket client (RFC 6455) for ComfyUI's /ws progress events.
 *
 * The Android build has no Qt WebSockets module, and all we need is to read
 * JSON text frames and the odd binary preview, so this is plain QTcpSocket.
 * It reconnects by itself; generation never depends on it (results still
 * come from polling /history), it only feeds the progress bar.
 */
class Socket : public QObject
{
    Q_OBJECT
public:
    explicit Socket(QObject *parent = nullptr);
    ~Socket() override;

    /// ws://host:port/ws?clientId=<id>; reconnects until close() is called
    void open(const QUrl &server, const QString &clientId);
    void close();
    bool isConnected() const { return m_state == Open; }

Q_SIGNALS:
    void connected();
    void disconnected();
    /// a JSON message: {"type": ..., "data": {...}}
    void message(const QString &type, const QJsonObject &data);
    /// a live preview frame (only if the server sends them, e.g. --preview-method auto)
    void preview(const QImage &image);

private:
    enum State { Closed, Connecting, Handshake, Open };

    void connectNow();
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void parseFrames();
    void handleFrame(int opcode, const QByteArray &payload);
    void sendFrame(int opcode, const QByteArray &payload);
    void scheduleReconnect();

    QTcpSocket *m_tcp;
    QTimer m_reconnect;
    QUrl m_server;
    QString m_clientId;
    QByteArray m_key;
    QByteArray m_buffer;
    QByteArray m_fragments;
    int m_fragmentOpcode {0};
    State m_state {Closed};
    bool m_wanted {false};
    int m_retries {0};
};

} // namespace DhComfy

#endif // DH_COMFY_SOCKET_H
