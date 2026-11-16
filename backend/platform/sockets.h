#ifndef ANIMEAN_BACKEND_PLATFORM_SOCKETS_H
#define ANIMEAN_BACKEND_PLATFORM_SOCKETS_H

// TCP sockets on 127.0.0.1, the same calls over Winsock and over POSIX sockets.
// The event loop and the WebSocket server use these and never include a system
// socket header themselves. Every socket made here is non-blocking.

#include <cstddef>
#include <cstdint>

namespace Sockets {

using Handle = std::uintptr_t;
constexpr Handle kInvalid = ~Handle(0);

// Starts the system's socket layer (Winsock needs it; elsewhere nothing to do).
// Called by everything below, so only code that makes sockets some other way needs it.
void ensureStarted();

// A listener on 127.0.0.1. Port 0 lets the system choose; *boundPort gets the
// port in use. kInvalid when the port cannot be had.
Handle listenOnLoopback(std::uint16_t port, std::uint16_t *boundPort);
// The next waiting connection, with small writes sent at once (no Nagle delay).
// kInvalid when none is waiting.
Handle acceptClient(Handle listener);
// Starts connecting to a listener on 127.0.0.1; the socket turns writable once
// the connection is made.
Handle connectToLoopback(std::uint16_t port);

enum class Io {
    Ok,         // *transferred bytes moved
    WouldBlock, // nothing can move right now, wait for the socket to be ready
    Closed,     // receive only: the peer has finished sending
    Failed,     // the connection is broken
};
Io receive(Handle socket, char *buffer, std::size_t capacity, std::size_t *transferred);
Io send(Handle socket, const char *data, std::size_t size, std::size_t *transferred);

// No more sending: the peer reads to the end of what was sent, then sees the end.
void shutdownSend(Handle socket);
void close(Handle socket);

enum Readiness : unsigned int {
    Readable = 1u << 0,
    Writable = 1u << 1,
    Broken = 1u << 2,
};

struct PollItem {
    Handle socket = kInvalid;
    unsigned int wanted = 0; // Readable | Writable
    unsigned int ready = 0;  // filled in: Readable | Writable | Broken
};

// Waits until one of the sockets is ready or timeoutMs has passed (-1: no limit).
// Returns how many items have something in `ready`, 0 on timeout, -1 on error.
int poll(PollItem *items, std::size_t count, int timeoutMs);

// Gives the processor away for this long.
void sleepMs(int ms);

} // namespace Sockets

#endif // ANIMEAN_BACKEND_PLATFORM_SOCKETS_H
