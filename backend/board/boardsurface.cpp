#include "boardsurface.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

using namespace AnimeCore;

BoardSurface::BoardSurface(void * /*parent*/)
    : m_engine(std::make_unique<RecordingDevice>())
{
    m_updateTimer.setSingleShot(true);
    m_updateTimer.setInterval(0);
    m_updateTimer.timeout.connect([this]() { flushFrame(); });
}

BoardSurface::~BoardSurface() = default;

void BoardSurface::setFrameSink(FrameSink sink)
{
    m_frameSink = std::move(sink);
    if (!m_frameSink) {
        m_waitingForAck = false;
    } else if (m_dirty && !m_waitingForAck) {
        scheduleFlush();
    }
}

void BoardSurface::setCanSendFrame(std::function<bool()> canSend)
{
    m_canSendFrame = std::move(canSend);
    if (m_dirty && !m_waitingForAck) {
        scheduleFlush();
    }
}

void BoardSurface::setImageSink(std::function<void(const String &key, const Image &)> sink,
                                std::function<void(const String &key)> forget)
{
    if (!sink) {
        m_engine->setImageSink(nullptr, nullptr);
        return;
    }
    m_engine->setImageSink(
        [s = std::move(sink)](const std::string &key, const Image &image) {
            s(String::fromStdString(key), image);
        },
        [f = std::move(forget)](const std::string &key) {
            f(String::fromStdString(key));
        });
}

void BoardSurface::forgetSentImages(bool tellClients)
{
    m_engine->forgetSentImages(tellClients);
}

void BoardSurface::setImageBudget(std::int64_t bytes)
{
    m_engine->setImageBudget(bytes);
}

void BoardSurface::setCursorSink(std::function<void(const Cursor &)> sink)
{
    m_cursorSink = std::move(sink);
}

void BoardSurface::setCursor(const Cursor &cursor)
{
    m_cursor = cursor;
    if (m_cursorSink) {
        m_cursorSink(m_cursor);
    }
}

void BoardSurface::unsetCursor()
{
    m_cursor = Cursor(ArrowCursor);
    if (m_cursorSink) {
        m_cursorSink(m_cursor);
    }
}

void BoardSurface::injectMouse(Event::Type type,
                               PointF pos,
                               MouseButton button,
                               MouseButtons buttons,
                               KeyboardModifiers modifiers,
                               std::uint64_t timestamp)
{
    if (timestamp < m_lastMouseTimestamp) {
        timestamp = m_lastMouseTimestamp;
    } else {
        m_lastMouseTimestamp = timestamp;
    }
    MouseEvent ev(type, pos, pos, button, buttons, modifiers);
    ev.setTimestamp(timestamp);
    if (type == Event::MouseButtonPress) {
        mousePressEvent(&ev);
    } else if (type == Event::MouseMove) {
        mouseMoveEvent(&ev);
    } else if (type == Event::MouseButtonRelease) {
        mouseReleaseEvent(&ev);
    }
}

void BoardSurface::injectWheel(PointF pos,
                               Point angleDelta,
                               KeyboardModifiers modifiers,
                               MouseButtons buttons)
{
    WheelEvent ev(pos, pos, Point(), angleDelta, buttons, modifiers, 0, false);
    wheelEvent(&ev);
}

void BoardSurface::injectEnter()
{
    EnterEvent ev(PointF(0.0, 0.0), PointF(0.0, 0.0), PointF(0.0, 0.0));
    enterEvent(&ev);
}

void BoardSurface::injectLeave()
{
    Event ev(Event::Leave);
    leaveEvent(&ev);
}

void BoardSurface::injectFocus()
{
    FocusEvent ev(Event::FocusIn, OtherFocusReason);
    focusInEvent(&ev);
}

void BoardSurface::injectBlur()
{
    FocusEvent ev(Event::FocusOut, OtherFocusReason);
    focusOutEvent(&ev);
}

void BoardSurface::resizeSurface(int w, int h, double dpr)
{
    const Size oldSize(m_width, m_height);
    m_width = std::max(0, w);
    m_height = std::max(0, h);
    m_dpr = (dpr > 0.0 && std::isfinite(dpr)) ? dpr : 1.0;
    m_forceFullFrame = true;

    ResizeEvent ev(Size(m_width, m_height), oldSize);
    resizeEvent(&ev);
    update();
}

