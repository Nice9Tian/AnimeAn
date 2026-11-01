#ifndef ANIMECORE_INPUTEVENT_H
#define ANIMECORE_INPUTEVENT_H

#include "algorithm/core/geometry.h"

#include <cstdint>

namespace AnimeCore {

enum MouseButton {
    NoButton = 0x00000000,
    LeftButton = 0x00000001,
    RightButton = 0x00000002,
    MiddleButton = 0x00000004
};

using MouseButtons = int;

enum KeyboardModifier {
    NoModifier = 0x00000000,
    ShiftModifier = 0x02000000,
    ControlModifier = 0x04000000,
    AltModifier = 0x08000000,
    MetaModifier = 0x10000000
};

using KeyboardModifiers = int;

enum FocusPolicy {
    NoFocus = 0,
    TabFocus = 0x1,
    ClickFocus = 0x2,
    StrongFocus = 0xb,
    WheelFocus = 0xf
};

enum FocusReason {
    MouseFocusReason = 0,
    TabFocusReason = 1,
    BacktabFocusReason = 2,
    ActiveWindowFocusReason = 3,
    PopupFocusReason = 4,
    ShortcutFocusReason = 5,
    MenuBarFocusReason = 6,
    OtherFocusReason = 7
};

class Event
{
public:
    enum Type {
        None = 0,
        MouseButtonPress = 2,
        MouseButtonRelease = 3,
        MouseButtonDblClick = 4,
        MouseMove = 5,
        FocusIn = 8,
        FocusOut = 9,
        Enter = 10,
        Leave = 11,
        Resize = 14,
        Wheel = 31
    };

    explicit Event(Type type = None) noexcept : m_type(type) {}
    virtual ~Event() = default;

    Type type() const noexcept { return m_type; }

    void accept() noexcept { m_accepted = true; }
    void ignore() noexcept { m_accepted = false; }
    bool isAccepted() const noexcept { return m_accepted; }
    void setAccepted(bool accepted) noexcept { m_accepted = accepted; }

    std::uint64_t timestamp() const noexcept { return m_timestamp; }
    void setTimestamp(std::uint64_t ts) noexcept { m_timestamp = ts; }

private:
    Type m_type = None;
    bool m_accepted = true;
    std::uint64_t m_timestamp = 0;
};

class MouseEvent : public Event
{
public:
    MouseEvent(Type type,
               const PointF &localPos,
               MouseButton button,
               MouseButtons buttons,
               KeyboardModifiers modifiers,
               std::uint64_t timestamp = 0) noexcept
        : Event(type)
        , m_localPos(localPos)
        , m_button(button)
        , m_buttons(buttons)
        , m_modifiers(modifiers)
    {
        setTimestamp(timestamp);
    }

    MouseEvent(Type type,
               const PointF &localPos,
               const PointF & /*globalPos*/,
               MouseButton button,
               MouseButtons buttons,
               KeyboardModifiers modifiers,
               std::uint64_t timestamp = 0) noexcept
        : MouseEvent(type, localPos, button, buttons, modifiers, timestamp)
    {
    }

    MouseEvent(Type type,
               const PointF &localPos,
               const PointF & /*scenePos*/,
               const PointF & /*globalPos*/,
               MouseButton button,
               MouseButtons buttons,
               KeyboardModifiers modifiers,
               std::uint64_t timestamp = 0) noexcept
        : MouseEvent(type, localPos, button, buttons, modifiers, timestamp)
    {
    }

    PointF position() const noexcept { return m_localPos; }
    Point pos() const noexcept { return m_localPos.toPoint(); }
    MouseButton button() const noexcept { return m_button; }
    MouseButtons buttons() const noexcept { return m_buttons; }
    KeyboardModifiers modifiers() const noexcept { return m_modifiers; }

private:
    PointF m_localPos;
    MouseButton m_button = NoButton;
    MouseButtons m_buttons = NoButton;
    KeyboardModifiers m_modifiers = NoModifier;
};

class WheelEvent : public Event
{
public:
    WheelEvent(const PointF &pos,
               const Point &angleDelta,
               MouseButtons buttons = NoButton,
               KeyboardModifiers modifiers = NoModifier) noexcept
        : Event(Wheel)
        , m_pos(pos)
        , m_angleDelta(angleDelta)
        , m_buttons(buttons)
        , m_modifiers(modifiers)
    {
    }

    WheelEvent(const PointF &pos,
               const PointF & /*globalPos*/,
               const Point &pixelDelta,
               const Point &angleDelta,
               MouseButtons buttons,
               KeyboardModifiers modifiers,
               int /*phase*/ = 0,
               bool /*inverted*/ = false) noexcept
        : Event(Wheel)
        , m_pos(pos)
        , m_pixelDelta(pixelDelta)
        , m_angleDelta(angleDelta)
        , m_buttons(buttons)
        , m_modifiers(modifiers)
    {
    }

    PointF position() const noexcept { return m_pos; }
    Point pixelDelta() const noexcept { return m_pixelDelta; }
    Point angleDelta() const noexcept { return m_angleDelta; }
    MouseButtons buttons() const noexcept { return m_buttons; }
    KeyboardModifiers modifiers() const noexcept { return m_modifiers; }

private:
    PointF m_pos;
    Point m_pixelDelta;
    Point m_angleDelta;
    MouseButtons m_buttons = NoButton;
    KeyboardModifiers m_modifiers = NoModifier;
};

class EnterEvent : public Event
{
public:
    explicit EnterEvent(const PointF &localPos = PointF(),
                        const PointF & /*scenePos*/ = PointF(),
                        const PointF & /*globalPos*/ = PointF()) noexcept
        : Event(Enter)
        , m_localPos(localPos)
    {
    }

    PointF position() const noexcept { return m_localPos; }
    Point pos() const noexcept { return m_localPos.toPoint(); }

private:
    PointF m_localPos;
};

class FocusEvent : public Event
{
public:
    explicit FocusEvent(Type type, FocusReason reason = OtherFocusReason) noexcept
        : Event(type)
        , m_reason(reason)
    {
    }

    FocusReason reason() const noexcept { return m_reason; }

private:
    FocusReason m_reason = OtherFocusReason;
};

class ResizeEvent : public Event
{
public:
    explicit ResizeEvent(const Size &size, const Size &oldSize = Size()) noexcept
        : Event(Resize)
        , m_size(size)
        , m_oldSize(oldSize)
    {
    }

    const Size &size() const noexcept { return m_size; }
    const Size &oldSize() const noexcept { return m_oldSize; }

private:
    Size m_size;
    Size m_oldSize;
};

} // namespace AnimeCore

#endif // ANIMECORE_INPUTEVENT_H
