#include "boardsurface.h"

#include <QTimer>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

BoardSurface::BoardSurface(QObject *parent)
    : QObject(parent)
    , m_engine(std::make_unique<RecordingPaintEngine>())
    , m_updateTimer(new QTimer(this))
{
    m_updateTimer->setSingleShot(true);
    m_updateTimer->setInterval(0);
    connect(m_updateTimer, &QTimer::timeout, this, &BoardSurface::flushFrame);
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

void BoardSurface::setImageSink(std::function<void(const QString &key, const QImage &)> sink)
{
    m_engine->setImageSink(std::move(sink));
}

void BoardSurface::setCursorSink(std::function<void(const QCursor &)> sink)
{
    m_cursorSink = std::move(sink);
}

void BoardSurface::setCursor(const QCursor &cursor)
{
    m_cursor = cursor;
    if (m_cursorSink) {
        m_cursorSink(m_cursor);
    }
}

void BoardSurface::unsetCursor()
{
    m_cursor = QCursor(Qt::ArrowCursor);
    if (m_cursorSink) {
        m_cursorSink(m_cursor);
    }
}

void BoardSurface::injectMouse(QEvent::Type type,
                               QPointF pos,
                               Qt::MouseButton button,
                               Qt::MouseButtons buttons,
                               Qt::KeyboardModifiers modifiers,
                               quint64 timestamp)
{
    if (timestamp < m_lastMouseTimestamp) {
        timestamp = m_lastMouseTimestamp;
    } else {
        m_lastMouseTimestamp = timestamp;
    }
    QMouseEvent ev(type, pos, pos, button, buttons, modifiers);
    ev.setTimestamp(timestamp);
    if (type == QEvent::MouseButtonPress) {
        mousePressEvent(&ev);
    } else if (type == QEvent::MouseMove) {
        mouseMoveEvent(&ev);
    } else if (type == QEvent::MouseButtonRelease) {
        mouseReleaseEvent(&ev);
    }
}

void BoardSurface::injectWheel(QPointF pos,
                               QPoint angleDelta,
                               Qt::KeyboardModifiers modifiers,
                               Qt::MouseButtons buttons)
{
    QWheelEvent ev(pos, pos, QPoint(), angleDelta, buttons, modifiers, Qt::NoScrollPhase, false);
    wheelEvent(&ev);
}

void BoardSurface::injectEnter()
{
    QEnterEvent ev(QPointF(0.0, 0.0), QPointF(0.0, 0.0), QPointF(0.0, 0.0));
    enterEvent(&ev);
}

void BoardSurface::injectLeave()
{
    QEvent ev(QEvent::Leave);
    leaveEvent(&ev);
}

void BoardSurface::injectFocus()
{
    QFocusEvent ev(QEvent::FocusIn, Qt::OtherFocusReason);
    focusInEvent(&ev);
}

void BoardSurface::resizeSurface(int w, int h, qreal dpr)
{
    const QSize oldSize(m_width, m_height);
    m_width = std::max(0, w);
    m_height = std::max(0, h);
    m_dpr = (dpr > 0.0 && std::isfinite(dpr)) ? dpr : 1.0;
    m_forceFullFrame = true;

    QResizeEvent ev(QSize(m_width, m_height), oldSize);
    resizeEvent(&ev);
    update();
}

void BoardSurface::acknowledgeFrame(quint64 frame)
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
    if (!m_updateTimer->isActive()) {
        m_updateTimer->start(0);
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
        if (m_engine->isActive()) {
            m_engine->end();
        }
        m_engine->resetFrame();
        return;
    }

    if (!m_frameSink || (m_canSendFrame && !m_canSendFrame())) {
        return;
    }

    const QVector<RecordedOp> &currOps = m_engine->ops();
    int keep = 0;
    if (!m_forceFullFrame) {
        const int maxKeep = std::min(m_prevOps.size(), currOps.size());
        while (keep < maxKeep
               && m_prevOps[keep].hash == currOps[keep].hash
               && m_prevOps[keep].json == currOps[keep].json) {
            ++keep;
        }
    }
    m_forceFullFrame = false;

    int totalBytes = 2; // '[' and ']'
    for (int i = keep; i < currOps.size(); ++i) {
        if (i > keep) {
            ++totalBytes; // ','
        }
        totalBytes += currOps[i].json.size();
    }

    QByteArray opsJson;
    opsJson.resize(totalBytes);
    char *dst = opsJson.data();
    *dst++ = '[';
    for (int i = keep; i < currOps.size(); ++i) {
        if (i > keep) {
            *dst++ = ',';
        }
        const QByteArray &item = currOps[i].json;
        std::memcpy(dst, item.constData(), size_t(item.size()));
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
}

QPaintEngine *BoardSurface::paintEngine() const
{
    return m_engine.get();
}

int BoardSurface::metric(PaintDeviceMetric m) const
{
    switch (m) {
    case PdmWidth:
        return m_width;
    case PdmHeight:
        return m_height;
    case PdmWidthMM:
        return int(std::lround(m_width * 25.4 / 96.0));
    case PdmHeightMM:
        return int(std::lround(m_height * 25.4 / 96.0));
    case PdmNumColors:
        return INT_MAX;
    case PdmDepth:
        return 32;
    case PdmDpiX:
    case PdmPhysicalDpiX:
        return 96;
    case PdmDpiY:
    case PdmPhysicalDpiY:
        return 96;
    case PdmDevicePixelRatio:
        return std::max(1, int(std::lround(m_dpr)));
    case PdmDevicePixelRatioScaled:
        return int(std::lround(m_dpr * devicePixelRatioFScale()));
    default:
        return QPaintDevice::metric(m);
    }
}

void BoardSurface::paintGL() {}
void BoardSurface::mousePressEvent(QMouseEvent *event) { event->ignore(); }
void BoardSurface::mouseMoveEvent(QMouseEvent *event) { event->ignore(); }
void BoardSurface::mouseReleaseEvent(QMouseEvent *event) { event->ignore(); }
void BoardSurface::wheelEvent(QWheelEvent *event) { event->ignore(); }
void BoardSurface::resizeEvent(QResizeEvent *event) { event->accept(); }
void BoardSurface::focusInEvent(QFocusEvent *event) { event->accept(); }
void BoardSurface::focusOutEvent(QFocusEvent *event) { event->accept(); }
void BoardSurface::enterEvent(QEnterEvent *event) { event->accept(); }
void BoardSurface::leaveEvent(QEvent *event) { event->accept(); }
