#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <windows.h>

#include "eventloop.h"

#include <algorithm>
#include <chrono>
#include <climits>

namespace {

void ensureWinsockInitialized()
{
    static const bool initialized = []() {
        WSADATA wsaData{};
        return ::WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
    }();
    (void)initialized;
}

struct TimerEntry {
    std::uint64_t id = 0;
    std::uint64_t creationOrder = 0;
    std::uint64_t generation = 0;
    std::uint64_t minIteration = 0;
    int intervalMs = 0;
    bool singleShot = false;
    bool active = false;
    bool autoRemove = false;
    std::int64_t deadlineMs = 0;
    std::function<void()> callback;
};

struct SocketEntry {
    EventLoop::SocketHandle socket = 0;
    unsigned int events = EventLoop::SocketNone;
    EventLoop::SocketCallback callback;
};

struct LoopState {
    using Clock = std::chrono::steady_clock;

    Clock::time_point epoch = Clock::now();
    Clock::time_point lastExternalArmTime{};
    bool hasExternalArmTime = false;
    std::int64_t logicalNowMs = 0;

    bool inExec = false;
    bool quitRequested = false;
    int exitCode = 0;
    std::uint64_t iteration = 0;
    std::uint64_t iterationCount = 0;

    std::uint64_t nextTimerId = 1;
    std::uint64_t nextTimerOrder = 1;
    std::uint64_t nextTimerGeneration = 1;

    std::vector<std::function<void()>> postedTasks;
    std::vector<TimerEntry> timers;
    std::vector<SocketEntry> sockets;

    // 过渡，后端去掉 Qt 时删
    std::function<void()> idlePump;

    LoopState()
    {
        ensureWinsockInitialized();
    }

