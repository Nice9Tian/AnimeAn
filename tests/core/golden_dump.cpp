#ifdef GOLDEN_OLD
#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QPainterPath>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QTransform>
#include <QVector>

using GPoint = QPoint;
using GPointF = QPointF;
using GSize = QSize;
using GRect = QRect;
using GRectF = QRectF;
using GLineF = QLineF;
using GTransform = QTransform;
using GColor = QColor;
using GImage = QImage;
using GPath = QPainterPath;
using GString = QString;
template <typename T>
using GVector = QVector<T>;
using GJsonObject = QJsonObject;
using GJsonDocument = QJsonDocument;

#else
#include "algorithm/core/color.h"
#include "algorithm/core/containers.h"
#include "algorithm/core/geometry.h"
#include "algorithm/core/image.h"
#include "algorithm/core/json.h"
#include "algorithm/core/path.h"
#include "algorithm/core/string.h"

using GPoint = AnimeCore::Point;
using GPointF = AnimeCore::PointF;
using GSize = AnimeCore::Size;
using GRect = AnimeCore::Rect;
using GRectF = AnimeCore::RectF;
using GLineF = AnimeCore::LineF;
using GTransform = AnimeCore::Transform;
using GColor = AnimeCore::Color;
using GImage = AnimeCore::Image;
using GPath = AnimeCore::Path;
using GString = AnimeCore::String;
template <typename T>
using GVector = AnimeCore::Vector<T>;
using GJsonObject = AnimeCore::JsonObject;
using GJsonDocument = AnimeCore::JsonDocument;
#endif

#include "algorithm/animemodel.h"
#include "algorithm/beziersplit.h"
#include "algorithm/scenehistory.h"
#include "algorithm/vectorlogic.h"
#include "algorithm/viewscale.h"
#include "projectio.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

inline std::string toStdStr(const GString &s)
{
#ifdef GOLDEN_OLD
    const QByteArray utf8 = s.toUtf8();
    return std::string(utf8.constData(), static_cast<std::size_t>(utf8.size()));
#else
    return s.toUtf8();
#endif
}

inline GString fromUtf8Str(const char *s)
{
#ifdef GOLDEN_OLD
    return QString::fromUtf8(s);
#else
    return AnimeCore::String::fromUtf8(s);
#endif
}

inline std::string jsonToCompactBytes(const GJsonObject &obj)
{
#ifdef GOLDEN_OLD
    const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
#else
    return AnimeCore::JsonDocument(obj).toJson(AnimeCore::JsonDocument::Compact);
#endif
}

inline GJsonObject jsonFromBytes(const std::string &bytes)
{
#ifdef GOLDEN_OLD
    return QJsonDocument::fromJson(QByteArray(bytes.data(), static_cast<int>(bytes.size()))).object();
#else
    return AnimeCore::JsonDocument::fromJson(bytes).object();
#endif
}

// Portable SplitMix64 generator so both builds feed bit-identical inputs.
struct Rng {
    std::uint64_t state;
    explicit Rng(std::uint64_t seed = 0x243F6A8885A308D3ULL) : state(seed) {}

