#ifndef ANIMECORE_RASTERDEVICE_H
#define ANIMECORE_RASTERDEVICE_H

#include "algorithm/core/painter.h"

#include <cstdint>
#include <vector>

namespace AnimeCore {

class RasterDevice : public PaintDevice
{
public:
    explicit RasterDevice(Image *image);
    ~RasterDevice() override;

    Image *image() const { return m_image; }

    double paintDevicePixelRatio() const override;

    void drawRects(const PaintState &state, const RectF *rects, int count) override;
    void drawLines(const PaintState &state, const LineF *lines, int count) override;
    void drawEllipse(const PaintState &state, const RectF &rect) override;
    void drawPath(const PaintState &state, const Path &path) override;
    void drawPoints(const PaintState &state, const PointF *points, int count) override;
    void drawPolygon(const PaintState &state, const PointF *points, int count,
                     PolygonMode mode) override;
    void drawImage(const PaintState &state, const RectF &target, const Image &image,
                   const RectF &source) override;

private:
    struct SubpathRange {
        int start = 0;
        int count = 0;
        bool closed = false;
    };

    bool prepareTarget();
    void resetScanBounds(int y0, int y1);
    void addEdgeAA(double x0, double y0, double x1, double y1);
    void addEdgeNonAA(double x0, double y0, double x1, double y1);
    void flushScanlines(std::uint32_t srcPremul, std::uint32_t opacity8,
                        CompositionMode mode, FillRule rule, bool antialiasing);

    void fillSubpaths(const PaintState &state, FillRule rule);
    void strokeSubpaths(const PaintState &state);
    void strokeSingleSubpath(const PointF *pts, int count, bool closed,
                             double halfWidth, PenCapStyle cap, PenJoinStyle join,
                             double miterLimit, const Transform *postTransform,
                             bool antialiasing, double deviceScale);

    void extractPathSubpaths(const Path &path, const Transform &transform,
                             double flattenThreshold, bool closeAll);

    Image *m_image = nullptr;
    unsigned char *m_baseBits = nullptr;
    int m_width = 0;
    int m_height = 0;
    int m_bytesPerLine = 0;

    int m_bufWidth = 0;
    int m_bufHeight = 0;
    int m_deltaStride = 0;
    int m_minY = 0;
    int m_maxY = -1;
    std::vector<float> m_delta;
    std::vector<int> m_rowMinX;
    std::vector<int> m_rowMaxX;

    std::vector<PointF> m_points;
    std::vector<SubpathRange> m_subpaths;
    std::vector<PointF> m_dashPoints;
    std::vector<PointF> m_segDirs;
    std::vector<PointF> m_segNormals;
    std::vector<PointF> m_capTable;
};

} // namespace AnimeCore

#endif // ANIMECORE_RASTERDEVICE_H
