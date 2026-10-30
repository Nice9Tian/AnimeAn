// Same scene and frames as canvas.html: strokes of cubic segments plus filled
// closed shapes, redrawn under a moving pan/zoom at 1920x1080.
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLPaintDevice>
#include <QPainter>
#include <QPainterPath>
#include <QSurfaceFormat>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static const int W = 1920, H = 1080, SEGMENTS = 40;

static std::vector<QPainterPath> makeStrokes(int count)
{
    std::vector<QPainterPath> out;
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        double x = 960.0 + 900.0 * std::sin(i * 0.7);
        double y = 540.0 + 500.0 * std::cos(i * 1.3);
        QPainterPath path(QPointF(x, y));
        for (int j = 0; j < SEGMENTS; ++j) {
            double a = (i + j) * 0.37, b = (i - j) * 0.53;
            double x1 = x + 12.0 * std::sin(a), y1 = y + 12.0 * std::cos(b);
            double x2 = x1 + 12.0 * std::cos(a), y2 = y1 + 12.0 * std::sin(b);
            double x3 = x2 + 12.0 * std::sin(b), y3 = y2 + 12.0 * std::cos(a);
            path.cubicTo(x1, y1, x2, y2, x3, y3);
            x = x3;
            y = y3;
        }
        out.push_back(path);
    }
    return out;
}

static std::vector<QPainterPath> makeFills(int count)
{
    std::vector<QPainterPath> out;
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        double cx = 960.0 + 880.0 * std::sin(i * 1.1), cy = 540.0 + 480.0 * std::cos(i * 0.9);
        QPainterPath path(QPointF(cx + 60.0, cy));
        for (int j = 1; j <= 6; ++j) {
            double a0 = (j - 1) * M_PI / 3.0, a1 = j * M_PI / 3.0, r = 60.0 + 20.0 * std::sin(i + j);
            path.cubicTo(cx + r * std::cos(a0 + 0.3), cy + r * std::sin(a0 + 0.3),
                         cx + r * std::cos(a1 - 0.3), cy + r * std::sin(a1 - 0.3),
                         cx + r * std::cos(a1), cy + r * std::sin(a1));
        }
        path.closeSubpath();
        out.push_back(path);
    }
    return out;
}

static void drawFrame(QPainter &p, int frame, const std::vector<QPainterPath> &strokes,
                      const std::vector<QPainterPath> &fills)
{
    p.setRenderHint(QPainter::Antialiasing);
    p.resetTransform();
    p.fillRect(QRect(0, 0, W, H), Qt::white);
    p.translate(frame * 2.0, frame * 1.0);
    p.scale(1.0 + 0.002 * frame, 1.0 + 0.002 * frame);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(90, 140, 220, 160));
    for (const QPainterPath &fill : fills) {
        p.drawPath(fill);
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(20, 20, 30), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (const QPainterPath &stroke : strokes) {
        p.drawPath(stroke);
    }
}

static double median(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

static double runRaster(const std::vector<QPainterPath> &strokes, const std::vector<QPainterPath> &fills, int frames)
{
    QImage image(W, H, QImage::Format_ARGB32_Premultiplied);
    std::vector<double> times;
    for (int f = 0; f < frames; ++f) {
        QElapsedTimer t;
        t.start();
        QPainter p(&image);
        drawFrame(p, f, strokes, fills);
        p.end();
        times.push_back(t.nsecsElapsed() / 1e6);
    }
    return median(times);
}

static double runGl(QOpenGLContext &gl, const std::vector<QPainterPath> &strokes,
                    const std::vector<QPainterPath> &fills, int frames, int samples)
{
    QOpenGLFramebufferObjectFormat format;
    format.setSamples(samples);
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    QOpenGLFramebufferObject fbo(W, H, format);
    QOpenGLFramebufferObject resolve(W, H);
    QOpenGLPaintDevice device(W, H);
    std::vector<double> times;
    for (int f = 0; f < frames; ++f) {
        QElapsedTimer t;
        t.start();
        fbo.bind();
        QPainter p(&device);
        drawFrame(p, f, strokes, fills);
        p.end();
        QOpenGLFramebufferObject::blitFramebuffer(&resolve, &fbo);
        gl.functions()->glFinish();
        times.push_back(t.nsecsElapsed() / 1e6);
    }
    return median(times);
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QSurfaceFormat format;
    format.setSamples(8);
    format.setStencilBufferSize(8);
    QOffscreenSurface surface;
    surface.setFormat(format);
    surface.create();
    QOpenGLContext gl;
    gl.setFormat(format);
    const bool haveGl = gl.create() && gl.makeCurrent(&surface);
    if (haveGl) {
        std::printf("GL %s | %s\n", gl.functions()->glGetString(GL_RENDERER),
                    gl.functions()->glGetString(GL_VERSION));
    }
    const int scenes[3] = {500, 3000, 10000};
    for (int count : scenes) {
        QElapsedTimer build;
        build.start();
        const auto strokes = makeStrokes(count);
        const auto fills = makeFills(count / 10);
        const double buildMs = build.nsecsElapsed() / 1e6;
        const int frames = count >= 10000 ? 7 : 15;
        const double raster = runRaster(strokes, fills, frames);
        const double glMs = haveGl ? runGl(gl, strokes, fills, frames, 8) : -1.0;
        std::printf("{\"strokes\":%d,\"build_ms\":%.1f,\"qt_raster_ms\":%.1f,\"qt_gl_msaa8_ms\":%.1f}\n",
                    count, buildMs, raster, glMs);
        std::fflush(stdout);
    }
    return 0;
}