    std::uint64_t nextU64()
    {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    double nextUnit()
    {
        return static_cast<double>(nextU64() >> 11) * (1.0 / 9007199254740992.0);
    }

    double uniform(double lo, double hi)
    {
        return lo + (hi - lo) * nextUnit();
    }

    int uniformInt(int lo, int hi)
    {
        return lo + static_cast<int>(nextU64() % static_cast<std::uint64_t>(hi - lo + 1));
    }
};

void writePointF(std::FILE *out, const GPointF &p)
{
    std::fprintf(out, " %.17g %.17g", p.x(), p.y());
}

void writeRectF(std::FILE *out, const GRectF &r)
{
    std::fprintf(out, " %.17g %.17g %.17g %.17g", r.x(), r.y(), r.width(), r.height());
}

void writePathInline(std::FILE *out, const GPath &path)
{
    const int n = path.elementCount();
    const double len = path.length();
    const GRectF br = path.boundingRect();
    std::fprintf(out, " ec %d len %.17g rect %.17g %.17g %.17g %.17g el",
                 n, len, br.x(), br.y(), br.width(), br.height());
    for (int i = 0; i < n; ++i) {
        const auto el = path.elementAt(i);
        std::fprintf(out, " %d %.17g %.17g", static_cast<int>(el.type), el.x, el.y);
    }
}

void writeStrokeInline(std::FILE *out, const AnimeVectorStroke &s)
{
    std::fprintf(out, " id %d prop %s pts %d",
                 s.id,
                 toStdStr(s.property).empty() ? "-" : toStdStr(s.property).c_str(),
                 static_cast<int>(s.points.size()));
    for (int i = 0; i < s.points.size(); ++i) {
        writePointF(out, s.points[i]);
    }
    std::fprintf(out, " lens %d", static_cast<int>(s.lengths.size()));
    for (int i = 0; i < s.lengths.size(); ++i) {
        std::fprintf(out, " %.17g", s.lengths[i]);
    }
    std::fprintf(out, " totalLen %.17g width %.17g pen %d rgba %d %d %d %d bounds",
                 s.totalLength, s.width, s.penStyle,
                 s.color.red(), s.color.green(), s.color.blue(), s.color.alpha());
    writeRectF(out, s.bounds);
    writePathInline(out, s.path);
}

void writeRangesInline(std::FILE *out, const GVector<AnimeVectorRange> &ranges)
{
    std::fprintf(out, " count %d", static_cast<int>(ranges.size()));
    for (int i = 0; i < ranges.size(); ++i) {
        std::fprintf(out, " %.17g %.17g", ranges[i].first, ranges[i].second);
    }
}

enum StrokeCategory {
    CatJitterCurve = 0,
    CatStraightLine = 1,
    CatSharpPolyline = 2,
    CatCircle = 3,
    CatSelfIntersect = 4,
    CatShortStroke = 5,
    CatDuplicatePoints = 6,
    CatCount = 7
};

const char *categoryName(int cat)
{
    switch (cat) {
    case CatJitterCurve: return "jitter_curve";
    case CatStraightLine: return "straight_line";
    case CatSharpPolyline: return "sharp_polyline";
    case CatCircle: return "circle";
    case CatSelfIntersect: return "self_intersect";
    case CatShortStroke: return "short_stroke";
    case CatDuplicatePoints: return "duplicate_points";
    default: return "unknown";
    }
}

GVector<GPointF> generateStrokePoints(Rng &rng, int category, int index)
{
    GVector<GPointF> pts;
    const double ox = rng.uniform(40.0, 900.0);
    const double oy = rng.uniform(40.0, 600.0);

    switch (category) {
    case CatJitterCurve: {
        const int n = rng.uniformInt(18, 45);
        const double len = rng.uniform(60.0, 360.0);
        const double baseAngle = rng.uniform(-kPi, kPi);
        const double amp1 = rng.uniform(8.0, 45.0);
        const double amp2 = rng.uniform(3.0, 20.0);
        const double freq1 = rng.uniform(1.0, 3.0);
        const double freq2 = rng.uniform(2.0, 5.0);
        const double jitterAmp = rng.uniform(0.2, 2.2);
        const double ca = std::cos(baseAngle);
        const double sa = std::sin(baseAngle);
        for (int i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(n - 1);
            const double u = t * len;
            const double v = amp1 * std::sin(t * kPi * freq1) + amp2 * std::cos(t * kPi * freq2);
            const double jx = rng.uniform(-jitterAmp, jitterAmp);
            const double jy = rng.uniform(-jitterAmp, jitterAmp);
            pts.append(GPointF(ox + u * ca - v * sa + jx, oy + u * sa + v * ca + jy));
        }
        break;
    }
    case CatStraightLine: {
        const int n = rng.uniformInt(2, 32);
        const double len = rng.uniform(12.0, 420.0);
        const double angle = (index % 8 == 0) ? 0.0
                           : (index % 8 == 1) ? (0.5 * kPi)
                           : rng.uniform(-kPi, kPi);
        const double dx = std::cos(angle) * len;
        const double dy = std::sin(angle) * len;
        const double tremor = (index % 2 == 0) ? 0.0 : rng.uniform(0.02, 0.35);
        for (int i = 0; i < n; ++i) {
            const double t = (n == 1) ? 0.0 : static_cast<double>(i) / static_cast<double>(n - 1);
            const double jx = (i == 0 || i == n - 1) ? 0.0 : rng.uniform(-tremor, tremor);
            const double jy = (i == 0 || i == n - 1) ? 0.0 : rng.uniform(-tremor, tremor);
            pts.append(GPointF(ox + dx * t + jx, oy + dy * t + jy));
        }
        break;
    }
    case CatSharpPolyline: {
        const int corners = rng.uniformInt(3, 7);
        const int stepsPerSeg = rng.uniformInt(3, 8);
        GVector<GPointF> verts;
        GPointF cur(ox, oy);
        verts.append(cur);
        double heading = rng.uniform(-kPi, kPi);
        for (int c = 0; c < corners; ++c) {
            const double turn = (c % 2 == 0 ? 1.0 : -1.0) * rng.uniform(0.45 * kPi, 0.85 * kPi);
            heading += turn;
            const double segLen = rng.uniform(25.0, 120.0);
            cur = GPointF(cur.x() + std::cos(heading) * segLen,
                          cur.y() + std::sin(heading) * segLen);
            verts.append(cur);
        }
        const double tremor = rng.uniform(0.0, 0.4);
        for (int c = 0; c + 1 < verts.size(); ++c) {
            const GPointF a = verts[c];
            const GPointF b = verts[c + 1];
            const int sEnd = (c + 2 == verts.size()) ? stepsPerSeg : (stepsPerSeg - 1);
            for (int s = 0; s <= sEnd; ++s) {
                const double t = static_cast<double>(s) / static_cast<double>(stepsPerSeg);
                const bool isCorner = (s == 0 || s == stepsPerSeg);
                const double jx = isCorner ? 0.0 : rng.uniform(-tremor, tremor);
                const double jy = isCorner ? 0.0 : rng.uniform(-tremor, tremor);
                pts.append(GPointF(a.x() + (b.x() - a.x()) * t + jx,
                                   a.y() + (b.y() - a.y()) * t + jy));
            }
        }
        break;
    }
    case CatCircle: {
        const int n = rng.uniformInt(20, 48);
        const double rx = rng.uniform(10.0, 140.0);
        const double ry = (index % 3 == 0) ? rx : rng.uniform(10.0, 140.0);
        const double startAngle = rng.uniform(0.0, 2.0 * kPi);
        const double sweep = (index % 4 == 0) ? (2.0 * kPi) : rng.uniform(1.6 * kPi, 2.05 * kPi);
        const double tremor = (index % 3 == 0) ? 0.0 : rng.uniform(0.05, 0.5);
        for (int i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(n - 1);
            const double a = startAngle + sweep * t;
            const double jx = (i == 0 || i == n - 1) ? 0.0 : rng.uniform(-tremor, tremor);
            const double jy = (i == 0 || i == n - 1) ? 0.0 : rng.uniform(-tremor, tremor);
            pts.append(GPointF(ox + rx * std::cos(a) + jx, oy + ry * std::sin(a) + jy));
        }
        if (index % 2 == 0 && !pts.isEmpty()) {
            pts[pts.size() - 1] = pts[0];
        }
        break;
    }
    case CatSelfIntersect: {
        const int n = rng.uniformInt(28, 52);
        const double rx = rng.uniform(30.0, 150.0);
        const double ry = rng.uniform(30.0, 150.0);
        const double phase = rng.uniform(0.0, kPi);
        const double freqX = (index % 2 == 0) ? 1.0 : 2.0;
        const double freqY = (index % 2 == 0) ? 2.0 : 3.0;
        for (int i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(n - 1);
            const double a = t * 2.0 * kPi;
            pts.append(GPointF(ox + rx * std::sin(freqX * a + phase),
                               oy + ry * std::sin(freqY * a)));
        }
        break;
    }
    case CatShortStroke: {
        const int n = (index % 5) + 1;
        const double span = rng.uniform(0.0, 3.2);
        GPointF cur(ox, oy);
        pts.append(cur);
        for (int i = 1; i < n; ++i) {
            const double a = rng.uniform(-kPi, kPi);
            const double d = rng.uniform(0.0, span / static_cast<double>(n));
            cur = GPointF(cur.x() + std::cos(a) * d, cur.y() + std::sin(a) * d);
            pts.append(cur);
        }
        break;
    }
    case CatDuplicatePoints: {
        const int baseN = rng.uniformInt(6, 18);
        GPointF cur(ox, oy);
        if (index % 10 == 0) {
            for (int i = 0; i < baseN; ++i) {
                pts.append(cur);
            }
            break;
        }
        double angle = rng.uniform(-kPi, kPi);
        for (int i = 0; i < baseN; ++i) {
            pts.append(cur);
            if (rng.uniformInt(0, 2) == 0) {
                const int dupCount = rng.uniformInt(1, 4);
                for (int d = 0; d < dupCount; ++d) {
                    const double micro = (d % 2 == 0) ? 0.0 : rng.uniform(-1e-5, 1e-5);
                    pts.append(GPointF(cur.x() + micro, cur.y() - micro));
                }
            }
            angle += rng.uniform(-0.4, 0.4);
            const double step = rng.uniform(4.0, 22.0);
            cur = GPointF(cur.x() + std::cos(angle) * step, cur.y() + std::sin(angle) * step);
        }
        break;
    }
    default:
        break;
    }
    return pts;
}

void dumpModelStep(std::FILE *out, const char *stepName, int stepIdx, const AnimeSceneModel &model)
{
    std::fprintf(out, "model_step %d name %s layers %d frames %d assets %d curL %d curF %d curA %d",
                 stepIdx, stepName,
                 model.layerCount(), model.frameCount(), model.assetCount(),
                 model.currentLayer(), model.currentFrame(), model.currentAsset());
    for (int l = 0; l < model.layerCount(); ++l) {
        std::fprintf(out, " L%d:%s(id=%d,p=%d,v=%d,lk=%d,op=%.17g,fill=%d,grp=%d)",
                     l,
                     toStdStr(model.layerName(l)).c_str(),
                     model.layerIdAt(l),
                     model.layerParentId(l),
                     model.layerVisible(l) ? 1 : 0,
                     model.layerLocked(l) ? 1 : 0,
                     model.layerOpacity(l),
                     model.isFillLayer(l) ? 1 : 0,
                     model.groupIdForLayer(l));
    }
    for (int f = 0; f < model.frameCount(); ++f) {
        std::fprintf(out, " F%d:%s(dup=%s,hold=%d)",
                     f,
                     toStdStr(model.frameName(f)).c_str(),
                     toStdStr(model.nextDuplicateName(f)).c_str(),
                     model.isHoldFrame(f) ? 1 : 0);
        writeRectF(out, model.contentBounds(f));
        writeRectF(out, model.fillBoundaryBounds(f));
        for (int l = 0; l < model.layerCount(); ++l) {
            const AnimeVectorImageModel *img = model.imageAt(f, l);
            const int sc = img ? img->strokeCount() : 0;
            const int fc = img ? img->fillCount() : 0;
            const GRectF b = img ? img->bounds() : GRectF();
            std::fprintf(out, " C(%d,%d):sc=%d,fc=%d,b=", f, l, sc, fc);
            writeRectF(out, b);
        }
    }
    std::fprintf(out, "\n");
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "Usage: golden_dump <output_path>\n");
        return 1;
    }

    std::FILE *out = std::fopen(argv[1], "wb");
    if (!out) {
        std::fprintf(stderr, "Failed to open output file: %s\n", argv[1]);
        return 1;
    }

    // -----------------------------------------------------------------------
    // 1. AnimeViewScale public functions & constants
    // -----------------------------------------------------------------------
    std::fprintf(out, "viewscale_const 0 %.17g %.17g\n",
                 static_cast<double>(AnimeViewScale::kBudgetMinZoom),
                 static_cast<double>(AnimeViewScale::kBudgetMaxZoom));

    {
        Rng vsRng(0xA11CE001ULL);
        const double zoomSpecials[] = {
            -5.0, 0.0, 0.05, 0.1, 0.25, 0.5, 1.0, 1.5, 2.0, 4.0, 8.0, 16.0, 32.0, 1e9, 2e9
        };
        int vsIdx = 0;
        for (int i = 0; i < 60; ++i) {
            const double zoom = (i < 15) ? zoomSpecials[i] : vsRng.uniform(-2.0, 25.0);
            const GPointF pt(vsRng.uniform(-500.0, 1500.0), vsRng.uniform(-500.0, 1500.0));
            const GPointF pan(vsRng.uniform(-300.0, 300.0), vsRng.uniform(-300.0, 300.0));
            const double len = vsRng.uniform(0.1, 500.0);
            const double rawScale = vsRng.uniform(-1.0, 8.0);

            const double sz = AnimeViewScale::safeZoom(zoom);
            const GPointF docPt = AnimeViewScale::toDocument(pt, zoom, pan);
            const GPointF scrPt = AnimeViewScale::toScreen(docPt, zoom, pan);
            const double docLen = AnimeViewScale::toDocumentLength(len, zoom);
            const double scrLen = AnimeViewScale::toScreenLength(docLen, zoom);
            const double psDefault = AnimeViewScale::pixelScale(zoom);
            const double psCustom = AnimeViewScale::pixelScale(zoom, 0.5, 8.0);
            const double cpsDefault = AnimeViewScale::clampPixelScale(rawScale);
            const double cpsCustom = AnimeViewScale::clampPixelScale(rawScale, 0.5, 8.0);

            std::fprintf(out, "viewscale %d %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g\n",
                         vsIdx++, sz,
                         docPt.x(), docPt.y(), scrPt.x(), scrPt.y(),
                         docLen, scrLen, psDefault, psCustom, cpsDefault, cpsCustom);
        }
    }

    // -----------------------------------------------------------------------
    // 2. Build all 75 AnimeStrokeFitSettings & 7x225 strokes (225 >= 200/cat)
    // -----------------------------------------------------------------------
    const int kSimplifyVals[5] = {0, 25, 50, 75, 100};
    const int kCornerVals[5] = {0, 25, 50, 75, 100};
    const double kPixelScaleVals[3] = {0.25, 1.0, 4.0};
    std::vector<AnimeStrokeFitSettings> fitSettingsList;
    for (int s : kSimplifyVals) {
        for (int c : kCornerVals) {
            for (double ps : kPixelScaleVals) {
                AnimeStrokeFitSettings st;
                st.simplify = s;
                st.corner = c;
                st.pixelScale = ps;
                fitSettingsList.push_back(st);
            }
        }
    }

    constexpr int kStrokesPerCategory = 225; // 3 full cycles of all 75 settings per category
    struct GeneratedStrokeCase {
        int category = 0;
        int indexInCat = 0;
        int globalIndex = 0;
        AnimeStrokeFitSettings settings;
        GVector<GPointF> points;
        AnimeVectorStroke stroke;
    };

    std::vector<GeneratedStrokeCase> allCases;
    allCases.reserve(CatCount * kStrokesPerCategory);
    {
        Rng strokeRng(0xBEEF123456789ULL);
        int globalIdx = 0;
        for (int cat = 0; cat < CatCount; ++cat) {
            for (int i = 0; i < kStrokesPerCategory; ++i) {
                GeneratedStrokeCase c;
                c.category = cat;
                c.indexInCat = i;
                c.globalIndex = globalIdx++;
                c.settings = fitSettingsList[static_cast<std::size_t>(i) % fitSettingsList.size()];
                c.points = generateStrokePoints(strokeRng, cat, i);
                allCases.push_back(std::move(c));
            }
        }
    }

    // -----------------------------------------------------------------------
    // 3. VectorLogic basic constants & displayStrokeWidth & AnimeOneEuroFilter
    // -----------------------------------------------------------------------
    std::fprintf(out, "vectorlogic_const 0 %.17g %.17g\n",
                 static_cast<double>(AnimeVectorLogic::kVectorRegionOverpaintWidth),
                 static_cast<double>(AnimeVectorLogic::epsilon()));

    {
        Rng dswRng(0x55AA01ULL);
        for (int i = 0; i < 60; ++i) {
            const double docW = (i == 0) ? 0.0 : dswRng.uniform(0.5, 25.0);
            const double zoom = (i == 1) ? 0.0 : dswRng.uniform(0.05, 8.0);
            const double minPx = (i == 2) ? 0.0 : dswRng.uniform(0.5, 3.0);
            const double w1 = AnimeVectorLogic::displayStrokeWidth(docW, zoom);
            const double w2 = AnimeVectorLogic::displayStrokeWidth(docW, zoom, minPx);
            std::fprintf(out, "displayStrokeWidth %d %.17g %.17g\n", i, w1, w2);
        }
    }

    {
        const double strengths[5] = {0.0, 0.25, 0.5, 0.75, 1.0};
        for (int i = 0; i < 70; ++i) {
            const GeneratedStrokeCase &sc = allCases[static_cast<std::size_t>(i * 21) % allCases.size()];
            AnimeOneEuroFilter filter;
            filter.configure(strengths[i % 5]);
            filter.reset();
            std::fprintf(out, "OneEuroFilter %d active %d count %d",
                         i, filter.active() ? 1 : 0, static_cast<int>(sc.points.size()));
            std::uint64_t ts = 1000;
            for (int p = 0; p < sc.points.size(); ++p) {
                const GPointF outPt = filter.filter(sc.points[p], ts);
                writePointF(out, outPt);
                ts += (p % 7 == 0) ? 0 : static_cast<std::uint64_t>(8 + (p % 16));
            }
            std::fprintf(out, "\n");
        }
    }

    // -----------------------------------------------------------------------
    // 4. Core stroke fitting & path creation across all 1575 strokes (7x225)
    // -----------------------------------------------------------------------
    for (std::size_t idx = 0; idx < allCases.size(); ++idx) {
        GeneratedStrokeCase &sc = allCases[idx];
        const char *catStr = categoryName(sc.category);

        // filteredPoints
        const GVector<GPointF> fp = AnimeVectorLogic::filteredPoints(sc.points);
        std::fprintf(out, "filteredPoints_%s %d count %d",
                     catStr, sc.indexInCat, static_cast<int>(fp.size()));
        for (int k = 0; k < fp.size(); ++k) {
            writePointF(out, fp[k]);
        }
        std::fprintf(out, "\n");

        // makeSmoothedPath
        const int smoothVal = sc.settings.simplify;
        const GPath smoothed = AnimeVectorLogic::makeSmoothedPath(sc.points, smoothVal);
        std::fprintf(out, "makeSmoothedPath_%s %d", catStr, sc.indexInCat);
        writePathInline(out, smoothed);
        std::fprintf(out, "\n");

        // makePolylinePath
        const GPath polyPath = AnimeVectorLogic::makePolylinePath(sc.points);
        std::fprintf(out, "makePolylinePath_%s %d", catStr, sc.indexInCat);
        writePathInline(out, polyPath);
        std::fprintf(out, "\n");

        // fitStrokePath
        const GPath fitted = AnimeVectorLogic::fitStrokePath(sc.points, sc.settings);
        std::fprintf(out, "fitStrokePath_%s %d s=%d c=%d ps=%.17g",
                     catStr, sc.indexInCat, sc.settings.simplify, sc.settings.corner, sc.settings.pixelScale);
        writePathInline(out, fitted);
        std::fprintf(out, "\n");

        // liveFitStrokePath: feed point by point with finalTail=false, then finalTail=true
        {
            AnimeLiveFitState state;
            GVector<GPointF> stream;
            stream.reserve(sc.points.size());
            GPath midPath;
            const int midIdx = sc.points.size() / 2;
            for (int p = 0; p < sc.points.size(); ++p) {
                stream.append(sc.points[p]);
                const GPath stepPath = AnimeVectorLogic::liveFitStrokePath(state, stream, sc.settings, false);
                if (p == midIdx) {
                    midPath = stepPath;
                }
            }
            const GPath finalLivePath = AnimeVectorLogic::liveFitStrokePath(state, stream, sc.settings, true);
            std::fprintf(out, "liveFitStrokePath_mid_%s %d", catStr, sc.indexInCat);
            writePathInline(out, midPath);
            std::fprintf(out, "\n");
            std::fprintf(out, "liveFitStrokePath_final_%s %d", catStr, sc.indexInCat);
            writePathInline(out, finalLivePath);
            std::fprintf(out, "\n");
        }

        // makeStroke
        const GColor col((sc.globalIndex * 37) & 255,
                         (sc.globalIndex * 73) & 255,
                         (sc.globalIndex * 109) & 255,
                         255);
        const double width = 1.5 + (sc.globalIndex % 7) * 0.75;
        const bool filterIn = (sc.indexInCat % 4 != 0);
        const bool smoothIn = (sc.indexInCat % 5 != 0);
        sc.stroke = AnimeVectorLogic::makeStroke(
            sc.points, col, width, sc.globalIndex + 1, filterIn, smoothIn, sc.settings);
        std::fprintf(out, "makeStroke_%s %d", catStr, sc.indexInCat);
        writeStrokeInline(out, sc.stroke);
        std::fprintf(out, "\n");

        // makeStrokeFromPath (for a subset or all)
        if (sc.indexInCat % 3 == 0) {
            const AnimeVectorStroke sFromPath = AnimeVectorLogic::makeStrokeFromPath(
                fitted, fp, col, width, sc.globalIndex + 10000);
            std::fprintf(out, "makeStrokeFromPath_%s %d", catStr, sc.indexInCat);
            writeStrokeInline(out, sFromPath);
            std::fprintf(out, "\n");
        }

        // Path::percentAtLength and Path::pointAtPercent (relaxed 1e-12 comparison allowed)
        if (sc.indexInCat % 3 == 0 && fitted.elementCount() >= 2) {
            const double totalL = fitted.length();
            std::fprintf(out, "percentAtLength %d", sc.globalIndex);
            for (int q = 0; q <= 4; ++q) {
                const double queryLen = totalL * (0.25 * q);
                std::fprintf(out, " %.17g", fitted.percentAtLength(queryLen));
            }
            std::fprintf(out, "\n");

            std::fprintf(out, "pointAtPercent %d", sc.globalIndex);
            for (int q = 0; q <= 4; ++q) {
                const double t = 0.25 * q;
                const GPointF pt = fitted.pointAtPercent(t);
                writePointF(out, pt);
            }
            std::fprintf(out, "\n");
        }
    }

    // -----------------------------------------------------------------------
    // 5. Hit testing, brush distance, erase/keep ranges, crossings, cuts, splits
    // -----------------------------------------------------------------------
    {
        Rng opRng(0xCAFEBABE1234ULL);
        for (std::size_t idx = 0; idx < allCases.size(); ++idx) {
            const GeneratedStrokeCase &sc = allCases[idx];
            const AnimeVectorStroke &stroke = sc.stroke;
            const GRectF b = stroke.bounds;
            const GPointF center(
                b.x() + b.width() * opRng.uniform(0.1, 0.9),
                b.y() + b.height() * opRng.uniform(0.1, 0.9));
            const GPointF capFrom(
                b.x() + b.width() * opRng.uniform(-0.1, 1.1),
                b.y() + b.height() * opRng.uniform(-0.1, 1.1));
            const GPointF capTo = (idx % 10 == 0)
                ? capFrom
                : GPointF(b.x() + b.width() * opRng.uniform(-0.1, 1.1),
                          b.y() + b.height() * opRng.uniform(-0.1, 1.1));
            const double radius = opRng.uniform(2.0, 35.0);

            const bool hitCircle = AnimeVectorLogic::strokeHitsCircle(stroke, center, radius);
            const bool hitCapsule = AnimeVectorLogic::strokeHitsCapsule(stroke, capFrom, capTo, radius);
            const double distSeg = AnimeVectorLogic::strokeDistanceToBrush(stroke, capFrom, capTo);
            const double distPt = AnimeVectorLogic::strokeDistanceToBrush(stroke, center, center);

            std::fprintf(out, "strokeHitsAndDist %d %d %d %.17g %.17g\n",
                         sc.globalIndex, hitCircle ? 1 : 0, hitCapsule ? 1 : 0, distSeg, distPt);

            const GVector<AnimeVectorRange> keepCircle =
                AnimeVectorLogic::keepRangesForCircle(stroke, center, radius);
            const GVector<AnimeVectorRange> keepCapsule =
                AnimeVectorLogic::keepRangesForCapsule(stroke, capFrom, capTo, radius);
            const GVector<AnimeVectorRange> compRanges =
                AnimeVectorLogic::complementRanges(keepCircle);

            std::fprintf(out, "keepRangesForCircle %d", sc.globalIndex);
            writeRangesInline(out, keepCircle);
            std::fprintf(out, "\n");

            std::fprintf(out, "keepRangesForCapsule %d", sc.globalIndex);
            writeRangesInline(out, keepCapsule);
            std::fprintf(out, "\n");

            std::fprintf(out, "complementRanges %d", sc.globalIndex);
            writeRangesInline(out, compRanges);
            std::fprintf(out, "\n");

            // pointAtLength, subStroke, splitStrokeAt, snapStrokeEndpoint
            const double pLen = stroke.totalLength * opRng.uniform(0.0, 1.0);
            const GPointF ptAtLen = AnimeVectorLogic::pointAtLength(stroke, pLen);
            std::fprintf(out, "pointAtLength %d %.17g %.17g\n",
                         sc.globalIndex, ptAtLen.x(), ptAtLen.y());

            if (idx % 2 == 0) {
                const double w0 = opRng.uniform(0.05, 0.45);
                const double w1 = opRng.uniform(0.55, 0.95);
                const AnimeVectorStroke sub =
                    AnimeVectorLogic::subStroke(stroke, w0, w1, sc.settings);
                std::fprintf(out, "subStroke %d", sc.globalIndex);
                writeStrokeInline(out, sub);
                std::fprintf(out, "\n");
            }

            if (idx % 3 == 0) {
                GVector<double> arcs;
                arcs.append(0.0);
                arcs.append(opRng.uniform(0.15, 0.45));
                arcs.append(opRng.uniform(0.55, 0.85));
                arcs.append(1.0);
                GVector<double> usedArcs;
                const GVector<AnimeVectorStroke> pieces =
                    AnimeVectorLogic::splitStrokeAt(stroke, arcs, sc.settings, 1e-3, &usedArcs);
                std::fprintf(out, "splitStrokeAt %d used %d",
                             sc.globalIndex, static_cast<int>(usedArcs.size()));
                for (int u = 0; u < usedArcs.size(); ++u) {
                    std::fprintf(out, " %.17g", usedArcs[u]);
                }
                std::fprintf(out, " pieces %d", static_cast<int>(pieces.size()));
                for (int p = 0; p < pieces.size(); ++p) {
                    writeStrokeInline(out, pieces[p]);
                }
                std::fprintf(out, "\n");
            }

            if (idx % 3 == 1) {
                const bool atEnd = (idx % 2 == 0);
                const GPointF snapTarget = center;
                const AnimeVectorStroke snapped =
                    AnimeVectorLogic::snapStrokeEndpoint(stroke, atEnd, snapTarget);
                std::fprintf(out, "snapStrokeEndpoint %d", sc.globalIndex);
                writeStrokeInline(out, snapped);
                std::fprintf(out, "\n");
            }

            if (idx % 3 == 2) {
                const GVector<GLineF> segs = AnimeVectorLogic::segmentsFromPath(stroke.path);
                std::fprintf(out, "segmentsFromPath %d count %d",
                             sc.globalIndex, static_cast<int>(segs.size()));
                for (int s = 0; s < segs.size(); ++s) {
                    std::fprintf(out, " %.17g %.17g %.17g %.17g",
                                 segs[s].x1(), segs[s].y1(), segs[s].x2(), segs[s].y2());
                }
                std::fprintf(out, "\n");
            }
        }

        // Crossings, planCutAt, cutSpanAt on intersecting wall configurations + self-intersecting strokes
        int crossCaseIdx = 0;
        for (int i = 0; i < 200; ++i) {
            const GeneratedStrokeCase &baseCase =
                allCases[static_cast<std::size_t>((CatSelfIntersect * kStrokesPerCategory) + (i % kStrokesPerCategory))];
            AnimeVectorStroke target = baseCase.stroke;
            if (target.points.size() < 2) {
                continue;
            }
            const GRectF tb = target.bounds;
            const double cx = tb.x() + tb.width() * 0.5;
            const double cy = tb.y() + tb.height() * 0.5;

            GVector<AnimeVectorStroke> walls;
            {
                GVector<GPointF> w1Pts;
                w1Pts.append(GPointF(tb.left() - 20.0, cy - 15.0));
                w1Pts.append(GPointF(cx, cy));
                w1Pts.append(GPointF(tb.right() + 20.0, cy + 15.0));
                walls.append(AnimeVectorLogic::makeStroke(w1Pts, GColor(0, 0, 0), 3.0, 500 + i * 2));

                GVector<GPointF> w2Pts;
                w2Pts.append(GPointF(cx - 10.0, tb.top() - 20.0));
                w2Pts.append(GPointF(cx, cy));
                w2Pts.append(GPointF(cx + 10.0, tb.bottom() + 20.0));
                walls.append(AnimeVectorLogic::makeStroke(w2Pts, GColor(0, 0, 0), 3.0, 501 + i * 2));

                // Also a T-junction wall whose endpoint rests on a target vertex
                if (target.points.size() >= 4) {
                    const GPointF anchor = target.points[target.points.size() / 2];
                    GVector<GPointF> w3Pts;
                    w3Pts.append(anchor);
                    w3Pts.append(GPointF(anchor.x() + 40.0, anchor.y() - 35.0));
                    walls.append(AnimeVectorLogic::makeStroke(w3Pts, GColor(0, 0, 0), 3.0, 700 + i));
                }
            }

            const GVector<AnimeStrokeCrossing> crossings =
                AnimeVectorLogic::strokeCrossings(target, walls);
            std::fprintf(out, "strokeCrossings %d count %d",
                         crossCaseIdx, static_cast<int>(crossings.size()));
            for (int c = 0; c < crossings.size(); ++c) {
                std::fprintf(out, " %.17g %d %.17g %d",
                             crossings[c].arc,
                             crossings[c].wallIndex,
                             crossings[c].wallArc,
                             crossings[c].isJunction() ? 1 : 0);
            }
            std::fprintf(out, "\n");

            const GPointF clickPos = AnimeVectorLogic::pointAtLength(
                target, target.totalLength * opRng.uniform(0.1, 0.9));
            AnimeCutPlan plan;
            const bool hasPlan = AnimeVectorLogic::planCutAt(target, walls, clickPos, &plan);
            std::fprintf(out, "planCutAt %d ok %d span %.17g %.17g low %.17g %d %.17g high %.17g %d %.17g\n",
                         crossCaseIdx, hasPlan ? 1 : 0,
                         plan.span.first, plan.span.second,
                         plan.low.arc, plan.low.wallIndex, plan.low.wallArc,
                         plan.high.arc, plan.high.wallIndex, plan.high.wallArc);

            AnimeVectorRange span;
            const bool hasSpan = AnimeVectorLogic::cutSpanAt(target, walls, clickPos, &span);
            std::fprintf(out, "cutSpanAt %d ok %d span %.17g %.17g\n",
                         crossCaseIdx, hasSpan ? 1 : 0, span.first, span.second);
            ++crossCaseIdx;
        }
    }

    // -----------------------------------------------------------------------
    // 6. AnimeBezierSplit public functions
    // -----------------------------------------------------------------------
    {
        Rng bzRng(0xB321E800ULL);
        for (int i = 0; i < 120; ++i) {
            GPointF p[4] = {
                GPointF(bzRng.uniform(0.0, 500.0), bzRng.uniform(0.0, 500.0)),
                GPointF(bzRng.uniform(0.0, 500.0), bzRng.uniform(0.0, 500.0)),
                GPointF(bzRng.uniform(0.0, 500.0), bzRng.uniform(0.0, 500.0)),
                GPointF(bzRng.uniform(0.0, 500.0), bzRng.uniform(0.0, 500.0))
            };
            const double t = bzRng.uniform(0.05, 0.95);
            const double t0 = bzRng.uniform(0.0, 0.5);
            const double t1 = bzRng.uniform(0.5, 1.0);

            GPointF left[4], right[4], seg[4], lineCub[4], quadCub[4];
            const GPointF splitPt = AnimeBezierSplit::splitCubic(p, t, left, right);
            const GPointF splitPtNoOut = AnimeBezierSplit::splitCubic(p, t);
            AnimeBezierSplit::cubicSegment(p, t0, t1, seg);
            const GPointF evalPt = AnimeBezierSplit::evaluateCubic(p, t);
            const GPointF derivPt = AnimeBezierSplit::cubicDerivative(p, t);
            AnimeBezierSplit::lineToCubic(p[0], p[3], lineCub);
            AnimeBezierSplit::quadToCubic(p[0], p[1], p[3], quadCub);
            const double hullLen = AnimeBezierSplit::cubicHullLength(p);

            AnimeBezierCurve cCubic;
            cCubic.isLine = false;
            for (int k = 0; k < 4; ++k) cCubic.p[k] = p[k];
            AnimeBezierCurve cLine;
            cLine.isLine = true;
            cLine.p[0] = p[0];
            cLine.p[3] = p[3];
            const GPointF cpCubic = AnimeBezierSplit::curvePoint(cCubic, t);
            const GPointF cpLine = AnimeBezierSplit::curvePoint(cLine, t);

            std::fprintf(out, "beziersplit_primitives %d", i);
            writePointF(out, splitPt);
            writePointF(out, splitPtNoOut);
            for (int k = 0; k < 4; ++k) writePointF(out, left[k]);
            for (int k = 0; k < 4; ++k) writePointF(out, right[k]);
            for (int k = 0; k < 4; ++k) writePointF(out, seg[k]);
            writePointF(out, evalPt);
            writePointF(out, derivPt);
            for (int k = 0; k < 4; ++k) writePointF(out, lineCub[k]);
            for (int k = 0; k < 4; ++k) writePointF(out, quadCub[k]);
            std::fprintf(out, " %.17g", hullLen);
            writePointF(out, cpCubic);
            writePointF(out, cpLine);
            std::fprintf(out, "\n");
        }

        // parsePath, parseSubpaths, buildRun, locate, slice across fitted stroke paths
        for (int i = 0; i < 140; ++i) {
            const GeneratedStrokeCase &sc = allCases[static_cast<std::size_t>(i * 11) % allCases.size()];
            GPath testPath = sc.stroke.path;
            if (i % 15 == 14) {
                // Multi-subpath case for parsePath vs parseSubpaths
                testPath.moveTo(GPointF(10.0 + i, 20.0));
                testPath.lineTo(GPointF(50.0 + i, 80.0));
            }

            GVector<AnimeBezierCurve> parsed;
            const bool okParse = AnimeBezierSplit::parsePath(testPath, &parsed);
            std::fprintf(out, "beziersplit_parsePath %d ok %d count %d",
                         i, okParse ? 1 : 0, static_cast<int>(parsed.size()));
            for (int c = 0; c < parsed.size(); ++c) {
                std::fprintf(out, " %d", parsed[c].isLine ? 1 : 0);
                for (int k = 0; k < 4; ++k) writePointF(out, parsed[c].p[k]);
            }
            std::fprintf(out, "\n");

            GVector<GVector<AnimeBezierCurve>> subpaths;
            const bool okSub = AnimeBezierSplit::parseSubpaths(testPath, &subpaths);
            std::fprintf(out, "beziersplit_parseSubpaths %d ok %d subs %d",
                         i, okSub ? 1 : 0, static_cast<int>(subpaths.size()));
            for (int s = 0; s < subpaths.size(); ++s) {
                std::fprintf(out, " c %d", static_cast<int>(subpaths[s].size()));
                for (int c = 0; c < subpaths[s].size(); ++c) {
                    std::fprintf(out, " %d", subpaths[s][c].isLine ? 1 : 0);
                    writePointF(out, subpaths[s][c].p[0]);
                    writePointF(out, subpaths[s][c].p[3]);
                }
            }
            std::fprintf(out, "\n");

            AnimeBezierSplitOptions opts;
            if (i % 3 == 1) {
                opts.targetSpacing = 2.0;
                opts.refineSteps = 12;
                opts.refineRounds = 2;
            }
            const AnimeBezierPathRun run = AnimeBezierSplit::buildRun(sc.stroke.path, opts);
            std::fprintf(out, "beziersplit_buildRun %d valid %d curves %d samples %d totalLen %.17g\n",
                         i, run.isValid() ? 1 : 0,
                         static_cast<int>(run.curves.size()),
                         static_cast<int>(run.samples.size()),
                         run.totalLength());

            if (run.isValid()) {
                const double w0 = 0.2 + 0.05 * (i % 5);
                const double w1 = 0.65 + 0.05 * (i % 5);
                const AnimeBezierPathPosition pos0 = AnimeBezierSplit::locate(run, w0, opts);
                const GPointF nearAnchor = AnimeVectorLogic::pointAtLength(
                    sc.stroke, sc.stroke.totalLength * w1);
                const AnimeBezierPathPosition pos1 = AnimeBezierSplit::locate(run, w1, nearAnchor, opts);

                std::fprintf(out, "beziersplit_locate %d c0 %d t0 %.17g p0 %.17g %.17g c1 %d t1 %.17g p1 %.17g %.17g\n",
                             i,
                             pos0.curve, pos0.t, pos0.point.x(), pos0.point.y(),
                             pos1.curve, pos1.t, pos1.point.x(), pos1.point.y());

                GPath slicedPath;
                GVector<GPointF> slicedPoly;
                const bool okSlice = AnimeBezierSplit::slice(run, pos0, pos1, &slicedPath, &slicedPoly);
                std::fprintf(out, "beziersplit_slice %d ok %d poly %d",
                             i, okSlice ? 1 : 0, static_cast<int>(slicedPoly.size()));
                for (int k = 0; k < slicedPoly.size(); ++k) {
                    writePointF(out, slicedPoly[k]);
                }
                writePathInline(out, slicedPath);
                std::fprintf(out, "\n");
            }
        }
    }

    // -----------------------------------------------------------------------
    // 7. Boolean-op functions: computeVectorRegionFaces, vectorRegionPathAt, fillPathFromMask
    // -----------------------------------------------------------------------
    {
        Rng regRng(0x778899AABBULL);
        int faceCaseIdx = 0;
        int regionAtIdx = 0;

        for (int i = 0; i < 60; ++i) {
            GVector<GLineF> segments;
            const double cx = 200.0 + (i % 5) * 30.0;
            const double cy = 200.0 + ((i / 5) % 5) * 25.0;
            const double rOuter = 60.0 + (i % 7) * 10.0;

            // Outer closed polygon (6..10 sides)
            const int nSides = 6 + (i % 5);
            GVector<GPointF> outerPts;
            for (int k = 0; k < nSides; ++k) {
                const double a = (2.0 * kPi * k) / nSides;
                const double rad = rOuter * (1.0 + ((k % 2 == 0) ? 0.12 : -0.08));
                outerPts.append(GPointF(cx + rad * std::cos(a), cy + rad * std::sin(a)));
            }
            for (int k = 0; k < nSides; ++k) {
                GPointF p2 = outerPts[(k + 1) % nSides];
                // Introduce tiny dangling gap on every 4th case to exercise healRadius > 0
                if (i % 4 == 3 && k == 0) {
                    p2 = GPointF(p2.x() + 0.5, p2.y() - 0.4);
                }
                segments.append(GLineF(outerPts[k], p2));
            }

            // Optional inner hole or cross-dividers
            if (i % 3 == 0) {
                // Disjoint inner hole loop around (cx + 18, cy) so seed at (cx - 22, cy) is outside hole
                const double hx = cx + 18.0;
                const double hy = cy;
                const double hr = 14.0;
                const GPointF h0(hx - hr, hy - hr);
                const GPointF h1(hx + hr, hy - hr);
                const GPointF h2(hx + hr, hy + hr);
                const GPointF h3(hx - hr, hy + hr);
                segments.append(GLineF(h0, h1));
                segments.append(GLineF(h1, h2));
                segments.append(GLineF(h2, h3));
                segments.append(GLineF(h3, h0));
            } else if (i % 3 == 1) {
                // Chord splitting the polygon into 2 faces
                segments.append(GLineF(GPointF(cx - rOuter * 1.2, cy),
                                       GPointF(cx + rOuter * 1.2, cy)));
            }

            const double healR = (i % 4 == 3) ? 2.5 : 0.0;
            const GVector<AnimeVectorRegionFace> faces =
                AnimeVectorLogic::computeVectorRegionFaces(segments, healR);
            std::fprintf(out, "computeVectorRegionFaces_count %d %d\n",
                         faceCaseIdx, static_cast<int>(faces.size()));
            for (int f = 0; f < faces.size(); ++f) {
                std::fprintf(out, "computeVectorRegionFaces_area %d_%d %.17g\n",
                             faceCaseIdx, f, faces[f].signedArea);
                std::fprintf(out, "computeVectorRegionFaces %d_%d", faceCaseIdx, f);
                writePathInline(out, faces[f].path);
                std::fprintf(out, "\n");
            }
            ++faceCaseIdx;

            const GRect canvasRect(0, 0, 600, 600);
            const GPointF seedInside(cx - 20.0, cy - 10.0);
            const GPath regionPath =
                AnimeVectorLogic::vectorRegionPathAt(seedInside, segments, canvasRect);
            std::fprintf(out, "vectorRegionPathAt %d", regionAtIdx++);
            writePathInline(out, regionPath);
            std::fprintf(out, "\n");

            if (i % 5 == 0) {
                // Outside seed -> empty path
                const GPath emptyReg =
                    AnimeVectorLogic::vectorRegionPathAt(GPointF(5.0, 5.0), segments, canvasRect);
                std::fprintf(out, "vectorRegionPathAt %d", regionAtIdx++);
                writePathInline(out, emptyReg);
                std::fprintf(out, "\n");
            }
        }

        // fillPathFromMask cases
        for (int i = 0; i < 40; ++i) {
            const int w = 64 + (i % 4) * 16;
            const int h = 64 + ((i / 4) % 4) * 16;
            GImage mask(w, h, GImage::Format_Grayscale8);
            mask.fill(0);

            const int x0 = 8 + (i % 7);
            const int y0 = 8 + (i % 5);
            const int x1 = w - 9 - (i % 6);
            const int y1 = h - 9 - (i % 5);

            // Draw closed rectangular boundary
            for (int x = x0; x <= x1; ++x) {
                mask.setPixel(x, y0, 255);
                mask.setPixel(x, y1, 255);
            }
            for (int y = y0; y <= y1; ++y) {
                mask.setPixel(x0, y, 255);
                mask.setPixel(x1, y, 255);
            }

            // Add internal island obstacle
            if (i % 2 == 0) {
                const int ix0 = (x0 + x1) / 2 - 4;
                const int ix1 = (x0 + x1) / 2 + 4;
                const int iy0 = (y0 + y1) / 2 - 4;
                const int iy1 = (y0 + y1) / 2 + 4;
                for (int y = iy0; y <= iy1; ++y) {
                    for (int x = ix0; x <= ix1; ++x) {
                        mask.setPixel(x, y, 255);
                    }
                }
            }

            const GPoint seed = (i % 10 == 9)
                ? GPoint(2, 2) // leaks to canvas edge -> returns empty Path
                : GPoint(x0 + 3, y0 + 3);
            const GPath maskPath = AnimeVectorLogic::fillPathFromMask(seed, mask);
            std::fprintf(out, "fillPathFromMask %d", i);
            writePathInline(out, maskPath);
            std::fprintf(out, "\n");
        }
    }

    // -----------------------------------------------------------------------
    // 8. AnimeSceneModel operations, ProjectIO JSON round-trip, SceneHistory
    // -----------------------------------------------------------------------
    {
        Rng modelRng(0x900DF00DULL);
        AnimeSceneModel mainModel;
        AnimeSceneModel texModel;
        SceneHistory history;

        int stepIdx = 0;
        auto recordStep = [&](const char *label) {
            dumpModelStep(out, label, stepIdx++, mainModel);
            history.commit(fromUtf8Str(label), mainModel);
        };

        mainModel.initializeScene(2, 3);
        mainModel.setId(fromUtf8Str("Scene_Alpha"));
        mainModel.setIntId(42);
        mainModel.setCanvasSize(GSize(1920, 1080));
        mainModel.setPlaybackFps(24);
        mainModel.setScriptData(fromUtf8Str("{\"tool\":\"automapping\",\"v\":2}"));
        history.reset(fromUtf8Str("init"), mainModel);
        dumpModelStep(out, "init", stepIdx++, mainModel);

        // Add layers & fill layers
        mainModel.addLayer();
        recordStep("add_layer_1");

        mainModel.addFillLayer();
        recordStep("add_fill_layer");

        mainModel.addLayer();
        recordStep("add_layer_2");

        // Rename layers & test uniqueLayerName
        mainModel.setLayerName(0, fromUtf8Str("LineArt"));
        mainModel.setLayerName(1, fromUtf8Str("Shadow"));
        mainModel.setLayerName(2, fromUtf8Str("Tone"));
        const GString uniqName = mainModel.uniqueLayerName(fromUtf8Str("LineArt"));
        mainModel.setLayerName(4, uniqName);
        recordStep("rename_layers");

        // Layer properties & parenting
        mainModel.setLayerOpacity(1, 0.75);
        mainModel.setLayerVisible(2, false);
        mainModel.setLayerVisible(2, true);
        mainModel.setLayerLocked(1, true);
        mainModel.setLayerLocked(1, false);
        const int lineLayerId = mainModel.layerIdAt(0);
        mainModel.setLayerParentId(3, lineLayerId);
        recordStep("layer_props_and_parent");

        // Add frames, hold frames, duplicate frames, rename frames
        mainModel.addFrame();
        recordStep("add_frame");

        // Add strokes to several cells before hold/duplicate so exposures carry real drawings
        for (int f = 0; f < mainModel.frameCount(); ++f) {
            for (int l = 0; l < 3; ++l) {
                if (mainModel.isFillLayer(l)) {
                    continue;
                }
                AnimeVectorImageModel *img = mainModel.imageAt(f, l, true);
                const int strokeCountToAdd = 2 + ((f + l) % 3);
                for (int s = 0; s < strokeCountToAdd; ++s) {
                    const std::size_t caseIdx =
                        static_cast<std::size_t>((f * 17 + l * 31 + s * 7)) % allCases.size();
                    AnimeVectorStroke st = allCases[caseIdx].stroke;
                    st.id = (f + 1) * 100 + l * 10 + s;
                    st.property = (s % 2 == 0) ? fromUtf8Str("key") : fromUtf8Str("inbetween");
                    if (s == 1) {
                        st.penStyle = 2;
                    }
                    img->addStroke(st);
                }
            }
        }
        recordStep("add_initial_strokes");

        // Frame naming and nextDuplicateName
        mainModel.setFrameName(0, fromUtf8Str("KEY-1"));
        mainModel.setFrameName(1, fromUtf8Str("IB-2"));
        recordStep("set_frame_names");

        const int holdRow = mainModel.insertHoldFrameAfter(0);
        std::fprintf(out, "model_op_ret insertHoldFrameAfter %d\n", holdRow);
        recordStep("insert_hold_frame");

        const GString dupName0 = mainModel.nextDuplicateName(0);
        const int dupRow = mainModel.duplicateFrame(0);
        if (dupRow >= 0) {
            mainModel.setFrameName(dupRow, dupName0);
        }
        std::fprintf(out, "model_op_ret duplicateFrame %d %s\n", dupRow, toStdStr(dupName0).c_str());
        recordStep("duplicate_frame_0");

        const GString dupNameAgain = mainModel.nextDuplicateName(0);
        const int dupRow2 = mainModel.duplicateFrame(dupRow);
        if (dupRow2 >= 0) {
            mainModel.setFrameName(dupRow2, dupNameAgain);
        }
        std::fprintf(out, "model_op_ret duplicateFrame2 %d %s\n", dupRow2, toStdStr(dupNameAgain).c_str());
        recordStep("duplicate_frame_again");

        // Move frame & delete frame
        mainModel.moveFrame(1, 3);
        recordStep("move_frame_1_to_3");

        mainModel.deleteFrame(2);
        recordStep("delete_frame_2");

        // Layer grouping operations
        GVector<int> grpLayers;
        grpLayers.append(0);
        grpLayers.append(1);
        const int grp1 = mainModel.createLayerGroup(fromUtf8Str("CharA"), grpLayers, GVector<int>(), false);
        mainModel.setLayerGroupTag(grp1, fromUtf8Str("automapping"));
        mainModel.setLayerGroupCollapsed(grp1, true);
        std::fprintf(out, "model_op_ret createLayerGroup %d tag %s collapsed %d\n",
                     grp1,
                     toStdStr(mainModel.layerGroupTag(grp1)).c_str(),
                     mainModel.layerGroupCollapsed(grp1) ? 1 : 0);
        recordStep("create_group_1");

        GVector<int> addMoreLayers;
        addMoreLayers.append(2);
        const int movedToGrp = mainModel.addLayersToGroup(grp1, addMoreLayers);
        mainModel.setLayerGroupName(grp1, fromUtf8Str("CharA_Full"));
        std::fprintf(out, "model_op_ret addLayersToGroup %d\n", movedToGrp);
        recordStep("add_to_group_and_rename");

        // Move layer & add another group then dissolve it
        mainModel.moveLayer(0, 2);
        recordStep("move_layer_0_to_2");

        GVector<int> grp2Layers;
        grp2Layers.append(4);
        const int grp2 = mainModel.createLayerGroup(fromUtf8Str("TempGroup"), grp2Layers, GVector<int>(), false);
        recordStep("create_group_2");

        mainModel.dissolveLayerGroup(grp2);
        recordStep("dissolve_group_2");

        // Translate and transform strokes on frames
        if (AnimeVectorImageModel *img0 = mainModel.imageAt(0, 0, false)) {
            img0->translate(GPointF(12.5, -8.25));
        }
        recordStep("translate_image_0_0");

        if (AnimeVectorImageModel *img1 = mainModel.imageAt(1, 0, false)) {
            GTransform xf;
            xf.translate(50.0, 30.0);
            xf.rotate(15.0);
            xf.scale(1.25, 0.85);
            img1->transform(xf);
        }
        recordStep("transform_image_1_0");

        // Also populate textureModel with a couple of strokes
        texModel.initializeScene(1, 2);
        texModel.setId(fromUtf8Str("Tex_Scene"));
        if (AnimeVectorImageModel *tImg = texModel.imageAt(0, 0, true)) {
            tImg->addStroke(allCases[0].stroke);
            tImg->addStroke(allCases[75].stroke);
        }

        // ProjectIO filter / extension helpers
        std::fprintf(out, "projectio_helpers 0 %s | %s | %s | %s | %s | %s\n",
                     toStdStr(projectOpenFilter()).c_str(),
                     toStdStr(projectSaveFilter()).c_str(),
                     toStdStr(textureViewOpenFilter()).c_str(),
                     toStdStr(textureViewSaveFilter()).c_str(),
                     toStdStr(ensureProjectFileExtension(fromUtf8Str("drawing.textureview"))).c_str(),
                     toStdStr(ensureTextureViewFileExtension(fromUtf8Str("drawing.anproj"))).c_str());

        // projectToJson -> Compact JSON bytes
        const GJsonObject projJson1 = projectToJson(mainModel, texModel);
        const std::string jsonBytes1 = jsonToCompactBytes(projJson1);
        std::fprintf(out, "projectToJson_bytes 0 %s\n", jsonBytes1.c_str());

        // projectFromJson -> round-trip back to models -> projectToJson -> Compact JSON bytes again
        AnimeSceneModel loadedMain;
        AnimeSceneModel loadedTex;
        GString loadErr;
        const GJsonObject parsedObj = jsonFromBytes(jsonBytes1);
        const bool loadOk = projectFromJson(parsedObj, &loadedMain, &loadedTex, &loadErr);
        const GJsonObject projJson2 = projectToJson(loadedMain, loadedTex);
        const std::string jsonBytes2 = jsonToCompactBytes(projJson2);
        std::fprintf(out, "projectFromJson_status 0 ok %d sameBytes %d err %s\n",
                     loadOk ? 1 : 0,
                     (jsonBytes1 == jsonBytes2) ? 1 : 0,
                     toStdStr(loadErr).empty() ? "-" : toStdStr(loadErr).c_str());
        std::fprintf(out, "projectToJson_bytes 1 %s\n", jsonBytes2.c_str());
        dumpModelStep(out, "loaded_from_json", stepIdx++, loadedMain);

        // textureViewToJson / textureViewFromJson
        const GJsonObject tvJson1 = textureViewToJson(texModel);
        const std::string tvBytes1 = jsonToCompactBytes(tvJson1);
        AnimeSceneModel loadedTv;
        GString tvErr;
        const bool tvOk = textureViewFromJson(jsonFromBytes(tvBytes1), &loadedTv, &tvErr);
        const std::string tvBytes2 = jsonToCompactBytes(textureViewToJson(loadedTv));
        std::fprintf(out, "textureViewJson_bytes 0 ok %d same %d %s\n",
                     tvOk ? 1 : 0, (tvBytes1 == tvBytes2) ? 1 : 0, tvBytes1.c_str());

        // SceneHistory undo / redo walk
        int histWalkIdx = 0;
        while (history.canUndo()) {
            const bool uOk = history.undo(&mainModel);
            std::fprintf(out, "history_undo %d ok %d idx %d count %d canU %d canR %d seq %llu rseq %llu label %s\n",
                         histWalkIdx,
                         uOk ? 1 : 0,
                         history.currentIndex(),
                         history.count(),
                         history.canUndo() ? 1 : 0,
                         history.canRedo() ? 1 : 0,
                         static_cast<unsigned long long>(history.currentSeq()),
                         static_cast<unsigned long long>(history.redoSeq()),
                         toStdStr(history.labelAt(history.currentIndex())).c_str());
            dumpModelStep(out, "after_undo", 1000 + histWalkIdx, mainModel);
            ++histWalkIdx;
        }

        while (history.canRedo()) {
            const bool rOk = history.redo(&mainModel);
            std::fprintf(out, "history_redo %d ok %d idx %d count %d canU %d canR %d seq %llu rseq %llu label %s\n",
                         histWalkIdx,
                         rOk ? 1 : 0,
                         history.currentIndex(),
                         history.count(),
                         history.canUndo() ? 1 : 0,
                         history.canRedo() ? 1 : 0,
                         static_cast<unsigned long long>(history.currentSeq()),
                         static_cast<unsigned long long>(history.redoSeq()),
                         toStdStr(history.labelAt(history.currentIndex())).c_str());
            dumpModelStep(out, "after_redo", 2000 + histWalkIdx, mainModel);
            ++histWalkIdx;
        }

        // goTo and truncateRedo
        const int midHist = history.count() / 2;
        const bool goOk = history.goTo(midHist, &mainModel);
        const bool truncOk = history.truncateRedo();
        std::fprintf(out, "history_goto_truncate 0 goOk %d truncOk %d idx %d count %d canR %d\n",
                     goOk ? 1 : 0, truncOk ? 1 : 0,
                     history.currentIndex(), history.count(), history.canRedo() ? 1 : 0);
        dumpModelStep(out, "after_goto_truncate", 3000, mainModel);
    }

    std::fclose(out);
    return 0;
}
