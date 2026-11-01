#ifndef ANIMEAN_BACKEND_PLATFORM_EVENTLOOP_H
#define ANIMEAN_BACKEND_PLATFORM_EVENTLOOP_H

#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

class Timer;

class EventLoop
{
public:
    using SocketHandle = std::uintptr_t;
    using SocketCallback = std::function<void(unsigned int)>;

    enum SocketEvents : unsigned int {
        SocketNone = 0,
        SocketRead = 1u << 0,
        SocketWrite = 1u << 1,
        SocketError = 1u << 2,

        Read = SocketRead,
        Write = SocketWrite,
        Error = SocketError
    };

    static EventLoop &instance();

    EventLoop() = default;
    ~EventLoop() = default;

    EventLoop *operator->() noexcept { return this; }
    const EventLoop *operator->() const noexcept { return this; }
    operator EventLoop *() noexcept { return this; }
    operator const EventLoop *() const noexcept { return this; }

    int exec();
    void quit(int returnCode = 0);
    void exit(int returnCode = 0) { quit(returnCode); }

    void post(std::function<void()> task);

    void addSocket(SocketHandle socket, unsigned int events, SocketCallback callback);

    template <typename F,
              typename = std::enable_if_t<
                  !std::is_convertible_v<F &&, SocketCallback> &&
                  std::is_invocable_v<F>>>
    void addSocket(SocketHandle socket, unsigned int events, F &&callback)
    {
        auto fn = std::function<void()>(std::forward<F>(callback));
        addSocket(socket, events, [fn = std::move(fn)](unsigned int) {
            if (fn) {
                fn();
            }
        });
    }

    void updateSocket(SocketHandle socket, unsigned int events);
    void removeSocket(SocketHandle socket);

    // 过渡，后端去掉 Qt 时删
    void setIdlePump(std::function<void()> pump);

    std::uint64_t iterationCount() const noexcept;

private:
    friend class Timer;

    std::uint64_t allocateTimerOrder();
    std::uint64_t createTimerEntry(std::uint64_t creationOrder, bool autoRemove = false);
    void startTimer(std::uint64_t timerId,
                    int intervalMs,
                    bool singleShot,
                    std::function<void()> callback);
    void stopTimer(std::uint64_t timerId);
    void removeTimer(std::uint64_t timerId);
    void setTimerSingleShot(std::uint64_t timerId, bool singleShot);
    bool isTimerActive(std::uint64_t timerId) const;
};

namespace AnimeCore {
using ::EventLoop;
} // namespace AnimeCore

#endif // ANIMEAN_BACKEND_PLATFORM_EVENTLOOP_H
