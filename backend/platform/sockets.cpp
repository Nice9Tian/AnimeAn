#include "sockets.h"

#include <algorithm>
#include <vector>

// ---- what differs between the two socket layers ---------------------------------
// Winsock and POSIX sockets are the same design with different spellings. The
// differences are named here once; everything after this block is shared.

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace {

using NativeSocket = SOCKET;
using NativePollItem = WSAPOLLFD;
using AddressLength = int;
constexpr NativeSocket kNativeInvalid = INVALID_SOCKET;
constexpr int kShutdownSend = SD_SEND;
constexpr int kSendFlags = 0;
constexpr short kWantRead = POLLRDNORM;
constexpr short kWantWrite = POLLWRNORM;
constexpr short kGotRead = POLLRDNORM | POLLRDBAND | POLLPRI;
constexpr short kGotWrite = POLLWRNORM;

int lastError() noexcept { return ::WSAGetLastError(); }
bool isWouldBlock(int error) noexcept { return error == WSAEWOULDBLOCK; }
bool isInterrupted(int) noexcept { return false; }
bool isConnecting(int error) noexcept { return error == WSAEWOULDBLOCK; }
void closeNative(NativeSocket socket) noexcept { ::closesocket(socket); }

bool makeNonBlocking(NativeSocket socket) noexcept
{
    u_long on = 1;
    return ::ioctlsocket(socket, FIONBIO, &on) != SOCKET_ERROR;
}

// A new non-blocking TCP socket.
NativeSocket openStreamSocket() noexcept
{
    const NativeSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket != kNativeInvalid && !makeNonBlocking(socket)) {
        closeNative(socket);
        return kNativeInvalid;
    }
    return socket;
}

// The next waiting connection, non-blocking.
NativeSocket acceptStream(NativeSocket listener) noexcept
{
    const NativeSocket accepted = ::accept(listener, nullptr, nullptr);
    if (accepted != kNativeInvalid) {
        makeNonBlocking(accepted);
    }
    return accepted;
}

void prepareListener(NativeSocket) noexcept {}

int pollNative(NativePollItem *items, std::size_t count, int timeoutMs) noexcept
{
    return ::WSAPoll(items, static_cast<ULONG>(count), timeoutMs);
}

} // namespace

#else

#include <cerrno>
#include <ctime>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sched.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

using NativeSocket = int;
using NativePollItem = pollfd;
using AddressLength = socklen_t;
constexpr NativeSocket kNativeInvalid = -1;
constexpr int kShutdownSend = SHUT_WR;
// A write to a peer that has gone reports an error instead of raising SIGPIPE,
// which would end the process.
constexpr int kSendFlags = MSG_NOSIGNAL;
constexpr short kWantRead = POLLIN;
constexpr short kWantWrite = POLLOUT;
constexpr short kGotRead = POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI;
constexpr short kGotWrite = POLLOUT | POLLWRNORM;

int lastError() noexcept { return errno; }
bool isWouldBlock(int error) noexcept { return error == EAGAIN || error == EWOULDBLOCK; }
bool isInterrupted(int error) noexcept { return error == EINTR; }
bool isConnecting(int error) noexcept { return error == EINPROGRESS || error == EINTR; }
void closeNative(NativeSocket socket) noexcept { ::close(socket); }

// Sockets are non-blocking, and closed in any program this one starts (pip, a
// script's subprocess), from the moment they exist: set afterwards, a program
// started from another thread in between would inherit the socket and keep the
// connection open after it was closed here. SOCK_NONBLOCK, SOCK_CLOEXEC and
// accept4 are what Linux offers for this.

// A new non-blocking TCP socket.
NativeSocket openStreamSocket() noexcept
{
    return ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
}

// The next waiting connection, non-blocking.
NativeSocket acceptStream(NativeSocket listener) noexcept
{
    NativeSocket accepted = kNativeInvalid;
    do {
        accepted = ::accept4(listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    } while (accepted == kNativeInvalid && errno == EINTR);
    return accepted;
}

// A port is free again as soon as its listener is gone, instead of staying
// taken for a minute or so while old connections time out. A second listener
// on a port still in use is refused either way.
void prepareListener(NativeSocket socket) noexcept
{
    int on = 1;
    ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
}

int pollNative(NativePollItem *items, std::size_t count, int timeoutMs) noexcept
{
    return ::poll(items, static_cast<nfds_t>(count), timeoutMs);
}

} // namespace

#endif

// ---- shared ---------------------------------------------------------------------

namespace {

constexpr std::size_t kMaxTransfer = std::size_t(1) << 30;

NativeSocket toNative(Sockets::Handle handle) noexcept
{
    return static_cast<NativeSocket>(handle);
}

Sockets::Handle toHandle(NativeSocket socket) noexcept
{
    return socket == kNativeInvalid ? Sockets::kInvalid : static_cast<Sockets::Handle>(socket);
}

sockaddr_in loopbackAddress(std::uint16_t port) noexcept
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    return address;
}

} // namespace

