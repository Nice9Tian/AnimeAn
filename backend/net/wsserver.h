#ifndef ANIMEAN_BACKEND_NET_WSSERVER_H
#define ANIMEAN_BACKEND_NET_WSSERVER_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

class WsServer : public QObject
{
    Q_OBJECT

public:
    explicit WsServer(const QString &token, QObject *parent = nullptr);
    ~WsServer() override;

    bool listen(quint16 port = 0);
    quint16 port() const;

    void sendText(const QByteArray &data);
    void sendBinary(const QByteArray &data);

    bool hasClient() const;

signals:
    void clientConnected();
    void clientDisconnected();
    void textReceived(QByteArray data);
    void binaryReceived(QByteArray data);

private:
    struct PendingClient {
        QPointer<QTcpSocket> socket;
        QByteArray buffer;
    };

    void onNewConnection();
    void onPendingReadyRead(QTcpSocket *socket);
    void onPendingDisconnected(QTcpSocket *socket);
    void removePending(QTcpSocket *socket);

    bool tryCompleteHandshake(PendingClient &pending);
    static bool isOriginAllowed(const QByteArray &origin);
    static void rejectHttp(QTcpSocket *socket, int statusCode, const char *reasonPhrase);

    void promoteClient(QTcpSocket *socket, const QByteArray &initialBuffer);
    void disconnectActiveClient(quint16 closeCode);

    void onClientReadyRead();
    void onClientDisconnected();
    bool processFrames(QTcpSocket *socket);
    void sendFrame(quint8 opcode, const QByteArray &payload);
    void sendCloseFrame(QTcpSocket *socket, quint16 closeCode);

    QString m_token;
    QTcpServer m_server;

    QList<PendingClient> m_pendingClients;
    QTcpSocket *m_client = nullptr;
    QByteArray m_readBuffer;

    bool m_inFragment = false;
    quint8 m_fragmentOpcode = 0;
    QByteArray m_fragmentBuffer;
};

#endif // ANIMEAN_BACKEND_NET_WSSERVER_H
