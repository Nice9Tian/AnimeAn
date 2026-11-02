#include "timer.h"

#include "eventloop.h"

#include <algorithm>

Timer::Timer(void * /*parent*/)
{
    EventLoop &loop = EventLoop::instance();
    m_creationOrder = loop.allocateTimerOrder();
    m_timerId = loop.createTimerEntry(m_creationOrder, false);
}

Timer::~Timer()
{
    EventLoop::instance().removeTimer(m_timerId);
}

void Timer::setSingleShot(bool singleShot)
{
    m_singleShot = singleShot;
    EventLoop::instance().setTimerSingleShot(m_timerId, singleShot);
}

void Timer::setInterval(int msec)
{
    m_interval = std::max(0, msec);
    if (isActive()) {
        start();
    }
}

void Timer::start()
{
    EventLoop::instance().startTimer(m_timerId, m_interval, m_singleShot, [this]() {
        timeout();
    });
}

void Timer::start(int msec)
{
    m_interval = std::max(0, msec);
    start();
}

void Timer::stop()
{
    EventLoop::instance().stopTimer(m_timerId);
}

bool Timer::isActive() const
{
    return EventLoop::instance().isTimerActive(m_timerId);
}

void Timer::singleShot(int msec, std::function<void()> callback)
{
    EventLoop &loop = EventLoop::instance();
    const std::uint64_t order = loop.allocateTimerOrder();
    const std::uint64_t id = loop.createTimerEntry(order, true);
    loop.startTimer(id, std::max(0, msec), true, std::move(callback));
}
