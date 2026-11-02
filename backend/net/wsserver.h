#ifndef ANIMEAN_BACKEND_NET_WSSERVER_H
#define ANIMEAN_BACKEND_NET_WSSERVER_H

#include "algorithm/core/string.h"
#include "platform/signalslot.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class WsServer
{
public:
    explicit WsServer(const AnimeCore::String &token);
    ~WsServer();

    WsServer(const WsServer &) = delete;
    WsServer &operator=(const WsServer &) = delete;

    bool listen(std::uint16_t port = 0);
    std::uint16_t port() const;

    void sendText(const std::string &data);
    void sendBinary(const std::string &data);

    bool hasClient() const;

    Signal<> clientConnected;
    Signal<> clientDisconnected;
    Signal<std::string> textReceived;
    Signal<std::string> binaryReceived;

private:
    using SocketHandle = std::uintptr_t;

    struct PendingClient {
        SocketHandle socket = 0;
        std::string buffer;
    };

    void closeListener();
    void onAcceptReady();
    void onPendingSocketEvent(SocketHandle socket, unsigned int events);
    void removePending(SocketHandle socket);
    void closeAndRemovePending(SocketHandle socket);

    bool tryCompleteHandshake(SocketHandle socket);
    static bool isOriginAllowed(const std::string &originRaw);
    void rejectHttp(SocketHandle socket, int statusCode, const char *reasonPhrase);

    void promoteClient(SocketHandle socket, std::string initialBuffer);
    void disconnectActiveClient(std::uint16_t closeCode);

    void onClientSocketEvent(SocketHandle socket, unsigned int events);
    void onClientDisconnected();
    bool processFrames(SocketHandle socket);
    void sendFrame(std::uint8_t opcode, const std::string &payload);
    static void sendCloseFrameImmediate(SocketHandle socket, std::uint16_t closeCode);
    void queueWrite(const char *data, std::size_t size);
    void flushWriteBuffer();
    void updateClientPollEvents();

    AnimeCore::String m_token;
    SocketHandle m_listenSocket = ~SocketHandle(0);
    std::uint16_t m_port = 0;

    std::vector<PendingClient> m_pendingClients;
    SocketHandle m_client = ~SocketHandle(0);
    std::string m_readBuffer;
    std::string m_writeBuffer;
    std::size_t m_writeOffset = 0;
    bool m_watchingWrite = false;

    bool m_inFragment = false;
    std::uint8_t m_fragmentOpcode = 0;
    std::string m_fragmentBuffer;
};

#endif // ANIMEAN_BACKEND_NET_WSSERVER_H
