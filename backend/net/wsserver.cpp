#include "wsserver.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QUrl>
#include <QUrlQuery>
#include <QtEndian>

namespace {

constexpr quint64 kMaxMessageSize = 256ULL * 1024ULL * 1024ULL; // 256 MB
constexpr qsizetype kMaxHandshakeHeaderSize = 64 * 1024;        // 64 KB

constexpr quint8 kOpcodeContinuation = 0x0;
constexpr quint8 kOpcodeText = 0x1;
constexpr quint8 kOpcodeBinary = 0x2;
constexpr quint8 kOpcodeClose = 0x8;
constexpr quint8 kOpcodePing = 0x9;
constexpr quint8 kOpcodePong = 0xA;

} // namespace

WsServer::WsServer(const QString &token, QObject *parent)
    : QObject(parent)
    , m_token(token)
{
    connect(&m_server, &QTcpServer::newConnection, this, &WsServer::onNewConnection);
}

WsServer::~WsServer()
{
    if (m_client) {
        m_client->disconnect(this);
        m_client->close();
        m_client = nullptr;
    }
    for (PendingClient &pending : m_pendingClients) {
        if (pending.socket) {
            pending.socket->disconnect(this);
            pending.socket->close();
        }
    }
    m_pendingClients.clear();
    if (m_server.isListening()) {
        m_server.close();
    }
}

bool WsServer::listen(quint16 port)
{
    if (m_server.isListening()) {
        m_server.close();
    }
    return m_server.listen(QHostAddress::LocalHost, port);
}

quint16 WsServer::port() const
{
    return m_server.isListening() ? m_server.serverPort() : 0;
}

void WsServer::sendText(const QByteArray &data)
{
    sendFrame(kOpcodeText, data);
}

void WsServer::sendBinary(const QByteArray &data)
{
    sendFrame(kOpcodeBinary, data);
}

bool WsServer::hasClient() const
{
    return m_client != nullptr && m_client->state() == QAbstractSocket::ConnectedState;
}

void WsServer::onNewConnection()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket *socket = m_server.nextPendingConnection();
        if (!socket) {
            continue;
        }
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

        PendingClient pending;
        pending.socket = socket;
        m_pendingClients.append(pending);

        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            onPendingReadyRead(socket);
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            onPendingDisconnected(socket);
        });

        if (socket->bytesAvailable() > 0) {
            onPendingReadyRead(socket);
        }
    }
}

void WsServer::onPendingReadyRead(QTcpSocket *socket)
{
    for (qsizetype i = 0; i < m_pendingClients.size(); ++i) {
        if (m_pendingClients[i].socket == socket) {
            m_pendingClients[i].buffer.append(socket->readAll());
            if (tryCompleteHandshake(m_pendingClients[i])) {
                return;
            }
            if (m_pendingClients[i].buffer.size() > kMaxHandshakeHeaderSize) {
                rejectHttp(socket, 400, "Bad Request");
                removePending(socket);
            }
            return;
        }
    }
}

void WsServer::onPendingDisconnected(QTcpSocket *socket)
{
    removePending(socket);
    socket->deleteLater();
}

void WsServer::removePending(QTcpSocket *socket)
{
    for (qsizetype i = 0; i < m_pendingClients.size(); ++i) {
        if (m_pendingClients[i].socket == socket) {
            m_pendingClients.removeAt(i);
            return;
        }
    }
}

bool WsServer::isOriginAllowed(const QByteArray &originRaw)
{
    const QByteArray origin = originRaw.trimmed();
    if (origin.isEmpty()) {
        return true;
    }
    if (origin == "tauri://localhost"
        || origin == "http://tauri.localhost"
        || origin == "https://tauri.localhost") {
        return true;
    }
    const QByteArray localhostPrefix("http://localhost:");
    if (origin.startsWith(localhostPrefix)) {
        const QByteArray portPart = origin.mid(localhostPrefix.size());
        if (portPart.isEmpty() || portPart.size() > 5) {
            return false;
        }
        for (char c : portPart) {
            if (c < '0' || c > '9') {
                return false;
            }
        }
        bool ok = false;
        const int portNum = portPart.toInt(&ok);
        return ok && portNum >= 1 && portNum <= 65535;
    }
    return false;
}

