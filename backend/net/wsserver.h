#ifndef ANIMEAN_BACKEND_NET_WSSERVER_H
#define ANIMEAN_BACKEND_NET_WSSERVER_H

#include "algorithm/core/string.h"
#include "platform/signalslot.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// The WebSocket server the pages of the web UI connect to, on 127.0.0.1 only.
// Several pages may be connected at once; a new one never displaces the others.
class WsServer
{
public:
    // Names one connection. Counted up from 1 and never used again, so 0 can
    // stand for "no client" and the id of a client that left matches nothing.
    using ClientId = std::uint64_t;

    explicit WsServer(const AnimeCore::String &token);
    ~WsServer();

    WsServer(const WsServer &) = delete;
    WsServer &operator=(const WsServer &) = delete;

    bool listen(std::uint16_t port = 0);
    std::uint16_t port() const;

    // To every connected client.
    void sendText(const std::string &data);
    void sendBinary(const std::string &data);
    // To one client. Nothing happens when it has left.
    void sendText(ClientId client, const std::string &data);
    void sendBinary(ClientId client, const std::string &data);

    bool hasClient() const;
    bool hasClient(ClientId client) const;
    std::size_t clientCount() const;
    // In the order they connected.
    std::vector<ClientId> clients() const;
    void closeClient(ClientId client, std::uint16_t closeCode = 1000);

    Signal<ClientId> clientConnected;
    Signal<ClientId> clientDisconnected;
    Signal<ClientId, std::string> textReceived;
    Signal<ClientId, std::string> binaryReceived;

private:
    using SocketHandle = std::uintptr_t;

    struct PendingClient {
        SocketHandle socket = 0;
        std::string buffer;
    };

    struct Client {
        ClientId id = 0;
        SocketHandle socket = 0;
        // clientConnected has gone out for it, so clientDisconnected is owed.
        bool announced = false;
        std::string readBuffer;
        std::string writeBuffer;
        std::size_t writeOffset = 0;
        bool watchingWrite = false;
        bool inFragment = false;
        std::uint8_t fragmentOpcode = 0;
        std::string fragmentBuffer;
        // When the peer last took bytes, or last had nothing waiting.
        std::uint64_t lastProgressMs = 0;
    };

    void closeListener();
    void onAcceptReady();
    void onPendingSocketEvent(SocketHandle socket, unsigned int events);
    void removePending(SocketHandle socket);
    void closeAndRemovePending(SocketHandle socket);

    bool tryCompleteHandshake(SocketHandle socket);
    static bool isOriginAllowed(const std::string &originRaw);
    void rejectHttp(SocketHandle socket, int statusCode, const char *reasonPhrase);

    // Every callback into the session may drop any client, so nothing here
    // keeps a Client pointer across one: it is looked up again by id.
    Client *findClient(ClientId id);
    const Client *findClient(ClientId id) const;
    ClientId addClient(SocketHandle socket, std::string initialBuffer);
    // closeCode 0: the peer is already gone, so no close frame is sent.
    void dropClient(ClientId id, std::uint16_t closeCode);

    void onClientSocketEvent(ClientId id, unsigned int events);
    bool processFrames(ClientId id);
    void sendFrame(ClientId id, std::uint8_t opcode, const std::string &payload);
    static void sendCloseFrameImmediate(SocketHandle socket, std::uint16_t closeCode);
    void queueWrite(ClientId id, const char *data, std::size_t size);
    void flushWriteBuffer(ClientId id);
    void updateClientPollEvents(Client &client);
    bool dropIfStalled(ClientId id);

    AnimeCore::String m_token;
    SocketHandle m_listenSocket = ~SocketHandle(0);
    std::uint16_t m_port = 0;

    std::vector<PendingClient> m_pendingClients;
    std::vector<std::unique_ptr<Client>> m_clients;
    ClientId m_nextClientId = 1;
};

#endif // ANIMEAN_BACKEND_NET_WSSERVER_H
