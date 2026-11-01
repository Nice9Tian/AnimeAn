#ifndef RECORDINGENGINE_H
#define RECORDINGENGINE_H

#include <QBrush>
#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QPaintEngine>
#include <QPainterPath>
#include <QPen>
#include <QSet>
#include <QString>
#include <QTransform>
#include <QVector>

#include <functional>

struct RecordedOp {
    quint64 hash = 0;
    QByteArray json;
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

class RecordingPaintEngine : public QPaintEngine
{
public:
    using ImageSink = std::function<void(const QString &key, const QImage &image)>;

    RecordingPaintEngine();
    ~RecordingPaintEngine() override;

    bool begin(QPaintDevice *pdev) override;
    bool end() override;

    void updateState(const QPaintEngineState &state) override;

    void drawRects(const QRect *rects, int rectCount) override;
    void drawRects(const QRectF *rects, int rectCount) override;
    void drawLines(const QLine *lines, int lineCount) override;
    void drawLines(const QLineF *lines, int lineCount) override;
    void drawEllipse(const QRectF &r) override;
    void drawEllipse(const QRect &r) override;
    void drawPath(const QPainterPath &path) override;
    void drawPoints(const QPointF *points, int pointCount) override;
    void drawPoints(const QPoint *points, int pointCount) override;
    void drawPolygon(const QPointF *points, int pointCount, PolygonDrawMode mode) override;
    void drawPolygon(const QPoint *points, int pointCount, PolygonDrawMode mode) override;
    void drawPixmap(const QRectF &r, const QPixmap &pm, const QRectF &sr) override;
    void drawImage(const QRectF &r, const QImage &pm, const QRectF &sr,
                   Qt::ImageConversionFlags flags = Qt::AutoColor) override;
    void drawTiledPixmap(const QRectF &r, const QPixmap &pixmap, const QPointF &s) override;
    void drawTextItem(const QPointF &p, const QTextItem &textItem) override;

    Type type() const override { return QPaintEngine::User; }

    void setImageSink(ImageSink sink);
    void resetFrame();
    const QVector<RecordedOp> &ops() const { return m_ops; }

    void beginFieldScope(const QString &key,
                         quint64 version,
                         const QRectF &rect,
                         double maxEdge,
                         const QVector<FieldScopePass> &passes);
    void endFieldScope();

private:
    void syncCommonState();
    void syncStateForShape(bool usePen, bool useBrush);
    void syncStateForImage();
    void syncTransform();
    void syncOpacity();
    void syncCompositionMode();
    void syncPen(const QPen &pen);
    void syncBrush(const QBrush &brush);
    void syncSmooth();
    void updateClip(const QPaintEngineState &state);

    void emitPathOp(const QPainterPath &path);
    QString ensureImageSent(const QImage &image);
    void appendOp(QByteArray json);

    ImageSink m_imageSink;
    QSet<QString> m_sentImageKeys;
    QHash<qint64, QString> m_cacheKeyToHash;

    QVector<RecordedOp> m_ops;

    QTransform m_currentTransform;
    QPen m_currentPen;
    QBrush m_currentBrush;
    qreal m_currentOpacity = 1.0;
    QPainter::CompositionMode m_currentMode = QPainter::CompositionMode_SourceOver;
    QPainter::RenderHints m_currentHints;
    QPainterPath m_lastClipPath;
    QTransform m_lastClipTransform;
    bool m_hasLastClip = false;

    bool m_hasEmittedTransform = false;
    QByteArray m_emittedTransformBytes;

    bool m_hasEmittedPen = false;
    QByteArray m_emittedPenBytes;

    bool m_hasEmittedBrush = false;
    QByteArray m_emittedBrushBytes;

    qreal m_emittedOpacity = 1.0;
    QPainter::CompositionMode m_emittedMode = QPainter::CompositionMode_SourceOver;
    bool m_emittedClipEnabled = false;
    QByteArray m_emittedClipBytes;
    int m_emittedSmooth = -1;
};

#endif // RECORDINGENGINE_H