void WsServer::rejectHttp(QTcpSocket *socket, int statusCode, const char *reasonPhrase)
{
    if (!socket) {
        return;
    }
    socket->disconnect();
    const QByteArray response =
        "HTTP/1.1 " + QByteArray::number(statusCode) + " " + QByteArray(reasonPhrase) + "\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";
    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
    socket->deleteLater();
}

bool WsServer::tryCompleteHandshake(PendingClient &pending)
{
    QTcpSocket *socket = pending.socket;
    if (!socket) {
        return true;
    }

    const qsizetype headerEnd = pending.buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return false;
    }

    const QByteArray headerBlock = pending.buffer.left(headerEnd);
    const QByteArray remainingBuffer = pending.buffer.mid(headerEnd + 4);
    removePending(socket);

    const QList<QByteArray> lines = headerBlock.split('\n');
    if (lines.isEmpty()) {
        rejectHttp(socket, 400, "Bad Request");
        return true;
    }

    const QByteArray requestLine = lines.first().trimmed();
    const QList<QByteArray> requestParts = requestLine.split(' ');
    if (requestParts.size() < 3 || requestParts[0] != "GET" || !requestParts[2].startsWith("HTTP/1.")) {
        rejectHttp(socket, 400, "Bad Request");
        return true;
    }

    const QByteArray requestTarget = requestParts[1];
    QByteArray upgradeValue;
    QByteArray wsKeyValue;
    QByteArray wsVersionValue;
    QByteArray originValue;
    bool hasOrigin = false;

    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines[i].trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const qsizetype colonIdx = line.indexOf(':');
        if (colonIdx <= 0) {
            continue;
        }
        const QByteArray name = line.left(colonIdx).trimmed().toLower();
        const QByteArray value = line.mid(colonIdx + 1).trimmed();

        if (name == "upgrade") {
            upgradeValue = value;
        } else if (name == "sec-websocket-key") {
            wsKeyValue = value;
        } else if (name == "sec-websocket-version") {
            wsVersionValue = value;
        } else if (name == "origin") {
            hasOrigin = true;
            originValue = value;
        }
    }

    if (!upgradeValue.toLower().contains("websocket") || wsKeyValue.isEmpty()) {
        rejectHttp(socket, 400, "Bad Request");
        return true;
    }

    if (!wsVersionValue.isEmpty() && wsVersionValue != "13") {
        rejectHttp(socket, 426, "Upgrade Required");
        return true;
    }

    if (hasOrigin && !isOriginAllowed(originValue)) {
        rejectHttp(socket, 403, "Forbidden");
        return true;
    }

    const QUrl url = QUrl::fromEncoded(requestTarget);
    const QUrlQuery query(url);
    if (!query.hasQueryItem(QStringLiteral("token"))
        || query.queryItemValue(QStringLiteral("token"), QUrl::FullyDecoded) != m_token) {
        rejectHttp(socket, 401, "Unauthorized");
        return true;
    }

    const QByteArray acceptInput = wsKeyValue + QByteArrayLiteral("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    const QByteArray acceptKey = QCryptographicHash::hash(acceptInput, QCryptographicHash::Sha1).toBase64();

    const QByteArray handshakeResponse =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + acceptKey + "\r\n"
        "\r\n";

    promoteClient(socket, remainingBuffer);
    socket->write(handshakeResponse);
    socket->flush();

    emit clientConnected();

    if (m_client == socket && (!m_readBuffer.isEmpty() || socket->bytesAvailable() > 0)) {
        onClientReadyRead();
    }
    return true;
}

