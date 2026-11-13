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

struct FieldScopePass {
    double tagLo = 0.0;
    double tagHi = 0.0;
    double opacity = 1.0;
    int tintR = 0;
    int tintG = 0;
    int tintB = 0;
    int tintA = 0;
};

class RecordingDevice : public AnimeCore::PaintDevice
{
public:
    using ImageSink = std::function<void(const std::string &key, const AnimeCore::Image &image)>;
    using ImageForget = std::function<void(const std::string &key)>;

    // What the client may hold for one board. The playback cache's own budget, so
    // a loop that fits there is only sent once; onion ghosts and everything else
    // that stopped being drawn make room for it.
    static constexpr std::int64_t kImageBudgetBytes = 512LL * 1024 * 1024;

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

    void setImageSink(ImageSink sink, ImageForget forget);
    void resetFrame();
    // Past the budget, tells the client to drop what was drawn longest ago. Never
    // what the frame just recorded draws: that is what the client is showing.
    void evictImages();
    // A new client holds nothing, so every image goes out again when next drawn.
    // tellClients: the clients already there are told to drop what they hold,
    // so that all of them hold what these books say and nothing more.
    void forgetSentImages(bool tellClients = false);
    void setImageBudget(std::int64_t bytes) { m_imageBudgetBytes = bytes; }
    const AnimeCore::Vector<RecordedOp> &ops() const { return m_ops; }

    // What is drawn between the two is shown through the displacement field the
    // page holds under key; version says which upload of it.
    void beginFieldScope(const AnimeCore::PaintState &state,
                         const std::string &key,
                         std::uint64_t version,
                         const AnimeCore::RectF &rect,
                         double maxEdge,
                         const AnimeCore::Vector<FieldScopePass> &passes);
    void endFieldScope();

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
    void forgetEmittedStyle();

    struct SentImage {
        std::int64_t bytes = 0;
        std::uint64_t lastFrame = 0;
    };

    ImageSink m_imageSink;
    ImageForget m_imageForget;
    AnimeCore::Hash<std::string, SentImage> m_sentImages;
    AnimeCore::Hash<std::int64_t, std::string> m_cacheKeyToHash;
    std::int64_t m_sentImageBytes = 0;
    std::int64_t m_imageBudgetBytes = kImageBudgetBytes;
    std::uint64_t m_frameNumber = 0;

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
