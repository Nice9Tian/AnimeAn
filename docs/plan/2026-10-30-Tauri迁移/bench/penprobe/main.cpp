#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFocusEvent>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTabletEvent>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

struct ProbeSample {
    double x = 0.0;
    double y = 0.0;
    double timeMs = 0.0;
    double pressure = 0.0;
    double tiltX = 0.0;
    double tiltY = 0.0;
};

struct StrokeStats {
    int index = 0;
    QString pointerType;
    int sampleCount = 0;
    double durationMs = 0.0;
    double avgHz = 0.0;
    double medianHz = 0.0;
    double pressureMin = 0.0;
    double pressureMax = 0.0;
    double tiltMax = 0.0;
    bool cancelled = false;
};

struct BucketSummary {
    QString pointerType;
    int strokeCount = 0;
    int sampleCount = 0;
    double avgSamplesPerStroke = 0.0;
    double durationMs = 0.0;
    double avgHz = 0.0;
    double medianHz = 0.0;
    double pressureMin = 0.0;
    double pressureMax = 0.0;
    double tiltMax = 0.0;
    int cancelCount = 0;
    bool cancelled = false;
    std::vector<StrokeStats> recentStrokes;
};

double round2(double v)
{
    if (!std::isfinite(v)) return 0.0;
    return std::round(v * 100.0) / 100.0;
}

double round4(double v)
{
    if (!std::isfinite(v)) return 0.0;
    return std::round(v * 10000.0) / 10000.0;
}

double computeMedian(std::vector<double> values)
{
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const size_t mid = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values[mid];
    }
    return (values[mid - 1] + values[mid]) * 0.5;
}

StrokeStats computeStrokeStats(const std::vector<ProbeSample> &samples, int index,
                               const QString &pointerType, bool cancelled)
{
    StrokeStats out;
    out.index = index;
    out.pointerType = pointerType;
    out.cancelled = cancelled;
    out.sampleCount = static_cast<int>(samples.size());
    if (samples.empty()) {
        return out;
    }

    const double firstTime = samples.front().timeMs;
    const double lastTime = samples.back().timeMs;
    const double rawDuration =
        samples.size() >= 2 ? std::max(0.0, lastTime - firstTime) : 0.0;
    out.durationMs = round2(rawDuration);
    out.avgHz = (samples.size() >= 2 && rawDuration > 0.0)
                    ? round2(((static_cast<double>(samples.size()) - 1.0) * 1000.0) / rawDuration)
                    : 0.0;

    std::vector<double> instantRates;
    instantRates.reserve(samples.size());
    for (size_t i = 1; i < samples.size(); ++i) {
        const double dt = samples[i].timeMs - samples[i - 1].timeMs;
        if (dt > 0.0) {
            instantRates.push_back(1000.0 / dt);
        }
    }
    out.medianHz = instantRates.empty() ? 0.0 : round2(computeMedian(std::move(instantRates)));

    double pMin = std::numeric_limits<double>::infinity();
    double pMax = -std::numeric_limits<double>::infinity();
    double tMax = 0.0;
    for (const ProbeSample &s : samples) {
        if (s.pressure < pMin) pMin = s.pressure;
        if (s.pressure > pMax) pMax = s.pressure;
        const double tilt = std::hypot(s.tiltX, s.tiltY);
        if (tilt > tMax) tMax = tilt;
    }
    out.pressureMin = std::isfinite(pMin) ? round4(pMin) : 0.0;
    out.pressureMax = std::isfinite(pMax) ? round4(pMax) : 0.0;
    out.tiltMax = round2(tMax);
    return out;
}

