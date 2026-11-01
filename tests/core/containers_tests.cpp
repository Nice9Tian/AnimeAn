#include "algorithm/core/containers.h"
#include "algorithm/core/geometry.h"
#include "algorithm/core/color.h"
#include "algorithm/core/qtbridge_geometry.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QQueue>
#include <QSet>
#include <QVector>

#include <cstdio>
#include <random>
#include <string>
#include <type_traits>
#include <utility>

using namespace AnimeCore;

static int g_failed = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL [line %d]: ", __LINE__); \
        std::fprintf(stderr, __VA_ARGS__); \
        std::fprintf(stderr, "\n"); \
        g_failed = 1; \
        return; \
    } \
} while (0)

static_assert(std::is_same_v<decltype(std::declval<Vector<int>>().size()), std::ptrdiff_t>,
              "Vector::size() must return std::ptrdiff_t");
static_assert(std::is_same_v<decltype(std::declval<Map<int, int>>().size()), std::ptrdiff_t>,
              "Map::size() must return std::ptrdiff_t");
static_assert(std::is_same_v<decltype(std::declval<Hash<int, int>>().size()), std::ptrdiff_t>,
              "Hash::size() must return std::ptrdiff_t");
static_assert(std::is_same_v<decltype(std::declval<Set<int>>().size()), std::ptrdiff_t>,
              "Set::size() must return std::ptrdiff_t");
static_assert(std::is_same_v<decltype(std::declval<Queue<int>>().size()), std::ptrdiff_t>,
              "Queue::size() must return std::ptrdiff_t");

static_assert(std::is_same_v<decltype(std::declval<Vector<bool> &>()[0]), bool &>,
              "Vector<bool> non-const subscript must return bool&, not a proxy");
static_assert(std::is_same_v<decltype(std::declval<const Vector<bool> &>()[0]), const bool &>,
              "Vector<bool> const subscript must return const bool&");
static_assert(std::is_same_v<decltype(std::declval<Vector<bool> &>().data()), bool *>,
              "Vector<bool>::data() must return bool*");

static constexpr int kNumSamples = 25000;

struct CustomStringKey
{
    std::string text;
    bool operator==(const CustomStringKey &o) const noexcept { return text == o.text; }
};

namespace std {
template <>
struct hash<CustomStringKey>
{
    std::size_t operator()(const CustomStringKey &k) const noexcept
    {
        return std::hash<std::string>{}(k.text);
    }
};
} // namespace std

template <class T>
static bool sameVec(const Vector<T> &cv, const QList<T> &qv)
{
    if (cv.size() != static_cast<std::ptrdiff_t>(qv.size()))
        return false;
    if (cv.isEmpty() != qv.isEmpty())
        return false;
    for (std::ptrdiff_t i = 0; i < cv.size(); ++i) {
        if (!(cv.at(i) == qv.at(i)))
            return false;
    }
    return true;
}