    std::int64_t rawNowMs() const noexcept
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - epoch).count();
    }

    void refreshNowMs() noexcept
    {
        logicalNowMs = rawNowMs();
        hasExternalArmTime = false;
    }

    std::int64_t armBaseNowMs() noexcept
    {
        if (inExec) {
            return logicalNowMs;
        }
        const Clock::time_point now = Clock::now();
        if (!hasExternalArmTime
            || std::chrono::duration_cast<std::chrono::milliseconds>(now - lastExternalArmTime).count() >= 5) {
            logicalNowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - epoch).count();
        }
        lastExternalArmTime = now;
        hasExternalArmTime = true;
        return logicalNowMs;
    }

    void fireDueTimers()
    {
        refreshNowMs();

        struct DueItem {
            std::int64_t deadlineMs;
            std::uint64_t creationOrder;
            std::uint64_t id;
            std::uint64_t generation;
        };

        std::vector<DueItem> due;
        for (const TimerEntry &e : timers) {
            if (e.active && e.minIteration <= iteration && e.deadlineMs <= logicalNowMs) {
                due.push_back(DueItem{e.deadlineMs, e.creationOrder, e.id, e.generation});
            }
        }
        if (due.empty()) {
            return;
        }

        std::sort(due.begin(), due.end(), [](const DueItem &a, const DueItem &b) {
            if (a.deadlineMs != b.deadlineMs) {
                return a.deadlineMs < b.deadlineMs;
            }
            return a.creationOrder < b.creationOrder;
        });

        for (const DueItem &item : due) {
            if (quitRequested) {
                break;
            }
            std::function<void()> cb;
            for (auto it = timers.begin(); it != timers.end(); ++it) {
                if (it->id == item.id) {
                    if (!it->active || it->generation != item.generation) {
                        break;
                    }
                    cb = it->callback;
                    if (it->singleShot) {
                        it->active = false;
                        if (it->autoRemove) {
                            timers.erase(it);
                        }
                    } else {
                        it->deadlineMs = logicalNowMs + it->intervalMs;
                        it->minIteration = iteration + 1;
                    }
                    break;
                }
            }
            if (cb) {
                cb();
            }
        }
    }

    void pollSocketsOrSleep()
    {
        refreshNowMs();

        int timeoutMs = -1;
        if (!postedTasks.empty()) {
            timeoutMs = 0;
        } else {
            for (const TimerEntry &e : timers) {
                if (!e.active) {
                    continue;
                }
                const std::int64_t diff = e.deadlineMs - logicalNowMs;
                const int wait = diff <= 0 ? 0 : static_cast<int>(std::min<std::int64_t>(diff, INT_MAX));
                if (timeoutMs < 0 || wait < timeoutMs) {
                    timeoutMs = wait;
                }
            }
        }

        // 过渡，后端去掉 Qt 时删
        if (idlePump && (timeoutMs < 0 || timeoutMs > 1)) {
            timeoutMs = 1;
        }

        std::vector<WSAPOLLFD> pollFds;
        std::vector<EventLoop::SocketHandle> pollHandles;
        pollFds.reserve(sockets.size());
        pollHandles.reserve(sockets.size());

        for (const SocketEntry &s : sockets) {
            short ev = 0;
            if (s.events & EventLoop::SocketRead) {
                ev |= POLLRDNORM;
            }
            if (s.events & EventLoop::SocketWrite) {
                ev |= POLLWRNORM;
            }
            if (ev != 0) {
                WSAPOLLFD pfd{};
                pfd.fd = static_cast<SOCKET>(s.socket);
                pfd.events = ev;
                pfd.revents = 0;
                pollFds.push_back(pfd);
                pollHandles.push_back(s.socket);
            }
        }

        if (!pollFds.empty()) {
            const int ret = ::WSAPoll(pollFds.data(), static_cast<ULONG>(pollFds.size()), timeoutMs);
            refreshNowMs();
            if (ret > 0) {
                for (std::size_t i = 0; i < pollFds.size(); ++i) {
                    if (quitRequested) {
                        break;
                    }
                    const short revents = pollFds[i].revents;
                    if (revents == 0) {
                        continue;
                    }
                    unsigned int fired = EventLoop::SocketNone;
                    if (revents & (POLLRDNORM | POLLRDBAND | POLLPRI)) {
                        fired |= EventLoop::SocketRead;
                    }
                    if (revents & POLLWRNORM) {
                        fired |= EventLoop::SocketWrite;
                    }
                    if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
                        fired |= (EventLoop::SocketError | EventLoop::SocketRead);
                    }
                    if (fired == EventLoop::SocketNone) {
                        continue;
                    }
                    const EventLoop::SocketHandle handle = pollHandles[i];
                    EventLoop::SocketCallback cb;
                    for (const SocketEntry &s : sockets) {
                        if (s.socket == handle) {
                            cb = s.callback;
                            break;
                        }
                    }
                    if (cb) {
                        cb(fired);
                    }
                }
            }
        } else {
            // Nothing to wait on at all: still give the CPU back.
            ::Sleep(static_cast<DWORD>(timeoutMs < 0 ? 10 : timeoutMs));
            refreshNowMs();
        }
    }
};

LoopState &state()
{
    static LoopState s;
    return s;
}

} // namespace

EventLoop &EventLoop::instance()
{
    static EventLoop loop;
    (void)state();
    return loop;
}

int EventLoop::exec()
{
    LoopState &s = state();
    s.quitRequested = false;
    s.exitCode = 0;
    s.inExec = true;
    s.iterationCount = 0;
    s.refreshNowMs();

    while (!s.quitRequested) {
        ++s.iteration;
        ++s.iterationCount;

        // 过渡，后端去掉 Qt 时删
        if (s.idlePump) {
            s.idlePump();
            if (s.quitRequested) {
                break;
            }
        }

        // Run only the tasks already queued before this round began so newly
        // posted tasks wait for the next round and cannot starve sockets.
        std::vector<std::function<void()>> batch;
        batch.swap(s.postedTasks);
        for (auto &task : batch) {
            if (task) {
                task();
            }
            if (s.quitRequested) {
                break;
            }
        }
        if (s.quitRequested) {
            break;
        }

        s.fireDueTimers();
        if (s.quitRequested) {
            break;
        }

        s.pollSocketsOrSleep();
        if (s.quitRequested) {
            break;
        }
    }

    s.inExec = false;
    return s.exitCode;
}

