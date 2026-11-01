#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "platform/elapsedtimer.h"
#include "platform/eventloop.h"
#include "platform/signalslot.h"
#include "platform/timer.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

bool testSignal()
{
    Signal<int, std::string> sig;
    std::vector<std::string> log;

    const auto id1 = sig.connect([&](int n, const std::string &s) {
        log.push_back("c1:" + std::to_string(n) + ":" + s);
    });
    const auto id2 = sig.connect([&](int n, const std::string &s) {
        log.push_back("c2:" + std::to_string(n) + ":" + s);
    });
    (void)id1;

    sig(1, "a");
    if (log != std::vector<std::string>{"c1:1:a", "c2:1:a"}) {
        std::fprintf(stderr, "Signal order failed\n");
        return false;
    }

    // Disconnect inside a callback: c3 disconnects c4 before c4 runs, and disconnects itself.
    log.clear();
    sig.disconnectAll();

    Signal<>::ConnectionId id3 = 0;
    Signal<>::ConnectionId id4 = 0;
    Signal<> simpleSig;

    id3 = simpleSig.connect([&]() {
        log.push_back("c3");
        simpleSig.disconnect(id4);
        simpleSig.disconnect(id3);
        simpleSig.connect([&]() {
            log.push_back("c5_added_in_cb");
        });
    });
    id4 = simpleSig.connect([&]() {
        log.push_back("c4_should_not_run");
    });

    (void)id2;
    simpleSig();
    if (log != std::vector<std::string>{"c3"}) {
        std::fprintf(stderr, "Signal disconnect-in-callback failed on first emit\n");
        return false;
    }

    log.clear();
    simpleSig();
    if (log != std::vector<std::string>{"c5_added_in_cb"}) {
        std::fprintf(stderr, "Signal connect-in-callback failed on second emit\n");
        return false;
    }

    return true;
}

bool testElapsedTimer()
{
    ElapsedTimer timer;
    if (timer.isValid() || timer.elapsed() != 0 || timer.nsecsElapsed() != 0) {
        std::fprintf(stderr, "ElapsedTimer initial invalid state failed\n");
        return false;
    }

    timer.start();
    if (!timer.isValid() || timer.hasExpired(-1)) {
        std::fprintf(stderr, "ElapsedTimer start / negative timeout failed\n");
        return false;
    }

    ::Sleep(25);
    const std::int64_t ms = timer.elapsed();
    const std::int64_t ns = timer.nsecsElapsed();
    if (ms < 15 || ns < 15000000LL || !timer.hasExpired(10) || timer.hasExpired(5000)) {
        std::fprintf(stderr, "ElapsedTimer elapsed measurement failed (ms=%lld, ns=%lld)\n",
                     static_cast<long long>(ms),
                     static_cast<long long>(ns));
        return false;
    }

    const std::int64_t restarted = timer.restart();
    if (restarted < 15 || !timer.isValid()) {
        std::fprintf(stderr, "ElapsedTimer restart failed\n");
        return false;
    }

    timer.invalidate();
    if (timer.isValid()) {
        std::fprintf(stderr, "ElapsedTimer invalidate failed\n");
        return false;
    }

    return true;
}

bool testPostOrderAndNextRound()
{
    EventLoop &loop = EventLoop::instance();
    std::vector<std::string> order;

    loop.post([&]() {
        order.push_back("A");
        loop.post([&]() {
            order.push_back("D_next_round");
            loop.quit(0);
        });
    });
    loop.post([&]() {
        order.push_back("B");
    });
    loop.post([&]() {
        order.push_back("C");
    });

    const int rc = loop.exec();
    const std::vector<std::string> expected{"A", "B", "C", "D_next_round"};
    if (rc != 0 || order != expected) {
        std::fprintf(stderr, "Post order failed\n");
        return false;
    }
    return true;
}

