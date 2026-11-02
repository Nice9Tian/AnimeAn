#ifndef ANIMEAN_BACKEND_PLATFORM_TIMER_H
#define ANIMEAN_BACKEND_PLATFORM_TIMER_H

#include "signalslot.h"

#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

class Timer
{
public:
    explicit Timer(void *parent = nullptr);
    ~Timer();

    Timer(const Timer &) = delete;
    Timer &operator=(const Timer &) = delete;

    void setSingleShot(bool singleShot);
    bool isSingleShot() const noexcept { return m_singleShot; }

    void setInterval(int msec);
    int interval() const noexcept { return m_interval; }

    void start();
    void start(int msec);
    void stop();
    bool isActive() const;

    static void singleShot(int msec, std::function<void()> callback);

    template <typename Obj, typename R>
    static void singleShot(int msec, Obj *receiver, R (Obj::*method)())
    {
        singleShot(msec, [receiver, method]() { (receiver->*method)(); });
    }

    template <typename Obj,
              typename Func,
              typename = std::enable_if_t<!std::is_member_function_pointer_v<Func> && std::is_invocable_v<Func>>>
    static void singleShot(int msec, const Obj * /*context*/, Func &&callback)
    {
        singleShot(msec, std::function<void()>(std::forward<Func>(callback)));
    }

    Signal<> timeout;

private:
    std::uint64_t m_creationOrder = 0;
    std::uint64_t m_timerId = 0;
    int m_interval = 0;
    bool m_singleShot = false;
};

namespace AnimeCore {
using ::Timer;
} // namespace AnimeCore

#endif // ANIMEAN_BACKEND_PLATFORM_TIMER_H