BucketSummary summarizeBucket(const std::vector<StrokeStats> &strokes,
                              const QString &pointerType, int recentLimit = 10)
{
    BucketSummary out;
    out.pointerType = pointerType;

    std::vector<StrokeStats> matching;
    for (const StrokeStats &s : strokes) {
        if (s.pointerType == pointerType) {
            matching.push_back(s);
        }
    }
    out.strokeCount = static_cast<int>(matching.size());
    if (matching.empty()) {
        return out;
    }

    int totalIntervals = 0;
    double totalDuration = 0.0;
    double pMin = std::numeric_limits<double>::infinity();
    double pMax = -std::numeric_limits<double>::infinity();
    double tMax = 0.0;
    std::vector<double> medians;

    for (const StrokeStats &s : matching) {
        out.sampleCount += s.sampleCount;
        totalIntervals += std::max(0, s.sampleCount - 1);
        totalDuration += s.durationMs;
        if (s.sampleCount > 0) {
            if (s.pressureMin < pMin) pMin = s.pressureMin;
            if (s.pressureMax > pMax) pMax = s.pressureMax;
            if (s.tiltMax > tMax) tMax = s.tiltMax;
        }
        if (s.medianHz > 0.0) {
            medians.push_back(s.medianHz);
        }
        if (s.cancelled) {
            out.cancelCount += 1;
        }
    }

    out.avgSamplesPerStroke =
        round2(static_cast<double>(out.sampleCount) / static_cast<double>(out.strokeCount));
    out.durationMs = round2(totalDuration);
    out.avgHz = totalDuration > 0.0
                    ? round2((static_cast<double>(totalIntervals) * 1000.0) / totalDuration)
                    : 0.0;
    out.medianHz = medians.empty() ? 0.0 : round2(computeMedian(std::move(medians)));
    out.pressureMin = std::isfinite(pMin) ? round4(pMin) : 0.0;
    out.pressureMax = std::isfinite(pMax) ? round4(pMax) : 0.0;
    out.tiltMax = round2(tMax);
    out.cancelled = out.cancelCount > 0;

    const int startIdx = std::max(0, static_cast<int>(matching.size()) - recentLimit);
    for (size_t i = static_cast<size_t>(startIdx); i < matching.size(); ++i) {
        out.recentStrokes.push_back(matching[i]);
    }
    return out;
}

QJsonObject strokeToJson(const StrokeStats &s)
{
    QJsonObject obj;
    obj["index"] = s.index;
    obj["pointerType"] = s.pointerType;
    obj["sampleCount"] = s.sampleCount;
    obj["durationMs"] = s.durationMs;
    obj["avgHz"] = s.avgHz;
    obj["medianHz"] = s.medianHz;
    obj["pressureMin"] = s.pressureMin;
    obj["pressureMax"] = s.pressureMax;
    obj["tiltMax"] = s.tiltMax;
    obj["cancelled"] = s.cancelled;
    return obj;
}

QJsonObject bucketToJson(const BucketSummary &b)
{
    QJsonObject obj;
    obj["pointerType"] = b.pointerType;
    obj["strokeCount"] = b.strokeCount;
    obj["sampleCount"] = b.sampleCount;
    obj["avgSamplesPerStroke"] = b.avgSamplesPerStroke;
    obj["durationMs"] = b.durationMs;
    obj["avgHz"] = b.avgHz;
    obj["medianHz"] = b.medianHz;
    obj["pressureMin"] = b.pressureMin;
    obj["pressureMax"] = b.pressureMax;
    obj["tiltMax"] = b.tiltMax;
    obj["cancelCount"] = b.cancelCount;
    obj["cancelled"] = b.cancelled;
    QJsonArray recent;
    for (const StrokeStats &s : b.recentStrokes) {
        recent.append(strokeToJson(s));
    }
    obj["recentStrokes"] = recent;
    return obj;
}

QString formatStrokeLine(const StrokeStats &s)
{
    return QString("#%1 [%2] samples=%3 dur=%4ms avg=%5Hz med=%6Hz p=%7..%8 tiltMax=%9deg cancel=%10")
        .arg(s.index)
        .arg(s.pointerType)
        .arg(s.sampleCount)
        .arg(s.durationMs, 0, 'f', 1)
        .arg(s.avgHz, 0, 'f', 1)
        .arg(s.medianHz, 0, 'f', 1)
        .arg(s.pressureMin, 0, 'f', 3)
        .arg(s.pressureMax, 0, 'f', 3)
        .arg(s.tiltMax, 0, 'f', 1)
        .arg(s.cancelled ? "YES" : "no");
}

QString formatBucketLine(const QString &label, const BucketSummary &b)
{
    return QString("%1(%2笔): samples=%3 (avg=%4/笔) dur=%5ms avg=%6Hz med=%7Hz p=%8..%9 tiltMax=%10deg cancel=%11")
        .arg(label)
        .arg(b.strokeCount)
        .arg(b.sampleCount)
        .arg(b.avgSamplesPerStroke, 0, 'f', 1)
        .arg(b.durationMs, 0, 'f', 1)
        .arg(b.avgHz, 0, 'f', 1)
        .arg(b.medianHz, 0, 'f', 1)
        .arg(b.pressureMin, 0, 'f', 3)
        .arg(b.pressureMax, 0, 'f', 3)
        .arg(b.tiltMax, 0, 'f', 1)
        .arg(b.cancelCount);
}