bool testTimerOrderAndZeroMs()
{
    EventLoop &loop = EventLoop::instance();
    std::vector<std::string> order;

    // Created in order t1, t2, t3; armed with same 0ms deadline in reverse order.
    // Must fire in creation order: t1, t2, t3.
    Timer t1;
    Timer t2;
    Timer t3;
    t1.setSingleShot(true);
    t2.setSingleShot(true);
    t3.setSingleShot(true);

    Timer tLate;
    Timer tEarly;
    tLate.setSingleShot(true);
    tEarly.setSingleShot(true);

    bool zeroInCallbackVerified = false;
    bool ranSynchronously = false;

    t1.timeout.connect([&]() { order.push_back("t1_0ms"); });
    t2.timeout.connect([&]() { order.push_back("t2_0ms"); });
    t3.timeout.connect([&]() {
        order.push_back("t3_0ms");
        // A 0ms timer started inside a callback must run on the NEXT iteration,
        // not synchronously inside start(0).
        ranSynchronously = true;
        Timer::singleShot(0, [&]() {
            if (!ranSynchronously) {
                zeroInCallbackVerified = true;
            }
            order.push_back("nested_0ms");
        });
        ranSynchronously = false;
    });

    tEarly.timeout.connect([&]() { order.push_back("early_15ms"); });
    tLate.timeout.connect([&]() {
        order.push_back("late_35ms");
        loop.quit(0);
    });

    t3.start(0);
    t1.start(0);
    t2.start(0);
    tLate.start(35);
    tEarly.start(15);

    loop.exec();

    const std::vector<std::string> expected{
        "t1_0ms",
        "t2_0ms",
        "t3_0ms",
        "nested_0ms",
        "early_15ms",
        "late_35ms"
    };
    if (!zeroInCallbackVerified || order != expected) {
        std::fprintf(stderr, "Timer order failed (got %zu items)\n", order.size());
        for (const auto &s : order) {
            std::fprintf(stderr, "  item: %s\n", s.c_str());
        }
        return false;
    }
    return true;
}

bool testTimerRepeatStopAndDeleteInCallback()
{
    EventLoop &loop = EventLoop::instance();
    int repeatCount = 0;
    bool singleFired = false;
    bool cancelledSiblingFired = false;
    bool selfDeletedFired = false;

    Timer repeating;
    repeating.setSingleShot(false);
    repeating.setInterval(10);

    Timer *heapTimer = new Timer();
    heapTimer->setSingleShot(false);
    heapTimer->setInterval(5);

    Timer siblingToStop;
    siblingToStop.setSingleShot(true);
    siblingToStop.timeout.connect([&]() {
        cancelledSiblingFired = true;
    });

    heapTimer->timeout.connect([&]() {
        selfDeletedFired = true;
        siblingToStop.stop();
        delete heapTimer;
        heapTimer = nullptr;
    });

    Timer::singleShot(5, [&]() {
        singleFired = true;
    });

    repeating.timeout.connect([&]() {
        ++repeatCount;
        if (repeatCount == 3) {
            repeating.stop();
            // Verify it does not fire a 4th time before quitting.
            Timer::singleShot(25, [&]() {
                loop.quit(0);
            });
        }
    });

    heapTimer->start();
    siblingToStop.start(5);
    repeating.start();

    loop.exec();

    if (!singleFired || !selfDeletedFired || cancelledSiblingFired || repeatCount != 3 || repeating.isActive()) {
        std::fprintf(stderr,
                     "Timer repeat/stop/delete failed: single=%d selfDel=%d sib=%d repeat=%d active=%d\n",
                     int(singleFired),
                     int(selfDeletedFired),
                     int(cancelledSiblingFired),
                     repeatCount,
                     int(repeating.isActive()));
        return false;
    }
    return true;
}