static void testVectorImplicitSharingAndBool()
{
    Vector<int> a = {10, 20, 30, 40, 50};
    Vector<int> b = a;

    CHECK(a.constData() == b.constData(), "Copy must share buffer before write");
    CHECK(std::as_const(b)[2] == 30, "const operator[] value mismatch");
    CHECK(a.constData() == b.constData(), "const operator[] must not detach");
    CHECK(b.at(1) == 20, "at() value mismatch");
    CHECK(a.constData() == b.constData(), "at() must not detach");
    CHECK(b.constFirst() == 10 && b.constLast() == 50, "constFirst/constLast mismatch");
    CHECK(a.constData() == b.constData(), "constFirst/constLast must not detach");
    CHECK(*std::as_const(b).begin() == 10 && *(std::as_const(b).end() - 1) == 50, "const begin/end mismatch");
    CHECK(a.constData() == b.constData(), "const begin/end must not detach");

    int sum = 0;
    for (int x : std::as_const(b))
        sum += x;
    CHECK(sum == 150, "range-for const sum mismatch");
    CHECK(a.constData() == b.constData(), "const range-for must not detach");

    const int *origPtr = a.constData();
    b[2] = 999;
    CHECK(a.constData() == origPtr, "Original constData() pointer changed");
    CHECK(a.constData() != b.constData(), "Non-const operator[] must detach");
    CHECK(a[2] == 30 && b[2] == 999, "COW isolation failed on operator[]");

    Vector<int> c = a;
    CHECK(c.constData() == a.constData(), "Copy c must share with a");
    auto it = c.begin();
    CHECK(c.constData() != a.constData(), "Non-const begin() must detach");
    *it = 777;
    CHECK(a[0] == 10 && c[0] == 777, "COW isolation failed on begin()");

    Vector<int> d = a;
    CHECK(d.constData() == a.constData(), "Copy d must share with a");
    (void)d.end();
    CHECK(d.constData() != a.constData(), "Non-const end() must detach");

    Vector<int> e = a;
    CHECK(e.constData() == a.constData(), "Copy e must share with a");
    int *rawData = e.data();
    CHECK(e.constData() != a.constData(), "Non-const data() must detach");
    rawData[4] = 555;
    CHECK(a[4] == 50 && e[4] == 555, "COW isolation failed on data()");

    Vector<bool> vb = {true, false, true, false};
    Vector<bool> vbCopy = vb;
    CHECK(vb.constData() == vbCopy.constData(), "Vector<bool> copy must share buffer");
    bool &ref = vbCopy[1];
    ref = true;
    CHECK(vb.constData() != vbCopy.constData(), "Vector<bool> non-const subscript must detach");
    CHECK(vb[1] == false && vbCopy[1] == true, "Vector<bool> COW mutation failed");
    bool *boolPtr = &vbCopy[0];
    *boolPtr = false;
    CHECK(vbCopy[0] == false && vb[0] == true, "Vector<bool> direct pointer write failed");
}

