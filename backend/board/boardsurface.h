#ifndef BOARDSURFACE_H
#define BOARDSURFACE_H

#include "recordingengine.h"

#include <QByteArray>
#include <QCursor>
#include <QEnterEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QImage>
#include <QMouseEvent>
#include <QObject>
#include <QPaintDevice>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QResizeEvent>
#include <QSize>
#include <QString>
#include <QVector>
#include <QWheelEvent>

#include <functional>
#include <memory>
#include <type_traits>

class QTimer;
class QWidget;

struct BoardFrame {
    quint64 frame = 0;
    int w = 0;
    int h = 0;
    int keep = 0;
    QByteArray ops;
};

class BoardSurface : public QObject, public QPaintDevice
{
    Q_OBJECT

public:
    using FrameSink = std::function<void(const BoardFrame &)>;

    explicit BoardSurface(QObject *parent = nullptr);
    template <typename T, typename = std::enable_if_t<std::is_same_v<T, QWidget>>>
    explicit BoardSurface(T * /*parent*/) : BoardSurface(nullptr) {}
    ~BoardSurface() override;

    void setFrameSink(FrameSink sink);
    void setCanSendFrame(std::function<bool()> canSend);
    void setImageSink(std::function<void(const QString &key, const QImage &)> sink);
    void setCursorSink(std::function<void(const QCursor &)> sink);

    void injectMouse(QEvent::Type type,
                     QPointF pos,
                     Qt::MouseButton button,
                     Qt::MouseButtons buttons,
                     Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                     quint64 timestamp = 0);
    void injectWheel(QPointF pos,
                     QPoint angleDelta,
                     Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                     Qt::MouseButtons buttons = Qt::NoButton);
    void injectEnter();
    void injectLeave();
    void injectFocus();
    void resizeSurface(int w, int h, qreal dpr);
    void acknowledgeFrame(quint64 frame);
    bool isWaitingForAck() const { return m_waitingForAck; }
    bool forceFullFrame() const { return m_forceFullFrame; }

    void update();
    int width() const { return m_width; }
    int height() const { return m_height; }
    QSize size() const { return QSize(m_width, m_height); }
    QRect rect() const { return QRect(0, 0, m_width, m_height); }
    qreal devicePixelRatioF() const { return m_dpr; }

    void setCursor(const QCursor &cursor);
    void unsetCursor();
    QCursor cursor() const { return m_cursor; }

    void setMouseTracking(bool enable) { m_mouseTracking = enable; }
    bool hasMouseTracking() const { return m_mouseTracking; }

    void setFocusPolicy(Qt::FocusPolicy policy) { m_focusPolicy = policy; }
    Qt::FocusPolicy focusPolicy() const { return m_focusPolicy; }

    void setAutoFillBackground(bool enabled) { m_autoFillBackground = enabled; }
    bool autoFillBackground() const { return m_autoFillBackground; }

    QPaintEngine *paintEngine() const override;

protected:
    int metric(PaintDeviceMetric m) const override;

    virtual void paintGL();
    virtual void mousePressEvent(QMouseEvent *event);
    virtual void mouseMoveEvent(QMouseEvent *event);
    virtual void mouseReleaseEvent(QMouseEvent *event);
    virtual void wheelEvent(QWheelEvent *event);
    virtual void resizeEvent(QResizeEvent *event);
    virtual void focusInEvent(QFocusEvent *event);
    virtual void focusOutEvent(QFocusEvent *event);
    virtual void enterEvent(QEnterEvent *event);
    virtual void leaveEvent(QEvent *event);

private:
    void scheduleFlush();
    void flushFrame();

    std::unique_ptr<RecordingPaintEngine> m_engine;
    QTimer *m_updateTimer = nullptr;

    FrameSink m_frameSink;
    std::function<bool()> m_canSendFrame;
    std::function<void(const QCursor &)> m_cursorSink;

    int m_width = 0;
    int m_height = 0;
    qreal m_dpr = 1.0;

    QCursor m_cursor;
    bool m_mouseTracking = false;
    Qt::FocusPolicy m_focusPolicy = Qt::NoFocus;
    bool m_autoFillBackground = false;

    bool m_dirty = false;
    bool m_waitingForAck = false;
    bool m_forceFullFrame = true;
    quint64 m_nextFrameId = 1;
    quint64 m_lastSentFrame = 0;
    quint64 m_lastMouseTimestamp = 0;
    QVector<RecordedOp> m_prevOps;
};

#endif // BOARDSURFACE_H
