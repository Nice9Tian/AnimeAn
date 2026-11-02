#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "wsserver.h"

#include "algorithm/core/image.h"
#include "platform/eventloop.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace {

constexpr std::uintptr_t kInvalidSocket = ~std::uintptr_t(0);
constexpr std::uint64_t kMaxMessageSize = 256ULL * 1024ULL * 1024ULL; // 256 MB
constexpr std::size_t kMaxHandshakeHeaderSize = 64 * 1024;            // 64 KB
constexpr std::size_t kRecvChunkSize = 64 * 1024;
constexpr std::size_t kWriteCompactThreshold = 64 * 1024;

constexpr std::uint8_t kOpcodeContinuation = 0x0;
constexpr std::uint8_t kOpcodeText = 0x1;
constexpr std::uint8_t kOpcodeBinary = 0x2;
constexpr std::uint8_t kOpcodeClose = 0x8;
constexpr std::uint8_t kOpcodePing = 0x9;
constexpr std::uint8_t kOpcodePong = 0xA;

void ensureWinsock()
{
    static const bool ok = []() {
        WSADATA wsa{};
        return ::WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    (void)ok;
}

std::uint16_t readBe16(const unsigned char *p) noexcept
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8)
                                      | static_cast<std::uint16_t>(p[1]));
}

std::uint64_t readBe64(const unsigned char *p) noexcept
{
    return (static_cast<std::uint64_t>(p[0]) << 56)
           | (static_cast<std::uint64_t>(p[1]) << 48)
           | (static_cast<std::uint64_t>(p[2]) << 40)
           | (static_cast<std::uint64_t>(p[3]) << 32)
           | (static_cast<std::uint64_t>(p[4]) << 24)
           | (static_cast<std::uint64_t>(p[5]) << 16)
           | (static_cast<std::uint64_t>(p[6]) << 8)
           | static_cast<std::uint64_t>(p[7]);
}

void writeBe16(unsigned char *dst, std::uint16_t v) noexcept
{
    dst[0] = static_cast<unsigned char>((v >> 8) & 0xFFu);
    dst[1] = static_cast<unsigned char>(v & 0xFFu);
}

void writeBe64(unsigned char *dst, std::uint64_t v) noexcept
{
    dst[0] = static_cast<unsigned char>((v >> 56) & 0xFFu);
    dst[1] = static_cast<unsigned char>((v >> 48) & 0xFFu);
    dst[2] = static_cast<unsigned char>((v >> 40) & 0xFFu);
    dst[3] = static_cast<unsigned char>((v >> 32) & 0xFFu);
    dst[4] = static_cast<unsigned char>((v >> 24) & 0xFFu);
    dst[5] = static_cast<unsigned char>((v >> 16) & 0xFFu);
    dst[6] = static_cast<unsigned char>((v >> 8) & 0xFFu);
    dst[7] = static_cast<unsigned char>(v & 0xFFu);
}

std::uint32_t rotl32(std::uint32_t value, unsigned int bits) noexcept
{
    return (value << bits) | (value >> (32u - bits));
}