void EventLoop::quit(int returnCode)
{
    LoopState &s = state();
    s.exitCode = returnCode;
    s.quitRequested = true;
}

void EventLoop::post(std::function<void()> task)
{
    state().postedTasks.push_back(std::move(task));
}

void EventLoop::addSocket(SocketHandle socket, unsigned int events, SocketCallback callback)
{
    ensureWinsockInitialized();
    LoopState &s = state();
    for (SocketEntry &entry : s.sockets) {
        if (entry.socket == socket) {
            entry.events = events;
            entry.callback = std::move(callback);
            return;
        }
    }
    s.sockets.push_back(SocketEntry{socket, events, std::move(callback)});
}

void EventLoop::updateSocket(SocketHandle socket, unsigned int events)
{
    LoopState &s = state();
    for (SocketEntry &entry : s.sockets) {
        if (entry.socket == socket) {
            entry.events = events;
            return;
        }
    }
}

void EventLoop::removeSocket(SocketHandle socket)
{
    LoopState &s = state();
    for (auto it = s.sockets.begin(); it != s.sockets.end(); ++it) {
        if (it->socket == socket) {
            s.sockets.erase(it);
            return;
        }
    }
}

// 过渡，后端去掉 Qt 时删
void EventLoop::setIdlePump(std::function<void()> pump)
{
    state().idlePump = std::move(pump);
}

std::uint64_t EventLoop::iterationCount() const noexcept
{
    return state().iterationCount;
}

std::uint64_t EventLoop::allocateTimerOrder()
{
    return state().nextTimerOrder++;
}

std::uint64_t EventLoop::createTimerEntry(std::uint64_t creationOrder, bool autoRemove)
{
    LoopState &s = state();
    const std::uint64_t id = s.nextTimerId++;
    TimerEntry entry;
    entry.id = id;
    entry.creationOrder = creationOrder;
    entry.autoRemove = autoRemove;
    s.timers.push_back(std::move(entry));
    return id;
}

void EventLoop::startTimer(std::uint64_t timerId,
                           int intervalMs,
                           bool singleShot,
                           std::function<void()> callback)
{
    LoopState &s = state();
    const int clampedInterval = std::max(0, intervalMs);
    const std::int64_t baseMs = s.armBaseNowMs();
    for (TimerEntry &e : s.timers) {
        if (e.id == timerId) {
            e.intervalMs = clampedInterval;
            e.singleShot = singleShot;
            e.active = true;
            e.generation = s.nextTimerGeneration++;
            e.deadlineMs = baseMs + clampedInterval;
            e.minIteration = s.inExec ? (s.iteration + 1) : 0;
            e.callback = std::move(callback);
            return;
        }
    }
}

void EventLoop::stopTimer(std::uint64_t timerId)
{
    LoopState &s = state();
    for (auto it = s.timers.begin(); it != s.timers.end(); ++it) {
        if (it->id == timerId) {
            it->active = false;
            if (it->autoRemove) {
                s.timers.erase(it);
            }
            return;
        }
    }
}

void EventLoop::removeTimer(std::uint64_t timerId)
{
    LoopState &s = state();
    for (auto it = s.timers.begin(); it != s.timers.end(); ++it) {
        if (it->id == timerId) {
            s.timers.erase(it);
            return;
        }
    }
}

void EventLoop::setTimerSingleShot(std::uint64_t timerId, bool singleShot)
{
    LoopState &s = state();
    for (TimerEntry &e : s.timers) {
        if (e.id == timerId) {
            e.singleShot = singleShot;
            return;
        }
    }
}

bool EventLoop::isTimerActive(std::uint64_t timerId) const
{
    const LoopState &s = state();
    for (const TimerEntry &e : s.timers) {
        if (e.id == timerId) {
            return e.active;
        }
    }
    return false;
}
