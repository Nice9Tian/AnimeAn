#ifndef ANIMEAN_BACKEND_PLATFORM_ELAPSEDTIMER_H
#define ANIMEAN_BACKEND_PLATFORM_ELAPSEDTIMER_H

#include <chrono>
#include <cstdint>

class ElapsedTimer
{
public:
    ElapsedTimer() noexcept = default;

    void start() noexcept
    {
        m_start = Clock::now();
        m_valid = true;
    }

    std::int64_t restart() noexcept
    {
        const TimePoint now = Clock::now();
        std::int64_t prev = 0;
        if (m_valid) {
            prev = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_start).count();
        }
        m_start = now;
        m_valid = true;
        return prev;
    }

    std::int64_t nsecsElapsed() const noexcept
    {
        if (!m_valid) {
            return 0;
        }
        return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - m_start).count();
    }

    std::int64_t elapsed() const noexcept
    {
        if (!m_valid) {
            return 0;
        }
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - m_start).count();
    }

    bool isValid() const noexcept
    {
        return m_valid;
    }

    void invalidate() noexcept
    {
        m_valid = false;
    }

    bool hasExpired(std::int64_t timeout) const noexcept
    {
        if (timeout < 0) {
            return false;
        }
        if (!m_valid) {
            return true;
        }
        return elapsed() >= timeout;
    }

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    TimePoint m_start{};
    bool m_valid = false;
};

namespace AnimeCore {
using ::ElapsedTimer;
} // namespace AnimeCore

#endif // ANIMEAN_BACKEND_PLATFORM_ELAPSEDTIMER_H