void computeSha1(const unsigned char *data, std::size_t len, unsigned char outDigest[20]) noexcept
{
    std::uint32_t h0 = 0x67452301u;
    std::uint32_t h1 = 0xEFCDAB89u;
    std::uint32_t h2 = 0x98BADCFEu;
    std::uint32_t h3 = 0x10325476u;
    std::uint32_t h4 = 0xC3D2E1F0u;

    const std::uint64_t bitLen = static_cast<std::uint64_t>(len) * 8ULL;
    std::vector<unsigned char> msg(data, data + len);
    msg.push_back(0x80u);
    while ((msg.size() % 64u) != 56u) {
        msg.push_back(0x00u);
    }
    unsigned char lenBytes[8];
    writeBe64(lenBytes, bitLen);
    msg.insert(msg.end(), lenBytes, lenBytes + 8);

    for (std::size_t chunkOffset = 0; chunkOffset < msg.size(); chunkOffset += 64u) {
        const unsigned char *chunk = msg.data() + chunkOffset;
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(chunk[i * 4 + 0]) << 24)
                   | (static_cast<std::uint32_t>(chunk[i * 4 + 1]) << 16)
                   | (static_cast<std::uint32_t>(chunk[i * 4 + 2]) << 8)
                   | static_cast<std::uint32_t>(chunk[i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1u);
        }

        std::uint32_t a = h0;
        std::uint32_t b = h1;
        std::uint32_t c = h2;
        std::uint32_t d = h3;
        std::uint32_t e = h4;

        for (int i = 0; i < 80; ++i) {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const std::uint32_t temp = rotl32(a, 5u) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl32(b, 30u);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    const std::uint32_t words[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
        outDigest[i * 4 + 0] = static_cast<unsigned char>((words[i] >> 24) & 0xFFu);
        outDigest[i * 4 + 1] = static_cast<unsigned char>((words[i] >> 16) & 0xFFu);
        outDigest[i * 4 + 2] = static_cast<unsigned char>((words[i] >> 8) & 0xFFu);
        outDigest[i * 4 + 3] = static_cast<unsigned char>(words[i] & 0xFFu);
    }
}

std::string trimAscii(std::string_view sv)
{
    std::size_t start = 0;
    while (start < sv.size() && std::isspace(static_cast<unsigned char>(sv[start]))) {
        ++start;
    }
    std::size_t end = sv.size();
    while (end > start && std::isspace(static_cast<unsigned char>(sv[end - 1]))) {
        --end;
    }
    return std::string(sv.substr(start, end - start));
}

std::string toLowerAscii(std::string_view sv)
{
    std::string out(sv);
    for (char &c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

int hexValue(char c) noexcept
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

std::string percentDecode(std::string_view sv)
{
    std::string out;
    out.reserve(sv.size());
    for (std::size_t i = 0; i < sv.size(); ++i) {
        if (sv[i] == '%' && i + 2 < sv.size()) {
            const int hi = hexValue(sv[i + 1]);
            const int lo = hexValue(sv[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(sv[i]);
    }
    return out;
}

bool extractQueryToken(std::string_view requestTarget, std::string *tokenOut)
{
    const std::size_t hashPos = requestTarget.find('#');
    if (hashPos != std::string_view::npos) {
        requestTarget = requestTarget.substr(0, hashPos);
    }
    const std::size_t qPos = requestTarget.find('?');
    if (qPos == std::string_view::npos) {
        return false;
    }
    std::string_view query = requestTarget.substr(qPos + 1);
    while (!query.empty()) {
        const std::size_t ampPos = query.find('&');
        const std::string_view pair = (ampPos == std::string_view::npos)
                                          ? query
                                          : query.substr(0, ampPos);
        const std::size_t eqPos = pair.find('=');
        if (eqPos != std::string_view::npos) {
            const std::string key = percentDecode(pair.substr(0, eqPos));
            if (key == "token") {
                *tokenOut = percentDecode(pair.substr(eqPos + 1));
                return true;
            }
        }
        if (ampPos == std::string_view::npos) {
            break;
        }
        query.remove_prefix(ampPos + 1);
    }
    return false;
}

} // namespace

WsServer::WsServer(const AnimeCore::String &token)
    : m_token(token)
{
    ensureWinsock();
}

WsServer::~WsServer()
{
    if (m_client != kInvalidSocket) {
        EventLoop::instance().removeSocket(m_client);
        ::closesocket(static_cast<SOCKET>(m_client));
        m_client = kInvalidSocket;
    }
    for (const PendingClient &pending : m_pendingClients) {
        if (pending.socket != kInvalidSocket) {
            EventLoop::instance().removeSocket(pending.socket);
            ::closesocket(static_cast<SOCKET>(pending.socket));
        }
    }
    m_pendingClients.clear();
    closeListener();
}

void WsServer::closeListener()
{
    if (m_listenSocket != kInvalidSocket) {
        EventLoop::instance().removeSocket(m_listenSocket);
        ::closesocket(static_cast<SOCKET>(m_listenSocket));
        m_listenSocket = kInvalidSocket;
    }
    m_port = 0;
}

bool WsServer::listen(std::uint16_t port)
{
    closeListener();

    SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        return false;
    }

    u_long nonBlocking = 1;
    if (::ioctlsocket(sock, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
        ::closesocket(sock);
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (::bind(sock, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        ::closesocket(sock);
        return false;
    }

    if (::listen(sock, SOMAXCONN) == SOCKET_ERROR) {
        ::closesocket(sock);
        return false;
    }

    sockaddr_in boundAddr{};
    int boundLen = sizeof(boundAddr);
    if (::getsockname(sock, reinterpret_cast<sockaddr *>(&boundAddr), &boundLen) == SOCKET_ERROR) {
        ::closesocket(sock);
        return false;
    }

    m_listenSocket = static_cast<SocketHandle>(sock);
    m_port = ntohs(boundAddr.sin_port);

    EventLoop::instance().addSocket(m_listenSocket, EventLoop::SocketRead, [this](unsigned int) {
        onAcceptReady();
    });
    return true;
}

std::uint16_t WsServer::port() const
{
    return m_port;
}

void WsServer::sendText(const std::string &data)
{
    sendFrame(kOpcodeText, data);
}

void WsServer::sendBinary(const std::string &data)
{
    sendFrame(kOpcodeBinary, data);
}

bool WsServer::hasClient() const
{
    return m_client != kInvalidSocket;
}

void WsServer::onAcceptReady()
{
    while (m_listenSocket != kInvalidSocket) {
        SOCKET accepted = ::accept(static_cast<SOCKET>(m_listenSocket), nullptr, nullptr);
        if (accepted == INVALID_SOCKET) {
            break;
        }

        u_long nonBlocking = 1;
        ::ioctlsocket(accepted, FIONBIO, &nonBlocking);

        BOOL noDelay = TRUE;
        ::setsockopt(accepted, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&noDelay), sizeof(noDelay));

        const SocketHandle handle = static_cast<SocketHandle>(accepted);
        PendingClient pending;
        pending.socket = handle;
        m_pendingClients.push_back(std::move(pending));

        EventLoop::instance().addSocket(handle, EventLoop::SocketRead, [this, handle](unsigned int events) {
            onPendingSocketEvent(handle, events);
        });
    }
}

void WsServer::onPendingSocketEvent(SocketHandle socket, unsigned int /*events*/)
{
    std::size_t idx = m_pendingClients.size();
    for (std::size_t i = 0; i < m_pendingClients.size(); ++i) {
        if (m_pendingClients[i].socket == socket) {
            idx = i;
            break;
        }
    }
    if (idx == m_pendingClients.size()) {
        return;
    }

    char chunk[kRecvChunkSize];
    bool peerClosed = false;
    for (;;) {
        const int n = ::recv(static_cast<SOCKET>(socket), chunk, static_cast<int>(sizeof(chunk)), 0);
        if (n > 0) {
            m_pendingClients[idx].buffer.append(chunk, static_cast<std::size_t>(n));
        } else if (n == 0) {
            peerClosed = true;
            break;
        } else {
            if (::WSAGetLastError() != WSAEWOULDBLOCK) {
                peerClosed = true;
            }
            break;
        }
    }

    if (tryCompleteHandshake(socket)) {
        return;
    }

    if (peerClosed) {
        closeAndRemovePending(socket);
        return;
    }

    for (const PendingClient &p : m_pendingClients) {
        if (p.socket == socket) {
            if (p.buffer.size() > kMaxHandshakeHeaderSize) {
                rejectHttp(socket, 400, "Bad Request");
            }
            return;
        }
    }
}

void WsServer::removePending(SocketHandle socket)
{
    for (auto it = m_pendingClients.begin(); it != m_pendingClients.end(); ++it) {
        if (it->socket == socket) {
            m_pendingClients.erase(it);
            return;
        }
    }
}

void WsServer::closeAndRemovePending(SocketHandle socket)
{
    removePending(socket);
    if (socket != kInvalidSocket) {
        EventLoop::instance().removeSocket(socket);
        ::closesocket(static_cast<SOCKET>(socket));
    }
}

bool WsServer::isOriginAllowed(const std::string &originRaw)
{
    const std::string origin = trimAscii(originRaw);
    if (origin.empty()) {
        return true;
    }
    if (origin == "tauri://localhost"
        || origin == "http://tauri.localhost"
        || origin == "https://tauri.localhost") {
        return true;
    }
    constexpr std::string_view kLocalhostPrefix = "http://localhost:";
    if (origin.size() > kLocalhostPrefix.size()
        && std::memcmp(origin.data(), kLocalhostPrefix.data(), kLocalhostPrefix.size()) == 0) {
        const std::string_view portPart(origin.data() + kLocalhostPrefix.size(),
                                        origin.size() - kLocalhostPrefix.size());
        if (portPart.empty() || portPart.size() > 5) {
            return false;
        }
        int portNum = 0;
        for (char c : portPart) {
            if (c < '0' || c > '9') {
                return false;
            }
            portNum = portNum * 10 + (c - '0');
        }
        return portNum >= 1 && portNum <= 65535;
    }
    return false;
}

void WsServer::rejectHttp(SocketHandle socket, int statusCode, const char *reasonPhrase)
{
    removePending(socket);
    if (socket == kInvalidSocket) {
        return;
    }
    EventLoop::instance().removeSocket(socket);

    const std::string response =
        "HTTP/1.1 " + std::to_string(statusCode) + " " + (reasonPhrase ? reasonPhrase : "") + "\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";

    ::send(static_cast<SOCKET>(socket), response.data(), static_cast<int>(response.size()), 0);
    ::shutdown(static_cast<SOCKET>(socket), SD_SEND);
    ::closesocket(static_cast<SOCKET>(socket));
}

bool WsServer::tryCompleteHandshake(SocketHandle socket)
{
    std::size_t pendingIdx = m_pendingClients.size();
    for (std::size_t i = 0; i < m_pendingClients.size(); ++i) {
        if (m_pendingClients[i].socket == socket) {
            pendingIdx = i;
            break;
        }
    }
    if (pendingIdx == m_pendingClients.size()) {
        return true;
    }

    const std::size_t headerEnd = m_pendingClients[pendingIdx].buffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return false;
    }

    const std::string headerBlock = m_pendingClients[pendingIdx].buffer.substr(0, headerEnd);
    std::string remainingBuffer = m_pendingClients[pendingIdx].buffer.substr(headerEnd + 4);
    removePending(socket);

    std::vector<std::string> lines;
    {
        std::size_t pos = 0;
        while (pos <= headerBlock.size()) {
            const std::size_t nl = headerBlock.find('\n', pos);
            if (nl == std::string::npos) {
                lines.push_back(trimAscii(std::string_view(headerBlock).substr(pos)));
                break;
            }
            lines.push_back(trimAscii(std::string_view(headerBlock).substr(pos, nl - pos)));
            pos = nl + 1;
        }
    }

    if (lines.empty() || lines[0].empty()) {
        rejectHttp(socket, 400, "Bad Request");
        return true;
    }

    // Parse "GET <target> HTTP/1.x"
    const std::string &requestLine = lines[0];
    const std::size_t sp1 = requestLine.find(' ');
    const std::size_t sp2 = (sp1 == std::string::npos) ? std::string::npos : requestLine.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) {
        rejectHttp(socket, 400, "Bad Request");
        return true;
    }
    const std::string method = requestLine.substr(0, sp1);
    const std::string requestTarget = requestLine.substr(sp1 + 1, sp2 - (sp1 + 1));
    const std::string httpVersion = trimAscii(std::string_view(requestLine).substr(sp2 + 1));
    if (method != "GET" || httpVersion.rfind("HTTP/1.", 0) != 0) {
        rejectHttp(socket, 400, "Bad Request");
        return true;
    }

    std::string upgradeValue;
    std::string wsKeyValue;
    std::string wsVersionValue;
    std::string originValue;
    bool hasOrigin = false;

    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::string &line = lines[i];
        if (line.empty()) {
            continue;
        }
        const std::size_t colonIdx = line.find(':');
        if (colonIdx == std::string::npos || colonIdx == 0) {
            continue;
        }
        const std::string name = toLowerAscii(trimAscii(std::string_view(line).substr(0, colonIdx)));
        const std::string value = trimAscii(std::string_view(line).substr(colonIdx + 1));

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

    if (toLowerAscii(upgradeValue).find("websocket") == std::string::npos || wsKeyValue.empty()) {
        rejectHttp(socket, 400, "Bad Request");
        return true;
    }

    if (!wsVersionValue.empty() && wsVersionValue != "13") {
        rejectHttp(socket, 426, "Upgrade Required");
        return true;
    }

    if (hasOrigin && !isOriginAllowed(originValue)) {
        rejectHttp(socket, 403, "Forbidden");
        return true;
    }

    std::string queryToken;
    if (!extractQueryToken(requestTarget, &queryToken) || queryToken != m_token.toUtf8()) {
        rejectHttp(socket, 401, "Unauthorized");
        return true;
    }

    const std::string acceptInput = wsKeyValue + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char sha1Digest[20];
    computeSha1(reinterpret_cast<const unsigned char *>(acceptInput.data()), acceptInput.size(), sha1Digest);
    const std::string acceptKey = AnimeCore::toBase64(sha1Digest, sizeof(sha1Digest));

    const std::string handshakeResponse =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + acceptKey + "\r\n"
        "\r\n";

    promoteClient(socket, std::move(remainingBuffer));
    queueWrite(handshakeResponse.data(), handshakeResponse.size());
    if (m_client != socket) {
        return true; // The send failed and has already reported the disconnect.
    }

    clientConnected();

    if (m_client == socket && !m_readBuffer.empty()) {
        processFrames(socket);
    }
    return true;
}

void WsServer::promoteClient(SocketHandle socket, std::string initialBuffer)
{
    if (m_client != kInvalidSocket) {
        disconnectActiveClient(1000);
    }

    m_client = socket;
    m_readBuffer = std::move(initialBuffer);
    m_writeBuffer.clear();
    m_writeOffset = 0;
    m_watchingWrite = false;
    m_inFragment = false;
    m_fragmentOpcode = 0;
    m_fragmentBuffer.clear();

    EventLoop::instance().addSocket(socket, EventLoop::SocketRead, [this, socket](unsigned int events) {
        onClientSocketEvent(socket, events);
    });
}

void WsServer::disconnectActiveClient(std::uint16_t closeCode)
{
    if (m_client == kInvalidSocket) {
        return;
    }

    const SocketHandle oldClient = m_client;
    // A close frame sent now would land in the middle of the half-written one.
    const bool midFrame = m_writeOffset > 0 && m_writeOffset < m_writeBuffer.size();
    m_client = kInvalidSocket;
    m_readBuffer.clear();
    m_writeBuffer.clear();
    m_writeOffset = 0;
    m_watchingWrite = false;
    m_inFragment = false;
    m_fragmentOpcode = 0;
    m_fragmentBuffer.clear();

    EventLoop::instance().removeSocket(oldClient);
    if (closeCode != 0 && !midFrame) {
        sendCloseFrameImmediate(oldClient, closeCode);
    }
    ::shutdown(static_cast<SOCKET>(oldClient), SD_SEND);
    ::closesocket(static_cast<SOCKET>(oldClient));

    clientDisconnected();
}

void WsServer::onClientDisconnected()
{
    if (m_client == kInvalidSocket) {
        return;
    }

    const SocketHandle oldClient = m_client;
    // A close frame sent now would land in the middle of the half-written one.
    const bool midFrame = m_writeOffset > 0 && m_writeOffset < m_writeBuffer.size();
    m_client = kInvalidSocket;
    m_readBuffer.clear();
    m_writeBuffer.clear();
    m_writeOffset = 0;
    m_watchingWrite = false;
    m_inFragment = false;
    m_fragmentOpcode = 0;
    m_fragmentBuffer.clear();

    EventLoop::instance().removeSocket(oldClient);
    ::closesocket(static_cast<SOCKET>(oldClient));

    clientDisconnected();
}

void WsServer::onClientSocketEvent(SocketHandle socket, unsigned int events)
{
    if (m_client != socket) {
        return;
    }

    if (events & EventLoop::SocketWrite) {
        flushWriteBuffer();
        if (m_client != socket) {
            return;
        }
    }

    if (events & (EventLoop::SocketRead | EventLoop::SocketError)) {
        char chunk[kRecvChunkSize];
        bool peerClosed = false;
        for (;;) {
            const int n = ::recv(static_cast<SOCKET>(socket), chunk, static_cast<int>(sizeof(chunk)), 0);
            if (n > 0) {
                m_readBuffer.append(chunk, static_cast<std::size_t>(n));
            } else if (n == 0) {
                peerClosed = true;
                break;
            } else {
                if (::WSAGetLastError() != WSAEWOULDBLOCK) {
                    peerClosed = true;
                }
                break;
            }
        }

        if (!m_readBuffer.empty()) {
            if (!processFrames(socket)) {
                return;
            }
        }

        if (peerClosed && m_client == socket) {
            onClientDisconnected();
        }
    }
}

bool WsServer::processFrames(SocketHandle socket)
{
    std::size_t offset = 0;

    while (m_client == socket) {
        const std::size_t avail = m_readBuffer.size() - offset;
        if (avail < 2) {
            break;
        }

        const unsigned char *ptr =
            reinterpret_cast<const unsigned char *>(m_readBuffer.data()) + offset;
        const bool fin = (ptr[0] & 0x80u) != 0;
        const std::uint8_t rsv = (ptr[0] & 0x70u);
        const std::uint8_t opcode = (ptr[0] & 0x0Fu);
        const bool masked = (ptr[1] & 0x80u) != 0;
        const std::uint8_t len7 = (ptr[1] & 0x7Fu);

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

        const bool isControl = (opcode & 0x08u) != 0;
        if (isControl && (!fin || len7 > 125)) {
            disconnectActiveClient(1002);
            return false;
        }

        std::size_t headerLen = 2 + 4;
        std::uint64_t payloadLen = len7;

        if (len7 == 126) {
            headerLen = 2 + 2 + 4;
            if (avail < headerLen) {
                break;
            }
            payloadLen = readBe16(ptr + 2);
        } else if (len7 == 127) {
            headerLen = 2 + 8 + 4;
            if (avail < headerLen) {
                break;
            }
            payloadLen = readBe64(ptr + 2);
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
                if (static_cast<std::uint64_t>(m_fragmentBuffer.size()) + payloadLen > kMaxMessageSize) {
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

        const std::uint64_t totalFrameLen = static_cast<std::uint64_t>(headerLen) + payloadLen;
        if (static_cast<std::uint64_t>(avail) < totalFrameLen) {
            break;
        }

        const unsigned char *maskKey = ptr + (headerLen - 4);
        const unsigned char *src = ptr + headerLen;

        std::string payload(static_cast<std::size_t>(payloadLen), '\0');
        unsigned char *dst = reinterpret_cast<unsigned char *>(payload.data());
        for (std::size_t i = 0; i < static_cast<std::size_t>(payloadLen); ++i) {
            dst[i] = src[i] ^ maskKey[i & 3u];
        }

        offset += static_cast<std::size_t>(totalFrameLen);
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
            std::uint16_t closeCode = 1000;
            if (payload.size() >= 2) {
                closeCode = readBe16(reinterpret_cast<const unsigned char *>(payload.data()));
            }
            disconnectActiveClient(closeCode);
            return false;
        } else if (opcode == kOpcodeText || opcode == kOpcodeBinary) {
            if (fin) {
                if (opcode == kOpcodeText) {
                    textReceived(payload);
                } else {
                    binaryReceived(payload);
                }
            } else {
                m_inFragment = true;
                m_fragmentOpcode = opcode;
                m_fragmentBuffer = std::move(payload);
            }
        } else if (opcode == kOpcodeContinuation) {
            m_fragmentBuffer.append(payload);
            if (fin) {
                std::string completeMessage = std::move(m_fragmentBuffer);
                m_fragmentBuffer.clear();
                const std::uint8_t finalOpcode = m_fragmentOpcode;
                m_inFragment = false;
                m_fragmentOpcode = 0;

                if (finalOpcode == kOpcodeText) {
                    textReceived(completeMessage);
                } else {
                    binaryReceived(completeMessage);
                }
            }
        }
    }

    if (m_client == socket && offset > 0) {
        m_readBuffer.erase(0, offset);
    }
    return m_client == socket;
}

void WsServer::sendFrame(std::uint8_t opcode, const std::string &payload)
{
    if (!hasClient()) {
        return;
    }

    const std::uint64_t len = static_cast<std::uint64_t>(payload.size());
    unsigned char header[10];
    std::size_t headerSize = 0;
    header[headerSize++] = static_cast<unsigned char>(0x80u | (opcode & 0x0Fu));

    if (len <= 125) {
        header[headerSize++] = static_cast<unsigned char>(len);
    } else if (len <= 65535) {
        header[headerSize++] = 126u;
        writeBe16(header + headerSize, static_cast<std::uint16_t>(len));
        headerSize += 2;
    } else {
        header[headerSize++] = 127u;
        writeBe64(header + headerSize, len);
        headerSize += 8;
    }

    m_writeBuffer.append(reinterpret_cast<const char *>(header), headerSize);
    if (!payload.empty()) {
        m_writeBuffer.append(payload.data(), payload.size());
    }
    flushWriteBuffer();
}

void WsServer::sendCloseFrameImmediate(SocketHandle socket, std::uint16_t closeCode)
{
    if (socket == kInvalidSocket) {
        return;
    }
    unsigned char frame[4];
    frame[0] = static_cast<unsigned char>(0x80u | kOpcodeClose);
    frame[1] = 2u;
    writeBe16(frame + 2, closeCode);
    ::send(static_cast<SOCKET>(socket), reinterpret_cast<const char *>(frame), sizeof(frame), 0);
}

void WsServer::queueWrite(const char *data, std::size_t size)
{
    if (!hasClient() || !data || size == 0) {
        return;
    }
    m_writeBuffer.append(data, size);
    flushWriteBuffer();
}

void WsServer::flushWriteBuffer()
{
    if (m_client == kInvalidSocket) {
        return;
    }

    const SocketHandle sock = m_client;
    while (m_writeOffset < m_writeBuffer.size()) {
        const std::size_t remaining = m_writeBuffer.size() - m_writeOffset;
        const int chunk = static_cast<int>(std::min<std::size_t>(remaining, 16 * 1024 * 1024));
        const int sent = ::send(static_cast<SOCKET>(sock),
                                m_writeBuffer.data() + m_writeOffset,
                                chunk,
                                0);
        if (sent > 0) {
            m_writeOffset += static_cast<std::size_t>(sent);
        } else if (sent == SOCKET_ERROR) {
            if (::WSAGetLastError() == WSAEWOULDBLOCK) {
                break;
            }
            onClientDisconnected();
            return;
        } else {
            break;
        }
    }

    if (m_writeOffset >= m_writeBuffer.size()) {
        m_writeBuffer.clear();
        m_writeOffset = 0;
        if (m_writeBuffer.capacity() > 1024 * 1024) {
            m_writeBuffer.shrink_to_fit();
        }
    } else if (m_writeOffset >= kWriteCompactThreshold && m_writeOffset * 2 >= m_writeBuffer.size()) {
        m_writeBuffer.erase(0, m_writeOffset);
        m_writeOffset = 0;
    }

    updateClientPollEvents();
}

void WsServer::updateClientPollEvents()
{
    if (m_client == kInvalidSocket) {
        return;
    }
    const bool needWrite = (m_writeOffset < m_writeBuffer.size());
    if (needWrite != m_watchingWrite) {
        m_watchingWrite = needWrite;
        const unsigned int events = EventLoop::SocketRead
                                    | (needWrite ? EventLoop::SocketWrite : EventLoop::SocketNone);
        EventLoop::instance().updateSocket(m_client, events);
    }
}