static void testVectorDifferential()
{
    std::mt19937_64 rng(0xA001ULL);
    Vector<int> cv;
    QList<int> qv;

    for (int i = 0; i < kNumSamples; ++i) {
        const int op = static_cast<int>(rng() % 18);
        const int val = static_cast<int>(rng() % 200) - 100;

        switch (op) {
        case 0:
        case 1:
        case 2:
            cv.append(val);
            qv.append(val);
            break;
        case 3:
            cv.prepend(val);
            qv.prepend(val);
            break;
        case 4:
            cv.push_back(val);
            qv.push_back(val);
            break;
        case 5: {
            const std::ptrdiff_t idx = cv.isEmpty() ? 0 : static_cast<std::ptrdiff_t>(rng() % (cv.size() + 1));
            cv.insert(idx, val);
            qv.insert(idx, val);
            break;
        }
        case 6:
            if (!cv.isEmpty()) {
                const std::ptrdiff_t idx = static_cast<std::ptrdiff_t>(rng() % cv.size());
                cv.removeAt(idx);
                qv.removeAt(idx);
            }
            break;
        case 7:
            if (!cv.isEmpty()) {
                const std::ptrdiff_t idx = static_cast<std::ptrdiff_t>(rng() % cv.size());
                const std::ptrdiff_t maxN = cv.size() - idx;
                const std::ptrdiff_t n = static_cast<std::ptrdiff_t>(rng() % (maxN + 1));
                cv.remove(idx, n);
                qv.remove(idx, n);
            }
            break;
        case 8: {
            const bool cr = cv.removeOne(val);
            const bool qr = qv.removeOne(val);
            CHECK(cr == qr, "Vector::removeOne mismatch at i=%d", i);
            break;
        }
        case 9:
            if (!cv.isEmpty()) {
                cv.removeFirst();
                qv.removeFirst();
            }
            break;
        case 10:
            if (!cv.isEmpty()) {
                cv.removeLast();
                qv.removeLast();
            }
            break;
        case 11:
            if (!cv.isEmpty()) {
                const int ct = cv.takeFirst();
                const int qt = qv.takeFirst();
                CHECK(ct == qt, "Vector::takeFirst mismatch at i=%d", i);
            }
            break;
        case 12:
            if (!cv.isEmpty()) {
                const int ct = cv.takeLast();
                const int qt = qv.takeLast();
                CHECK(ct == qt, "Vector::takeLast mismatch at i=%d", i);
            }
            break;
        case 13: {
            const std::ptrdiff_t pos = static_cast<std::ptrdiff_t>(rng() % 80) - 20;
            const std::ptrdiff_t len = static_cast<std::ptrdiff_t>(rng() % 80) - 20;
            Vector<int> cm = cv.mid(pos, len);
            QList<int> qm = qv.mid(pos, len);
            CHECK(sameVec(cm, qm), "Vector::mid(%lld, %lld) mismatch at i=%d (sz=%lld)",
                  static_cast<long long>(pos), static_cast<long long>(len), i,
                  static_cast<long long>(cv.size()));
            break;
        }
        case 14: {
            const std::ptrdiff_t newSz = static_cast<std::ptrdiff_t>(rng() % 64);
            cv.resize(newSz);
            qv.resize(newSz);
            break;
        }
        case 15:
            if (i % 50 == 0) {
                const std::ptrdiff_t fillSz = (i % 100 == 0) ? -1 : static_cast<std::ptrdiff_t>(rng() % 32);
                cv.fill(val, fillSz);
                qv.fill(val, fillSz);
            }
            break;
        case 16:
            if (cv.size() > 100) {
                cv.clear();
                qv.clear();
            }
            break;
        case 17: {
            Vector<int> copy = cv;
            if (!copy.isEmpty())
                copy[0] = val;
            break;
        }
        }

        CHECK(sameVec(cv, qv), "Vector state mismatch after op=%d at i=%d", op, i);
        CHECK(cv.contains(val) == qv.contains(val), "Vector::contains mismatch at i=%d", i);
        const std::ptrdiff_t from = static_cast<std::ptrdiff_t>(rng() % 50) - 15;
        CHECK(cv.indexOf(val, from) == qv.indexOf(val, from),
              "Vector::indexOf mismatch at i=%d (from=%lld)", i, static_cast<long long>(from));

        const std::ptrdiff_t vIdx = static_cast<std::ptrdiff_t>(rng() % 60) - 10;
        CHECK(cv.value(vIdx) == qv.value(vIdx), "Vector::value(i) mismatch at i=%d", i);
        CHECK(cv.value(vIdx, -777) == qv.value(vIdx, -777), "Vector::value(i, def) mismatch at i=%d", i);
        if (!cv.isEmpty()) {
            CHECK(cv.first() == qv.first() && cv.constFirst() == qv.constFirst(), "Vector::first mismatch at i=%d", i);
            CHECK(cv.last() == qv.last() && cv.constLast() == qv.constLast(), "Vector::last mismatch at i=%d", i);
        }
    }
}