bool testSocketReadinessAndFairness()
{
    EventLoop &loop = EventLoop::instance();

    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        std::fprintf(stderr, "Failed to create listener socket\n");
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (::bind(listener, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR
        || ::listen(listener, 1) == SOCKET_ERROR) {
        ::closesocket(listener);
        std::fprintf(stderr, "Failed to bind/listen\n");
        return false;
    }

    int addrLen = sizeof(addr);
    ::getsockname(listener, reinterpret_cast<sockaddr *>(&addr), &addrLen);

    SOCKET client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    u_long nonBlocking = 1;
    ::ioctlsocket(listener, FIONBIO, &nonBlocking);
    ::ioctlsocket(client, FIONBIO, &nonBlocking);

    ::connect(client, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));

    SOCKET serverPeer = INVALID_SOCKET;
    std::string received;
    int postChainCount = 0;

    // Keep posting tasks from within posted tasks to verify socket callbacks are not starved.
    std::function<void()> repostTask;
    repostTask = [&]() {
        ++postChainCount;
        if (received == "ping-socket") {
            loop.quit(0);
        } else {
            loop.post(repostTask);
        }
    };
    loop.post(repostTask);

    loop.addSocket(static_cast<EventLoop::SocketHandle>(listener),
                   EventLoop::SocketRead,
                   [&](unsigned int) {
                       serverPeer = ::accept(listener, nullptr, nullptr);
                       if (serverPeer == INVALID_SOCKET) {
                           return;
                       }
                       u_long nb = 1;
                       ::ioctlsocket(serverPeer, FIONBIO, &nb);
                       loop.removeSocket(static_cast<EventLoop::SocketHandle>(listener));

                       loop.addSocket(static_cast<EventLoop::SocketHandle>(serverPeer),
                                      EventLoop::SocketRead,
                                      [&](unsigned int) {
                                          char buf[64]{};
                                          const int n = ::recv(serverPeer, buf, sizeof(buf), 0);
                                          if (n > 0) {
                                              received.append(buf, static_cast<std::size_t>(n));
                                              if (received == "ping-socket") {
                                                  loop.removeSocket(static_cast<EventLoop::SocketHandle>(serverPeer));
                                              }
                                          }
                                      });
                   });

    loop.addSocket(static_cast<EventLoop::SocketHandle>(client),
                   EventLoop::SocketWrite,
                   [&](unsigned int) {
                       const char *msg = "ping-socket";
                       ::send(client, msg, static_cast<int>(std::strlen(msg)), 0);
                       loop.removeSocket(static_cast<EventLoop::SocketHandle>(client));
                   });

    Timer safetyTimeout;
    safetyTimeout.setSingleShot(true);
    safetyTimeout.timeout.connect([&]() {
        loop.quit(1);
    });
    safetyTimeout.start(3000);

    const int rc = loop.exec();
    safetyTimeout.stop();

    loop.removeSocket(static_cast<EventLoop::SocketHandle>(listener));
    loop.removeSocket(static_cast<EventLoop::SocketHandle>(client));
    if (serverPeer != INVALID_SOCKET) {
        loop.removeSocket(static_cast<EventLoop::SocketHandle>(serverPeer));
        ::closesocket(serverPeer);
    }
    ::closesocket(client);
    ::closesocket(listener);

    if (rc != 0 || received != "ping-socket" || postChainCount < 1) {
        std::fprintf(stderr, "Socket readiness/fairness failed (rc=%d, received='%s', posts=%d)\n",
                     rc,
                     received.c_str(),
                     postChainCount);
        return false;
    }
    return true;
}

bool testIdleNoBusySpin()
{
    EventLoop &loop = EventLoop::instance();
    loop.setIdlePump(nullptr);

    ElapsedTimer wall;
    wall.start();

    Timer::singleShot(80, [&]() {
        loop.quit(0);
    });

    loop.exec();

    const std::int64_t elapsedMs = wall.elapsed();
    const std::uint64_t iterations = loop.iterationCount();
    if (elapsedMs < 60 || iterations > 5) {
        std::fprintf(stderr,
                     "Idle no-spin test failed: elapsed=%lld ms, iterations=%llu\n",
                     static_cast<long long>(elapsedMs),
                     static_cast<unsigned long long>(iterations));
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!testSignal()) {
        return 1;
    }
    if (!testElapsedTimer()) {
        return 1;
    }
    if (!testPostOrderAndNextRound()) {
        return 1;
    }
    if (!testTimerOrderAndZeroMs()) {
        return 1;
    }
    if (!testTimerRepeatStopAndDeleteInCallback()) {
        return 1;
    }
    if (!testSocketReadinessAndFairness()) {
        return 1;
    }
    if (!testIdleNoBusySpin()) {
        return 1;
    }

    std::printf("PLATFORM_TESTS_OK\n");
    std::fflush(stdout);
    return 0;
}