void BoardSurface::acknowledgeFrame(std::uint64_t frame)
{
    if (m_waitingForAck && frame >= m_lastSentFrame) {
        m_waitingForAck = false;
        if (m_dirty) {
            scheduleFlush();
        }
    }
}

void BoardSurface::update()
{
    m_dirty = true;
    if (!m_waitingForAck) {
        scheduleFlush();
    }
}

void BoardSurface::scheduleFlush()
{
    if (!m_updateTimer.isActive()) {
        m_updateTimer.start(0);
    }
}

void BoardSurface::flushFrame()
{
    if (!m_dirty || m_waitingForAck || m_width <= 0 || m_height <= 0) {
        return;
    }
    const bool canSend = m_frameSink && (!m_canSendFrame || m_canSendFrame());
    if (!canSend) {
        m_dirty = false;
        return;
    }
    m_dirty = false;

    m_engine->resetFrame();
    try {
        paintGL();
    } catch (...) {
        m_engine->resetFrame();
        return;
    }

    if (!m_frameSink || (m_canSendFrame && !m_canSendFrame())) {
        return;
    }

    const Vector<RecordedOp> &currOps = m_engine->ops();
    int keep = 0;
    if (!m_forceFullFrame) {
        const int maxKeep = std::min(int(m_prevOps.size()), int(currOps.size()));
        while (keep < maxKeep
               && m_prevOps[keep].hash == currOps[keep].hash
               && m_prevOps[keep].json == currOps[keep].json) {
            ++keep;
        }
    }
    m_forceFullFrame = false;

    int totalBytes = 2; // '[' and ']'
    for (int i = keep; i < int(currOps.size()); ++i) {
        if (i > keep) {
            ++totalBytes; // ','
        }
        totalBytes += int(currOps[i].json.size());
    }

    std::string opsJson;
    opsJson.resize(static_cast<std::size_t>(totalBytes));
    char *dst = opsJson.data();
    *dst++ = '[';
    for (int i = keep; i < int(currOps.size()); ++i) {
        if (i > keep) {
            *dst++ = ',';
        }
        const std::string &item = currOps[i].json;
        std::memcpy(dst, item.data(), item.size());
        dst += item.size();
    }
    *dst++ = ']';

    m_prevOps = currOps;

    BoardFrame outFrame;
    outFrame.frame = m_nextFrameId++;
    outFrame.w = m_width;
    outFrame.h = m_height;
    outFrame.keep = keep;
    outFrame.ops = std::move(opsJson);

    m_waitingForAck = true;
    m_lastSentFrame = outFrame.frame;
    m_frameSink(outFrame);
    m_engine->evictImages();
}

void BoardSurface::drawRects(const PaintState &state, const RectF *rects, int count)
{
    m_engine->drawRects(state, rects, count);
}

void BoardSurface::drawLines(const PaintState &state, const LineF *lines, int count)
{
    m_engine->drawLines(state, lines, count);
}

void BoardSurface::drawEllipse(const PaintState &state, const RectF &rect)
{
    m_engine->drawEllipse(state, rect);
}

void BoardSurface::drawPath(const PaintState &state, const Path &path)
{
    m_engine->drawPath(state, path);
}

void BoardSurface::drawPoints(const PaintState &state, const PointF *points, int count)
{
    m_engine->drawPoints(state, points, count);
}

void BoardSurface::drawPolygon(const PaintState &state, const PointF *points, int count, PolygonMode mode)
{
    m_engine->drawPolygon(state, points, count, mode);
}

void BoardSurface::drawImage(const PaintState &state, const RectF &target, const Image &image, const RectF &source)
{
    m_engine->drawImage(state, target, image, source);
}

void BoardSurface::paintGL() {}
void BoardSurface::mousePressEvent(MouseEvent *event) { event->ignore(); }
void BoardSurface::mouseMoveEvent(MouseEvent *event) { event->ignore(); }
void BoardSurface::mouseReleaseEvent(MouseEvent *event) { event->ignore(); }
void BoardSurface::wheelEvent(WheelEvent *event) { event->ignore(); }
void BoardSurface::resizeEvent(ResizeEvent *event) { event->accept(); }
void BoardSurface::focusInEvent(FocusEvent *event) { event->accept(); }
void BoardSurface::focusOutEvent(FocusEvent *event) { event->accept(); }
void BoardSurface::enterEvent(EnterEvent *event) { event->accept(); }
void BoardSurface::leaveEvent(Event *event) { event->accept(); }