static void testMapDifferential()
{
    std::mt19937_64 rng(0xB002ULL);
    Map<int, int> cm;
    QMap<int, int> qm;

    for (int i = 0; i < kNumSamples; ++i) {
        const int op = static_cast<int>(rng() % 8);
        const int key = static_cast<int>(rng() % 120) - 60;
        const int val = static_cast<int>(rng() % 1000) - 500;

        switch (op) {
        case 0:
        case 1:
            cm.insert(key, val);
            qm.insert(key, val);
            break;
        case 2:
            cm[key] = val;
            qm[key] = val;
            break;
        case 3: {
            const auto cr = cm.remove(key);
            const auto qr = qm.remove(key);
            CHECK(cr == qr, "Map::remove mismatch at i=%d", i);
            break;
        }
        case 4: {
            const int ct = cm.take(key);
            const int qt = qm.take(key);
            CHECK(ct == qt, "Map::take mismatch at i=%d", i);
            break;
        }
        case 5: {
            Map<int, int> copy = cm;
            CHECK(copy.isSharedWith(cm), "Map copy must be shared before write");
            copy.insert(key, val + 1);
            CHECK(!copy.isSharedWith(cm), "Map copy must detach on insert");
            break;
        }
        case 6:
            if (cm.size() > 80) {
                cm.clear();
                qm.clear();
            }
            break;
        case 7: {
            auto cit = cm.constFind(key);
            auto qit = qm.constFind(key);
            CHECK((cit != cm.cend()) == (qit != qm.cend()), "Map::constFind mismatch at i=%d", i);
            if (cit != cm.cend()) {
                CHECK(cit.key() == qit.key() && cit.value() == qit.value() && *cit == *qit,
                      "Map iterator key/value mismatch at i=%d", i);
            }
            break;
        }
        }

        CHECK(cm.size() == qm.size() && cm.isEmpty() == qm.isEmpty(), "Map::size mismatch at i=%d", i);
        CHECK(cm.contains(key) == qm.contains(key), "Map::contains mismatch at i=%d", i);
        CHECK(cm.value(key, -999) == qm.value(key, -999), "Map::value mismatch at i=%d", i);
        CHECK(std::as_const(cm)[key] == std::as_const(qm)[key], "Map const operator[] mismatch at i=%d", i);

        if (i % 50 == 0) {
            CHECK(sameVec(cm.keys(), qm.keys()), "Map::keys order/content mismatch at i=%d", i);
            CHECK(sameVec(cm.values(), qm.values()), "Map::values order/content mismatch at i=%d", i);
        }
    }
}

static void testHashAndSetDifferential()
{
    std::mt19937_64 rng(0xC003ULL);
    Hash<int, int> ch;
    QHash<int, int> qh;
    Set<int> cs;
    QSet<int> qs;

    for (int i = 0; i < kNumSamples; ++i) {
        const int op = static_cast<int>(rng() % 7);
        const int key = static_cast<int>(rng() % 150) - 75;
        const int val = static_cast<int>(rng() % 1000) - 500;

        switch (op) {
        case 0:
        case 1:
            ch.insert(key, val);
            qh.insert(key, val);
            cs.insert(key);
            qs.insert(key);
            break;
        case 2:
            ch[key] = val;
            qh[key] = val;
            cs.insert(val);
            qs.insert(val);
            break;
        case 3:
            CHECK(ch.remove(key) == qh.remove(key), "Hash::remove mismatch at i=%d", i);
            CHECK(cs.remove(key) == qs.remove(key), "Set::remove mismatch at i=%d", i);
            break;
        case 4:
            CHECK(ch.take(key) == qh.take(key), "Hash::take mismatch at i=%d", i);
            break;
        case 5: {
            Hash<int, int> hCopy = ch;
            Set<int> sCopy = cs;
            CHECK(hCopy.isSharedWith(ch) && sCopy.isSharedWith(cs), "Hash/Set copy must share");
            hCopy[key] = val;
            sCopy.insert(key);
            CHECK(!hCopy.isSharedWith(ch) && !sCopy.isSharedWith(cs), "Hash/Set must detach on write");
            break;
        }
        case 6:
            if (ch.size() > 100) {
                ch.clear();
                qh.clear();
                cs.clear();
                qs.clear();
            }
            break;
        }

        CHECK(ch.size() == qh.size() && ch.isEmpty() == qh.isEmpty(), "Hash::size mismatch at i=%d", i);
        CHECK(ch.contains(key) == qh.contains(key), "Hash::contains mismatch at i=%d", i);
        CHECK(ch.value(key, -42) == qh.value(key, -42), "Hash::value mismatch at i=%d", i);

        CHECK(cs.size() == qs.size() && cs.isEmpty() == qs.isEmpty(), "Set::size mismatch at i=%d", i);
        CHECK(cs.contains(key) == qs.contains(key), "Set::contains mismatch at i=%d", i);
    }

    Hash<CustomStringKey, int> strHash;
    Set<CustomStringKey> strSet;
    strHash.insert(CustomStringKey{"layer_0"}, 42);
    strSet.insert(CustomStringKey{"stroke_1"});
    CHECK(strHash.contains(CustomStringKey{"layer_0"}) && strHash.value(CustomStringKey{"layer_0"}) == 42,
          "Hash with std::hash custom key failed");
    CHECK(strSet.contains(CustomStringKey{"stroke_1"}),
          "Set with std::hash custom key failed");
}