class PenProbeWidget : public QWidget {
public:
    PenProbeWidget()
    {
        setWindowTitle("AnimeAn Qt Pen Probe (Press C to copy JSON)");
        resize(1027, 775);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_OpaquePaintEvent);
        m_clock.start();
    }

protected:
    void tabletEvent(QTabletEvent *event) override
    {
        event->accept();
        const QPointF pos = event->position();
        const double nowMs = static_cast<double>(m_clock.nsecsElapsed()) / 1e6;
        const ProbeSample sample{
            pos.x(),
            pos.y(),
            nowMs,
            static_cast<double>(event->pressure()),
            static_cast<double>(event->xTilt()),
            static_cast<double>(event->yTilt()),
        };

        switch (event->type()) {
        case QEvent::TabletPress:
            if (m_active) {
                finishStroke(true);
            }
            m_tabletDown = true;
            m_active = true;
            m_activeType = QStringLiteral("pen");
            m_activeSamples.clear();
            m_activeSamples.push_back(sample);
            update();
            break;
        case QEvent::TabletMove:
            if (m_active && m_activeType == QLatin1String("pen")) {
                m_activeSamples.push_back(sample);
                update();
            }
            break;
        case QEvent::TabletRelease:
            m_tabletDown = false;
            if (m_active && m_activeType == QLatin1String("pen")) {
                finishStroke(false);
                update();
            }
            break;
        default:
            break;
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (m_tabletDown || event->source() != Qt::MouseEventNotSynthesized) {
            event->accept();
            return;
        }
        if (event->button() != Qt::LeftButton) {
            return;
        }
        if (m_active) {
            finishStroke(true);
        }
        const QPointF pos = event->position();
        const double nowMs = static_cast<double>(m_clock.nsecsElapsed()) / 1e6;
        m_active = true;
        m_activeType = QStringLiteral("mouse");
        m_activeSamples.clear();
        m_activeSamples.push_back(ProbeSample{pos.x(), pos.y(), nowMs, 0.5, 0.0, 0.0});
        update();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_tabletDown || event->source() != Qt::MouseEventNotSynthesized) {
            event->accept();
            return;
        }
        if (!m_active || m_activeType != QLatin1String("mouse")) {
            return;
        }
        if (!(event->buttons() & Qt::LeftButton)) {
            return;
        }
        const QPointF pos = event->position();
        const double nowMs = static_cast<double>(m_clock.nsecsElapsed()) / 1e6;
        m_activeSamples.push_back(ProbeSample{pos.x(), pos.y(), nowMs, 0.5, 0.0, 0.0});
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_tabletDown || event->source() != Qt::MouseEventNotSynthesized) {
            event->accept();
            return;
        }
        if (m_active && m_activeType == QLatin1String("mouse") &&
            event->button() == Qt::LeftButton) {
            finishStroke(false);
            update();
        }
    }

    void focusOutEvent(QFocusEvent *event) override
    {
        QWidget::focusOutEvent(event);
        if (m_active) {
            m_tabletDown = false;
            finishStroke(true);
            update();
        }
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_C) {
            copyAndPrintJson();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_R) {
            m_active = false;
            m_tabletDown = false;
            m_activeSamples.clear();
            m_drawnStrokes.clear();
            m_strokes.clear();
            m_statusMessage = QStringLiteral("已清空统计");
            update();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        p.setRenderHint(QPainter::Antialiasing, true);

        auto drawStrokePoints = [&p](const QString &type,
                                     const std::vector<ProbeSample> &pts) {
            if (pts.empty()) return;
            const QColor color = (type == QLatin1String("pen")) ? QColor(17, 17, 17)
                                                                : QColor(31, 90, 166);
            if (pts.size() == 1) {
                const double r = std::max(1.0, (pts[0].pressure > 0.0 ? pts[0].pressure : 0.5) * 3.0);
                p.setPen(Qt::NoPen);
                p.setBrush(color);
                p.drawEllipse(QPointF(pts[0].x, pts[0].y), r, r);
                return;
            }
            p.setBrush(Qt::NoBrush);
            for (size_t i = 1; i < pts.size(); ++i) {
                const double w = std::max(1.0, (pts[i].pressure > 0.0 ? pts[i].pressure : 0.5) * 6.0);
                p.setPen(QPen(color, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.drawLine(QPointF(pts[i - 1].x, pts[i - 1].y),
                           QPointF(pts[i].x, pts[i].y));
            }
        };

        for (const DrawnStroke &ds : m_drawnStrokes) {
            drawStrokePoints(ds.pointerType, ds.samples);
        }
        if (m_active) {
            drawStrokePoints(m_activeType, m_activeSamples);
        }

        const BucketSummary penSum = summarizeBucket(m_strokes, QStringLiteral("pen"), 10);
        const BucketSummary mouseSum = summarizeBucket(m_strokes, QStringLiteral("mouse"), 10);

        QStringList lines;
        if (m_strokes.empty()) {
            lines.append(QStringLiteral("等待落笔（pen / mouse 分开统计）… [按 C 复制 JSON 并输出到 stdout，按 R 清空]"));
        } else {
            lines.append(QString("最新 %1 | [按 C 复制 JSON 并输出到 stdout，按 R 清空] %2")
                             .arg(formatStrokeLine(m_strokes.back()))
                             .arg(m_statusMessage));
        }
        lines.append(formatBucketLine(QStringLiteral("合计[pen]"), penSum));
        for (const StrokeStats &s : penSum.recentStrokes) {
            lines.append(QStringLiteral("  ") + formatStrokeLine(s));
        }
        lines.append(formatBucketLine(QStringLiteral("合计[mouse]"), mouseSum));
        for (const StrokeStats &s : mouseSum.recentStrokes) {
            lines.append(QStringLiteral("  ") + formatStrokeLine(s));
        }

        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSize(9);
        p.setFont(font);
        const QFontMetrics fm(font);
        const int lineH = fm.height() + 2;
        const int boxH = static_cast<int>(lines.size()) * lineH + 12;

        p.setPen(QPen(QColor(204, 204, 204), 1.0));
        p.setBrush(QColor(255, 255, 255, 235));
        p.drawRect(QRect(8, 8, std::max(200, width() - 16), boxH));

        p.setPen(QColor(17, 17, 17));
        int y = 8 + 6 + fm.ascent();
        for (const QString &line : lines) {
            p.drawText(16, y, line);
            y += lineH;
        }
    }

private:
    struct DrawnStroke {
        QString pointerType;
        std::vector<ProbeSample> samples;
    };

    void finishStroke(bool cancelled)
    {
        if (!m_active) return;
        const int nextIndex = static_cast<int>(m_strokes.size()) + 1;
        StrokeStats stats =
            computeStrokeStats(m_activeSamples, nextIndex, m_activeType, cancelled);
        m_strokes.push_back(stats);
        m_drawnStrokes.push_back(DrawnStroke{m_activeType, m_activeSamples});
        m_activeSamples.clear();
        m_active = false;
        m_statusMessage.clear();
    }

    QString buildJsonReport() const
    {
        const BucketSummary penSum = summarizeBucket(m_strokes, QStringLiteral("pen"), 10);
        const BucketSummary mouseSum = summarizeBucket(m_strokes, QStringLiteral("mouse"), 10);

        QJsonObject summary;
        summary["pen"] = bucketToJson(penSum);
        summary["mouse"] = bucketToJson(mouseSum);

        QJsonArray recentArr;
        const int startIdx = std::max(0, static_cast<int>(m_strokes.size()) - 10);
        for (size_t i = static_cast<size_t>(startIdx); i < m_strokes.size(); ++i) {
            recentArr.append(strokeToJson(m_strokes[i]));
        }

        QJsonArray allArr;
        for (const StrokeStats &s : m_strokes) {
            allArr.append(strokeToJson(s));
        }

        QJsonObject root;
        root["probe"] = QStringLiteral("qt");
        root["recentLimit"] = 10;
        root["summary"] = summary;
        root["recentStrokes"] = recentArr;
        root["strokes"] = allArr;

        return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }

    void copyAndPrintJson()
    {
        const QString jsonText = buildJsonReport();
        if (QClipboard *cb = QGuiApplication::clipboard()) {
            cb->setText(jsonText);
        }
        const QByteArray utf8 = jsonText.toUtf8();
        std::fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stdout);
        if (!utf8.endsWith('\n')) {
            std::fputc('\n', stdout);
        }
        std::fflush(stdout);
        m_statusMessage = QStringLiteral("(已复制 JSON 到剪贴板并打印到 stdout)");
        update();
    }

    QElapsedTimer m_clock;
    bool m_active = false;
    bool m_tabletDown = false;
    QString m_activeType;
    std::vector<ProbeSample> m_activeSamples;
    std::vector<DrawnStroke> m_drawnStrokes;
    std::vector<StrokeStats> m_strokes;
    QString m_statusMessage;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    PenProbeWidget w;
    w.show();
    return app.exec();
}