namespace Sockets {

void ensureStarted()
{
#ifdef _WIN32
    static const bool started = []() {
        WSADATA data{};
        return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)started;
#endif
}

Handle listenOnLoopback(std::uint16_t port, std::uint16_t *boundPort)
{
    ensureStarted();
    const NativeSocket socket = openStreamSocket();
    if (socket == kNativeInvalid)
        return kInvalid;
    prepareListener(socket);

    const sockaddr_in address = loopbackAddress(port);
    sockaddr_in bound{};
    AddressLength boundLength = sizeof(bound);
    if (::bind(socket, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0
        || ::listen(socket, SOMAXCONN) != 0
        || ::getsockname(socket, reinterpret_cast<sockaddr *>(&bound), &boundLength) != 0) {
        closeNative(socket);
        return kInvalid;
    }

    if (boundPort)
        *boundPort = ntohs(bound.sin_port);
    return toHandle(socket);
}

Handle acceptClient(Handle listener)
{
    const NativeSocket accepted = acceptStream(toNative(listener));
    if (accepted == kNativeInvalid)
        return kInvalid;

    int noDelay = 1;
    ::setsockopt(accepted, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&noDelay), sizeof(noDelay));
    return toHandle(accepted);
}

Handle connectToLoopback(std::uint16_t port)
{
    ensureStarted();
    const NativeSocket socket = openStreamSocket();
    if (socket == kNativeInvalid)
        return kInvalid;

    const sockaddr_in address = loopbackAddress(port);
    if (::connect(socket, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0
        && !isConnecting(lastError())) {
        closeNative(socket);
        return kInvalid;
    }
    return toHandle(socket);
}

Io receive(Handle socket, char *buffer, std::size_t capacity, std::size_t *transferred)
{
    *transferred = 0;
    const int want = static_cast<int>(std::min(capacity, kMaxTransfer));
    for (;;) {
        const auto got = ::recv(toNative(socket), buffer, want, 0);
        if (got > 0) {
            *transferred = static_cast<std::size_t>(got);
            return Io::Ok;
        }
        if (got == 0)
            return Io::Closed;
        const int error = lastError();
        if (isInterrupted(error))
            continue;
        return isWouldBlock(error) ? Io::WouldBlock : Io::Failed;
    }
}

Io send(Handle socket, const char *data, std::size_t size, std::size_t *transferred)
{
    *transferred = 0;
    const int want = static_cast<int>(std::min(size, kMaxTransfer));
    for (;;) {
        const auto put = ::send(toNative(socket), data, want, kSendFlags);
        if (put >= 0) {
            *transferred = static_cast<std::size_t>(put);
            return Io::Ok;
        }
        const int error = lastError();
        if (isInterrupted(error))
            continue;
        return isWouldBlock(error) ? Io::WouldBlock : Io::Failed;
    }
}

void shutdownSend(Handle socket)
{
    if (socket != kInvalid)
        ::shutdown(toNative(socket), kShutdownSend);
}

void close(Handle socket)
{
    if (socket != kInvalid)
        closeNative(toNative(socket));
}

int poll(PollItem *items, std::size_t count, int timeoutMs)
{
    if (count == 0) {
        if (timeoutMs > 0)
            sleepMs(timeoutMs);
        return 0;
    }

    std::vector<NativePollItem> native(count);
    for (std::size_t i = 0; i < count; ++i) {
        native[i].fd = toNative(items[i].socket);
        native[i].events = static_cast<short>(((items[i].wanted & Readable) ? kWantRead : 0)
                                              | ((items[i].wanted & Writable) ? kWantWrite : 0));
        native[i].revents = 0;
        items[i].ready = 0;
    }

    const int result = pollNative(native.data(), count, timeoutMs);
    if (result <= 0)
        return (result < 0 && isInterrupted(lastError())) ? 0 : result;

    int readyCount = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const short got = native[i].revents;
        if (got & kGotRead)
            items[i].ready |= Readable;
        if (got & kGotWrite)
            items[i].ready |= Writable;
        if (got & (POLLERR | POLLHUP | POLLNVAL))
            items[i].ready |= Broken;
        if (items[i].ready != 0)
            ++readyCount;
    }
    return readyCount;
}

void sleepMs(int ms)
{
    if (ms < 0)
        return;
#ifdef _WIN32
    ::Sleep(static_cast<DWORD>(ms)); // 0 gives up the rest of this time slice
#else
    if (ms == 0) {
        ::sched_yield();
        return;
    }
    timespec duration{};
    duration.tv_sec = ms / 1000;
    duration.tv_nsec = static_cast<long>(ms % 1000) * 1000000L;
    ::nanosleep(&duration, nullptr);
#endif
}

} // namespace Sockets
