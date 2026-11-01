#ifndef ANIMECORE_PAINTER_H
#define ANIMECORE_PAINTER_H

#include "algorithm/core/color.h"
#include "algorithm/core/containers.h"
#include "algorithm/core/geometry.h"
#include "algorithm/core/image.h"
#include "algorithm/core/path.h"

#include <memory>

namespace AnimeCore {

// The numbers are stored in project files as AnimeVectorStroke::penStyle, so
// they cannot change.
enum PenStyle {
    NoPen = 0,
    SolidLine = 1,
    DashLine = 2,
    DotLine = 3,
    DashDotLine = 4,
    DashDotDotLine = 5
};

enum PenCapStyle {
    FlatCap = 0x00,
    SquareCap = 0x10,
    RoundCap = 0x20
};

enum PenJoinStyle {
    MiterJoin = 0x00,
    BevelJoin = 0x40,
    RoundJoin = 0x80
};

enum BrushStyle {
    NoBrush = 0,
    SolidPattern = 1
};

class Pen
{
public:
    Pen() = default;
    Pen(PenStyle style) : m_style(style) {}
    Pen(const Color &color) : m_color(color) {}
    Pen(const Color &color, double width, PenStyle style = SolidLine,
        PenCapStyle cap = SquareCap, PenJoinStyle join = BevelJoin)
        : m_color(color), m_width(width), m_style(style), m_cap(cap), m_join(join)
    {
    }

    PenStyle style() const { return m_style; }
    void setStyle(PenStyle style) { m_style = style; }
    Color color() const { return m_color; }
    void setColor(const Color &color) { m_color = color; }
    double widthF() const { return m_width; }
    void setWidthF(double width) { m_width = width; }
    PenCapStyle capStyle() const { return m_cap; }
    void setCapStyle(PenCapStyle cap) { m_cap = cap; }
    PenJoinStyle joinStyle() const { return m_join; }
    void setJoinStyle(PenJoinStyle join) { m_join = join; }
    double miterLimit() const { return m_miterLimit; }
    bool isCosmetic() const { return m_width == 0.0; }

    // In pen widths, empty for a solid line.
    Vector<double> dashPattern() const;

private:
    Color m_color = Color(0, 0, 0);
    double m_width = 1.0;
    PenStyle m_style = SolidLine;
    PenCapStyle m_cap = SquareCap;
    PenJoinStyle m_join = BevelJoin;
    double m_miterLimit = 2.0;
};

class Brush
{
public:
    Brush() = default;
    Brush(BrushStyle style) : m_style(style) {}
    Brush(const Color &color) : m_color(color), m_style(SolidPattern) {}

    BrushStyle style() const { return m_style; }
    Color color() const { return m_color; }

private:
    Color m_color = Color(0, 0, 0);
    BrushStyle m_style = NoBrush;
};

enum CompositionMode {
    CompositionMode_SourceOver,
    CompositionMode_SourceIn
};

// Everything a device needs to draw one shape. The transform already includes
// the device pixel ratio.
struct PaintState {
    Transform transform;
    Pen pen;
    Brush brush;
    double opacity = 1.0;
    CompositionMode compositionMode = CompositionMode_SourceOver;
    bool antialiasing = false;
    bool smoothImages = false;
};

enum class PolygonMode {
    OddEven,
    Winding,
    Polyline
};

// What a Painter draws on: the board's op list or an Image in memory.
class PaintDevice
{
public:
    virtual ~PaintDevice() = default;

    virtual double paintDevicePixelRatio() const { return 1.0; }

    virtual void drawRects(const PaintState &state, const RectF *rects, int count) = 0;
    virtual void drawLines(const PaintState &state, const LineF *lines, int count) = 0;
    virtual void drawEllipse(const PaintState &state, const RectF &rect) = 0;
    virtual void drawPath(const PaintState &state, const Path &path) = 0;
    virtual void drawPoints(const PaintState &state, const PointF *points, int count) = 0;
    virtual void drawPolygon(const PaintState &state, const PointF *points, int count,
                             PolygonMode mode) = 0;
    virtual void drawImage(const PaintState &state, const RectF &target, const Image &image,
                           const RectF &source) = 0;
};

// The subset of QPainter the board uses, with the same names and order of
// effects: a device sees exactly the calls a QPaintEngine used to see.
class Painter
{
public:
    using CompositionMode = AnimeCore::CompositionMode;
    static constexpr CompositionMode CompositionMode_SourceOver = AnimeCore::CompositionMode_SourceOver;
    static constexpr CompositionMode CompositionMode_SourceIn = AnimeCore::CompositionMode_SourceIn;

    enum RenderHint {
        Antialiasing = 0x01,
        SmoothPixmapTransform = 0x04
    };

    explicit Painter(PaintDevice *device);
    // Draws into the image itself. rasterdevice.cpp holds this one.
    explicit Painter(Image *image);
    ~Painter();

    Painter(const Painter &) = delete;
    Painter &operator=(const Painter &) = delete;

    void end();
    void save();
    void restore();

    void setRenderHint(RenderHint hint, bool on = true);
    void setPen(const Pen &pen);
    void setPen(const Color &color);
    void setPen(PenStyle style);
    void setBrush(const Brush &brush);
    void setBrush(const Color &color);
    void setBrush(BrushStyle style);
    void setOpacity(double opacity);
    void setCompositionMode(CompositionMode mode);

    void translate(double dx, double dy);
    void translate(const PointF &offset) { translate(offset.x(), offset.y()); }
    void scale(double sx, double sy);
    void setWorldTransform(const Transform &transform);

    void fillRect(const RectF &rect, const Color &color);
    void fillRect(const Rect &rect, const Color &color);

    void drawRect(const RectF &rect);
    void drawRect(const Rect &rect);
    void drawLine(const LineF &line);
    void drawLine(const PointF &p1, const PointF &p2) { drawLine(LineF(p1, p2)); }
    void drawEllipse(const RectF &rect);
    void drawEllipse(const PointF &center, double rx, double ry);
    void drawPoint(const PointF &point);
    void drawPolygon(const PointF *points, int count, FillRule rule = OddEvenFill);
    void drawPolygon(const Vector<PointF> &points, FillRule rule = OddEvenFill);
    void drawPolyline(const PointF *points, int count);
    void drawRoundedRect(const RectF &rect, double xRadius, double yRadius);
    void drawPath(const Path &path);
    void drawImage(const PointF &topLeft, const Image &image);

private:
    void updateTransform();

    PaintDevice *m_device = nullptr;
    std::unique_ptr<PaintDevice> m_ownedDevice;
    PaintState m_state;
    Transform m_world;
    double m_ratio = 1.0;
    Vector<PaintState> m_stateStack;
    Vector<Transform> m_worldStack;
};

} // namespace AnimeCore

#endif // ANIMECORE_PAINTER_H