static void testQueueDifferential()
{
    std::mt19937_64 rng(0xD004ULL);
    Queue<int> cq;
    QQueue<int> qq;

    for (int i = 0; i < kNumSamples; ++i) {
        const int op = static_cast<int>(rng() % 5);
        const int val = static_cast<int>(rng() % 1000);
        if (op < 3 || cq.isEmpty()) {
            cq.enqueue(val);
            qq.enqueue(val);
        } else {
            CHECK(cq.head() == qq.head(), "Queue::head mismatch at i=%d", i);
            CHECK(cq.dequeue() == qq.dequeue(), "Queue::dequeue mismatch at i=%d", i);
        }
        CHECK(cq.size() == qq.size() && cq.isEmpty() == qq.isEmpty(), "Queue::size mismatch at i=%d", i);
    }
}

static void testQtBridgeContainers()
{
    std::mt19937_64 rng(0xE005ULL);
    QVector<QPointF> qPts;
    qPts.reserve(kNumSamples);
    for (int i = 0; i < kNumSamples; ++i) {
        qPts.append(QPointF(i * 0.25 - 1000.0, (i % 97) * -1.5));
    }

    Vector<PointF> cPts = qPts;
    CHECK(cPts.size() == qPts.size(), "Bridge QVector<QPointF> -> Vector<PointF> size mismatch");
    for (int i = 0; i < kNumSamples; ++i) {
        CHECK(cPts[i].x() == qPts[i].x() && cPts[i].y() == qPts[i].y(),
              "Bridge QVector<QPointF> -> Vector<PointF> element mismatch at i=%d", i);
    }

    QVector<QPointF> qPtsBack = cPts;
    CHECK(qPtsBack == qPts, "Bridge Vector<PointF> -> QVector<QPointF> roundtrip mismatch");

    QMap<int, QRectF> qMap;
    for (int i = 0; i < 500; ++i) {
        const int k = static_cast<int>(rng() % 1000);
        qMap.insert(k, QRectF(i * 1.0, i * 2.0, i * 3.0, i * 4.0));
    }
    Map<int, RectF> cMap = qMap;
    QMap<int, QRectF> qMapBack = cMap;
    CHECK(qMapBack == qMap, "Bridge QMap<int, QRectF> <-> Map<int, RectF> roundtrip mismatch");

    QHash<int, QColor> qHash;
    for (int i = 0; i < 500; ++i) {
        qHash.insert(i, QColor(i % 256, (i * 3) % 256, (i * 7) % 256, (i * 11) % 256));
    }
    Hash<int, Color> cHash = qHash;
    QHash<int, QColor> qHashBack = cHash;
    CHECK(qHashBack == qHash, "Bridge QHash<int, QColor> <-> Hash<int, Color> roundtrip mismatch");

    QSet<int> qSet;
    for (int i = 0; i < 500; ++i)
        qSet.insert(static_cast<int>(rng() % 2000));
    Set<int> cSet = qSet;
    QSet<int> qSetBack = cSet;
    CHECK(qSetBack == qSet, "Bridge QSet<int> <-> Set<int> roundtrip mismatch");

    QQueue<QPoint> qQueue;
    for (int i = 0; i < 500; ++i)
        qQueue.enqueue(QPoint(i, -i));
    Queue<Point> cQueue = qQueue;
    QQueue<QPoint> qQueueBack = cQueue;
    CHECK(qQueueBack == qQueue, "Bridge QQueue<QPoint> <-> Queue<Point> roundtrip mismatch");
}

int main()
{
    testVectorImplicitSharingAndBool();
    testVectorDifferential();
    testMapDifferential();
    testHashAndSetDifferential();
    testQueueDifferential();
    testQtBridgeContainers();

    if (g_failed == 0) {
        std::printf("ALL containers_tests PASSED (%d random samples per suite)\n", kNumSamples);
    }
    return g_failed;
}
