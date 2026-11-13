#ifndef BOARDSURFACE_H
#define BOARDSURFACE_H


#include "algorithm/core/color.h"
#include "algorithm/core/containers.h"
#include "algorithm/core/geometry.h"
#include "algorithm/core/image.h"
#include "algorithm/core/json.h"
#include "algorithm/core/painter.h"
#include "algorithm/core/path.h"
#include "algorithm/core/string.h"
#include "backend/board/cursor.h"
#include "backend/board/inputevent.h"
#include "backend/board/recordingdevice.h"
#include "backend/platform/timer.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

struct BoardFrame {
    std::uint64_t frame = 0;
    int w = 0;
    int h = 0;
    int keep = 0;
    std::string ops;
};

class BoardSurface : public AnimeCore::PaintDevice
{
public:
    using FrameSink = std::function<void(const BoardFrame &)>;

    explicit BoardSurface(void *parent = nullptr);
    ~BoardSurface() override;

    void setFrameSink(FrameSink sink);
    void setCanSendFrame(std::function<bool()> canSend);
    void setImageSink(std::function<void(const AnimeCore::String &key, const AnimeCore::Image &)> sink,
                      std::function<void(const AnimeCore::String &key)> forget);
    void forgetSentImages(bool tellClients = false);
    void setImageBudget(std::int64_t bytes);
    RecordingDevice *recordingDevice() const { return m_engine.get(); }
    void setCursorSink(std::function<void(const AnimeCore::Cursor &)> sink);

    void injectMouse(AnimeCore::Event::Type type,
                     AnimeCore::PointF pos,
                     AnimeCore::MouseButton button,
                     AnimeCore::MouseButtons buttons,
                     AnimeCore::KeyboardModifiers modifiers = AnimeCore::NoModifier,
                     std::uint64_t timestamp = 0);
    void injectWheel(AnimeCore::PointF pos,
                     AnimeCore::Point angleDelta,
                     AnimeCore::KeyboardModifiers modifiers = AnimeCore::NoModifier,
                     AnimeCore::MouseButtons buttons = AnimeCore::NoButton);
    void injectEnter();
    void injectLeave();
    void injectFocus();
    void injectBlur();
    void resizeSurface(int w, int h, double dpr);
    void acknowledgeFrame(std::uint64_t frame);
    bool isWaitingForAck() const { return m_waitingForAck; }
    bool forceFullFrame() const { return m_forceFullFrame; }

    void update();
    int width() const { return m_width; }
    int height() const { return m_height; }
    AnimeCore::Size size() const { return AnimeCore::Size(m_width, m_height); }
    AnimeCore::Rect rect() const { return AnimeCore::Rect(0, 0, m_width, m_height); }
    double devicePixelRatioF() const { return m_dpr; }

    void setCursor(const AnimeCore::Cursor &cursor);
    void unsetCursor();
    AnimeCore::Cursor cursor() const { return m_cursor; }

    void setMouseTracking(bool enable) { m_mouseTracking = enable; }
    bool hasMouseTracking() const { return m_mouseTracking; }

    void setFocusPolicy(AnimeCore::FocusPolicy policy) { m_focusPolicy = policy; }
    AnimeCore::FocusPolicy focusPolicy() const { return m_focusPolicy; }

    void setAutoFillBackground(bool enabled) { m_autoFillBackground = enabled; }
    bool autoFillBackground() const { return m_autoFillBackground; }

    // The op list is in logical pixels and the page applies its own ratio. Telling
    // Painter the real one would scale the transforms too, so it counts twice.
    double paintDevicePixelRatio() const override { return 1.0; }

    void drawRects(const AnimeCore::PaintState &state, const AnimeCore::RectF *rects, int count) override;
    void drawLines(const AnimeCore::PaintState &state, const AnimeCore::LineF *lines, int count) override;
    void drawEllipse(const AnimeCore::PaintState &state, const AnimeCore::RectF &rect) override;
    void drawPath(const AnimeCore::PaintState &state, const AnimeCore::Path &path) override;
    void drawPoints(const AnimeCore::PaintState &state, const AnimeCore::PointF *points, int count) override;
    void drawPolygon(const AnimeCore::PaintState &state, const AnimeCore::PointF *points, int count,
                     AnimeCore::PolygonMode mode) override;
    void drawImage(const AnimeCore::PaintState &state, const AnimeCore::RectF &target,
                   const AnimeCore::Image &image, const AnimeCore::RectF &source) override;

protected:
    virtual void paintGL();
    virtual void mousePressEvent(AnimeCore::MouseEvent *event);
    virtual void mouseMoveEvent(AnimeCore::MouseEvent *event);
    virtual void mouseReleaseEvent(AnimeCore::MouseEvent *event);
    virtual void wheelEvent(AnimeCore::WheelEvent *event);
    virtual void resizeEvent(AnimeCore::ResizeEvent *event);
    virtual void focusInEvent(AnimeCore::FocusEvent *event);
    virtual void focusOutEvent(AnimeCore::FocusEvent *event);
    virtual void enterEvent(AnimeCore::EnterEvent *event);
    virtual void leaveEvent(AnimeCore::Event *event);

private:
    void scheduleFlush();
    void flushFrame();

    std::unique_ptr<RecordingDevice> m_engine;
    AnimeCore::Timer m_updateTimer;

    FrameSink m_frameSink;
    std::function<bool()> m_canSendFrame;
    std::function<void(const AnimeCore::Cursor &)> m_cursorSink;

    int m_width = 0;
    int m_height = 0;
    double m_dpr = 1.0;

    AnimeCore::Cursor m_cursor;
    bool m_mouseTracking = false;
    AnimeCore::FocusPolicy m_focusPolicy = AnimeCore::NoFocus;
    bool m_autoFillBackground = false;

    bool m_dirty = false;
    bool m_waitingForAck = false;
    bool m_forceFullFrame = true;
    std::uint64_t m_nextFrameId = 1;
    std::uint64_t m_lastSentFrame = 0;
    std::uint64_t m_lastMouseTimestamp = 0;
    AnimeCore::Vector<RecordedOp> m_prevOps;
};

#endif // BOARDSURFACE_H