void WsServer::promoteClient(QTcpSocket *socket, const QByteArray &initialBuffer)
{
    if (m_client) {
        disconnectActiveClient(1000);
    }

    socket->disconnect(this);
    m_client = socket;
    m_readBuffer = initialBuffer;
    m_inFragment = false;
    m_fragmentOpcode = 0;
    m_fragmentBuffer.clear();

    connect(socket, &QTcpSocket::readyRead, this, &WsServer::onClientReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &WsServer::onClientDisconnected);
}

void WsServer::disconnectActiveClient(quint16 closeCode)
{
    if (!m_client) {
        return;
    }

    QTcpSocket *oldClient = m_client;
    m_client = nullptr;
    m_readBuffer.clear();
    m_inFragment = false;
    m_fragmentOpcode = 0;
    m_fragmentBuffer.clear();

    oldClient->disconnect(this);
    if (oldClient->state() == QAbstractSocket::ConnectedState) {
        if (closeCode != 0) {
            sendCloseFrame(oldClient, closeCode);
        }
        oldClient->flush();
        oldClient->disconnectFromHost();
    }
    oldClient->deleteLater();

    emit clientDisconnected();
}

void WsServer::onClientDisconnected()
{
    if (!m_client) {
        return;
    }

    QTcpSocket *oldClient = m_client;
    m_client = nullptr;
    m_readBuffer.clear();
    m_inFragment = false;
    m_fragmentOpcode = 0;
    m_fragmentBuffer.clear();

    oldClient->disconnect(this);
    oldClient->deleteLater();

    emit clientDisconnected();
}

void WsServer::onClientReadyRead()
{
    if (!m_client) {
        return;
    }
    QTcpSocket *socket = m_client;
    if (socket->bytesAvailable() > 0) {
        m_readBuffer.append(socket->readAll());
    }
    processFrames(socket);
}

