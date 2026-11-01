#ifndef RECORDINGDEVICE_H
#define RECORDINGDEVICE_H

#include "algorithm/core/containers.h"
#include "algorithm/core/image.h"
#include "algorithm/core/painter.h"

#include <cstdint>
#include <functional>
#include <string>

struct RecordedOp {
    std::uint64_t hash = 0;
    std::string json;
};

class RecordingDevice : public AnimeCore::PaintDevice
{
public:
    using ImageSink = std::function<void(const std::string &key, const AnimeCore::Image &image)>;

    RecordingDevice();
    ~RecordingDevice() override;

    void drawRects(const AnimeCore::PaintState &state, const AnimeCore::RectF *rects, int count) override;
    void drawLines(const AnimeCore::PaintState &state, const AnimeCore::LineF *lines, int count) override;
    void drawEllipse(const AnimeCore::PaintState &state, const AnimeCore::RectF &rect) override;
    void drawPath(const AnimeCore::PaintState &state, const AnimeCore::Path &path) override;
    void drawPoints(const AnimeCore::PaintState &state, const AnimeCore::PointF *points, int count) override;
    void drawPolygon(const AnimeCore::PaintState &state, const AnimeCore::PointF *points, int count,
                     AnimeCore::PolygonMode mode) override;
    void drawImage(const AnimeCore::PaintState &state, const AnimeCore::RectF &target,
                   const AnimeCore::Image &image, const AnimeCore::RectF &source) override;

    void setImageSink(ImageSink sink);
    void resetFrame();
    const AnimeCore::Vector<RecordedOp> &ops() const { return m_ops; }

private:
    void syncCommonState(const AnimeCore::PaintState &state);
    void syncStateForShape(const AnimeCore::PaintState &state, bool usePen, bool useBrush);
    void syncStateForImage(const AnimeCore::PaintState &state);
    void syncTransform(const AnimeCore::Transform &transform);
    void syncOpacity(double opacity);
    void syncCompositionMode(AnimeCore::CompositionMode mode);
    void syncPen(const AnimeCore::Pen &pen);
    void syncBrush(const AnimeCore::Brush &brush);
    void syncSmooth(bool smoothImages);

    void emitPathOp(const AnimeCore::Path &path);
    std::string ensureImageSent(const AnimeCore::Image &image);
    void appendOp(std::string json);

    ImageSink m_imageSink;
    AnimeCore::Set<std::string> m_sentImageKeys;
    AnimeCore::Hash<std::int64_t, std::string> m_cacheKeyToHash;

    AnimeCore::Vector<RecordedOp> m_ops;

    bool m_hasEmittedTransform = false;
    std::string m_emittedTransformBytes;

    bool m_hasEmittedPen = false;
    std::string m_emittedPenBytes;

    bool m_hasEmittedBrush = false;
    std::string m_emittedBrushBytes;

    double m_emittedOpacity = 1.0;
    AnimeCore::CompositionMode m_emittedMode = AnimeCore::CompositionMode_SourceOver;
    int m_emittedSmooth = -1;
};

#endif // RECORDINGDEVICE_H