bool WsServer::processFrames(QTcpSocket *socket)
{
    qsizetype offset = 0;

    while (m_client == socket) {
        const qsizetype avail = m_readBuffer.size() - offset;
        if (avail < 2) {
            break;
        }

        const uchar *ptr = reinterpret_cast<const uchar *>(m_readBuffer.constData()) + offset;
        const bool fin = (ptr[0] & 0x80) != 0;
        const quint8 rsv = (ptr[0] & 0x70);
        const quint8 opcode = (ptr[0] & 0x0F);
        const bool masked = (ptr[1] & 0x80) != 0;
        const quint8 len7 = (ptr[1] & 0x7F);

        if (rsv != 0) {
            disconnectActiveClient(1002);
            return false;
        }

        if (opcode != kOpcodeContinuation
            && opcode != kOpcodeText
            && opcode != kOpcodeBinary
            && opcode != kOpcodeClose
            && opcode != kOpcodePing
            && opcode != kOpcodePong) {
            disconnectActiveClient(1002);
            return false;
        }

        if (!masked) {
            disconnectActiveClient(1002);
            return false;
        }

        const bool isControl = (opcode & 0x08) != 0;
        if (isControl && (!fin || len7 > 125)) {
            disconnectActiveClient(1002);
            return false;
        }

        qsizetype headerLen = 2 + 4;
        quint64 payloadLen = len7;

        if (len7 == 126) {
            headerLen = 2 + 2 + 4;
            if (avail < headerLen) {
                break;
            }
            payloadLen = qFromBigEndian<quint16>(ptr + 2);
        } else if (len7 == 127) {
            headerLen = 2 + 8 + 4;
            if (avail < headerLen) {
                break;
            }
            payloadLen = qFromBigEndian<quint64>(ptr + 2);
            if ((payloadLen & (1ULL << 63)) != 0) {
                disconnectActiveClient(1002);
                return false;
            }
        }

        if (payloadLen > kMaxMessageSize) {
            disconnectActiveClient(1009);
            return false;
        }

        if (!isControl) {
            if (opcode == kOpcodeContinuation) {
                if (!m_inFragment) {
                    disconnectActiveClient(1002);
                    return false;
                }
                if (static_cast<quint64>(m_fragmentBuffer.size()) + payloadLen > kMaxMessageSize) {
                    disconnectActiveClient(1009);
                    return false;
                }
            } else {
                if (m_inFragment) {
                    disconnectActiveClient(1002);
                    return false;
                }
            }
        }

        const quint64 totalFrameLen = static_cast<quint64>(headerLen) + payloadLen;
        if (static_cast<quint64>(avail) < totalFrameLen) {
            break;
        }

        const uchar *maskKey = ptr + (headerLen - 4);
        const uchar *src = ptr + headerLen;

        QByteArray payload(static_cast<qsizetype>(payloadLen), Qt::Uninitialized);
        uchar *dst = reinterpret_cast<uchar *>(payload.data());
        for (qsizetype i = 0; i < static_cast<qsizetype>(payloadLen); ++i) {
            dst[i] = src[i] ^ maskKey[i & 3];
        }

        offset += static_cast<qsizetype>(totalFrameLen);
        if (offset == m_readBuffer.size()) {
            m_readBuffer.clear();
            offset = 0;
        }

        if (opcode == kOpcodePing) {
            sendFrame(kOpcodePong, payload);
        } else if (opcode == kOpcodePong) {
            // No action required.
        } else if (opcode == kOpcodeClose) {
            if (payload.size() == 1) {
                disconnectActiveClient(1002);
                return false;
            }
            quint16 closeCode = 1000;
            if (payload.size() >= 2) {
                closeCode = qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(payload.constData()));
            }
            disconnectActiveClient(closeCode);
            return false;
        } else if (opcode == kOpcodeText || opcode == kOpcodeBinary) {
            if (fin) {
                if (opcode == kOpcodeText) {
                    emit textReceived(payload);
                } else {
                    emit binaryReceived(payload);
                }
            } else {
                m_inFragment = true;
                m_fragmentOpcode = opcode;
                m_fragmentBuffer = std::move(payload);
            }
        } else if (opcode == kOpcodeContinuation) {
            m_fragmentBuffer.append(payload);
            if (fin) {
                QByteArray completeMessage = std::move(m_fragmentBuffer);
                m_fragmentBuffer.clear();
                const quint8 finalOpcode = m_fragmentOpcode;
                m_inFragment = false;
                m_fragmentOpcode = 0;

                if (finalOpcode == kOpcodeText) {
                    emit textReceived(completeMessage);
                } else {
                    emit binaryReceived(completeMessage);
                }
            }
        }
    }

    if (m_client == socket && offset > 0) {
        m_readBuffer.remove(0, offset);
    }
    return m_client == socket;
}

void WsServer::sendFrame(quint8 opcode, const QByteArray &payload)
{
    if (!hasClient()) {
        return;
    }

    const quint64 len = static_cast<quint64>(payload.size());
    QByteArray header;
    header.reserve(10);
    header.append(static_cast<char>(0x80 | (opcode & 0x0F)));

    if (len <= 125) {
        header.append(static_cast<char>(len));
    } else if (len <= 65535) {
        header.append(static_cast<char>(126));
        quint16 be16 = qToBigEndian<quint16>(static_cast<quint16>(len));
        header.append(reinterpret_cast<const char *>(&be16), sizeof(be16));
    } else {
        header.append(static_cast<char>(127));
        quint64 be64 = qToBigEndian<quint64>(len);
        header.append(reinterpret_cast<const char *>(&be64), sizeof(be64));
    }

    m_client->write(header);
    if (!payload.isEmpty()) {
        m_client->write(payload);
    }
    m_client->flush();
}

void WsServer::sendCloseFrame(QTcpSocket *socket, quint16 closeCode)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    uchar frame[4];
    frame[0] = 0x80 | kOpcodeClose;
    frame[1] = 2;
    qToBigEndian<quint16>(closeCode, frame + 2);
    socket->write(reinterpret_cast<const char *>(frame), sizeof(frame));
}
