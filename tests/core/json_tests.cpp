#include "algorithm/core/json.h"
#include "algorithm/core/qtbridge_string.h"
#include "algorithm/core/string.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

using AnimeCore::JsonArray;
using AnimeCore::JsonDocument;
using AnimeCore::JsonObject;
using AnimeCore::JsonParseError;
using AnimeCore::JsonValue;
using AnimeCore::String;

QString findTestDocumentDir()
{
    const QStringList candidates = {
        QStringLiteral("test_document"),
        QStringLiteral("../test_document"),
        QStringLiteral("../../test_document"),
        QStringLiteral("../../../test_document")
    };
    for (const QString &rel : candidates) {
        QFileInfo fi(rel);
        if (fi.exists() && fi.isDir())
            return fi.absoluteFilePath();
    }

    QDir dir(QDir::currentPath());
    for (int i = 0; i < 8; ++i) {
        if (dir.exists(QStringLiteral("test_document")))
            return dir.filePath(QStringLiteral("test_document"));
        if (!dir.cdUp())
            break;
    }
    QDir srcDir = QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir();
    for (int i = 0; i < 6; ++i) {
        if (srcDir.exists(QStringLiteral("test_document")))
            return srcDir.filePath(QStringLiteral("test_document"));
        if (!srcDir.cdUp())
            break;
    }
    return QString();
}

bool compareBytes(const std::string &coreBytes, const QByteArray &qtBytes, const char *label)
{
    if (static_cast<int>(coreBytes.size()) != qtBytes.size()
        || std::memcmp(coreBytes.data(), qtBytes.constData(), coreBytes.size()) != 0) {
        std::cerr << "FAIL: byte mismatch in " << label
                  << " (core size=" << coreBytes.size() << ", qt size=" << qtBytes.size() << ")\n";
        std::size_t minLen = std::min(coreBytes.size(), static_cast<std::size_t>(qtBytes.size()));
        for (std::size_t i = 0; i < minLen; ++i) {
            if (coreBytes[i] != qtBytes[static_cast<int>(i)]) {
                std::cerr << "  first diff at byte " << i << "\n";
                std::size_t start = i > 40 ? i - 40 : 0;
                std::size_t len = std::min<std::size_t>(80, minLen - start);
                std::cerr << "  core: [" << coreBytes.substr(start, len) << "]\n";
                std::cerr << "  qt  : [" << qtBytes.mid(static_cast<int>(start), static_cast<int>(len)).toStdString() << "]\n";
                break;
            }
        }
        return false;
    }
    return true;
}

bool testProjectDocuments()
{
    QString dirPath = findTestDocumentDir();
    if (dirPath.isEmpty()) {
        std::cerr << "FAIL: could not locate test_document directory\n";
        return false;
    }

    QDir dir(dirPath);
    QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    if (files.isEmpty()) {
        std::cerr << "FAIL: test_document directory is empty\n";
        return false;
    }

    int jsonCount = 0;
    for (const QFileInfo &fi : files) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) {
            std::cerr << "FAIL: cannot open " << fi.fileName().toStdString() << "\n";
            return false;
        }
        QByteArray raw = f.readAll();
        std::string rawStd(raw.constData(), static_cast<std::size_t>(raw.size()));

        bool coreOk = false;
        JsonDocument coreDoc = JsonDocument::parse(rawStd, &coreOk);

        QJsonParseError qtErr;
        QJsonDocument qtDoc = QJsonDocument::fromJson(raw, &qtErr);
        bool qtOk = (qtErr.error == QJsonParseError::NoError && !qtDoc.isNull());

        if (coreOk != qtOk || coreDoc.isNull() != qtDoc.isNull()) {
            std::cerr << "FAIL: parse status mismatch on " << fi.fileName().toStdString()
                      << " coreOk=" << coreOk << " qtOk=" << qtOk << "\n";
            return false;
        }

        if (coreOk) {
            ++jsonCount;
            if (!compareBytes(coreDoc.toJson(JsonDocument::Indented),
                              qtDoc.toJson(QJsonDocument::Indented),
                              "test_document Indented")) {
                return false;
            }
            if (!compareBytes(coreDoc.toJson(JsonDocument::Compact),
                              qtDoc.toJson(QJsonDocument::Compact),
                              "test_document Compact")) {
                return false;
            }
        }
    }

    if (jsonCount == 0) {
        std::cerr << "FAIL: no valid JSON project files found in test_document\n";
        return false;
    }
    return true;
}

struct DualValue
{
    JsonValue core;
    QJsonValue qt;
};

DualValue makeRandomValue(std::mt19937_64 &rng, int depth);

String makeRandomString(std::mt19937_64 &rng)
{
    static const char *const fragments[] = {
        "layer", "frame", "stroke", "opacity", "matrix",
        "图层", "关键帧", "画笔", "透明度", "脚本数据",
        "レイヤー", "フレーム", "アニメーション",
        "😀", "🎨", "🚀", "🔥", "✨",
        "\"", "\\", "/", "\b", "\f", "\n", "\r", "\t",
        "\x01", "\x07", "\x1b", "\x1f", ""
    };
    constexpr std::size_t fragCount = sizeof(fragments) / sizeof(fragments[0]);
    std::uniform_int_distribution<int> lenDist(0, 8);
    std::uniform_int_distribution<std::size_t> pickDist(0, fragCount - 1);

    int count = lenDist(rng);
    std::string s;
    for (int i = 0; i < count; ++i) {
        const char *frag = fragments[pickDist(rng)];
        if (frag[0] == '\0' && (rng() % 5 == 0))
            s.push_back('\0');
        else
            s.append(frag);
    }
    return String::fromUtf8(s);
}

DualValue makeRandomNumber(std::mt19937_64 &rng)
{
    static const long long specialInts[] = {
        0LL, 1LL, -1LL, 42LL, -42LL,
        static_cast<long long>(std::numeric_limits<int>::min()),
        static_cast<long long>(std::numeric_limits<int>::max()),
        std::numeric_limits<long long>::min(),
        std::numeric_limits<long long>::max(),
        (1LL << 53) - 1,
        (1LL << 53),
        (1LL << 53) + 2,
        -((1LL << 53) - 1),
        -(1LL << 53)
    };
    static const double specialDoubles[] = {
        0.0, -0.0, 0.1, -0.1, 0.5, -0.5, 0.25, 123.456, -987.654321,
        0.001, 0.0001, 0.00001, 0.000001, 1e-7, -1e-7,
        1e5, 1e6, 1e14, 1e15, 1e16, 1e19, 1e20, 1e21,
        1e300, -1e300, 1e-300, -1e-300,
        1.234567890123456, -1.2345678901234567e20,
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()
    };

    int mode = static_cast<int>(rng() % 3);
    if (mode == 0) {
        constexpr std::size_t n = sizeof(specialInts) / sizeof(specialInts[0]);
        long long v = specialInts[rng() % n];
        return {JsonValue(v), QJsonValue(static_cast<qint64>(v))};
    }
    if (mode == 1) {
        constexpr std::size_t n = sizeof(specialDoubles) / sizeof(specialDoubles[0]);
        double d = specialDoubles[rng() % n];
        return {JsonValue(d), QJsonValue(d)};
    }
    std::uniform_real_distribution<double> dist(-1e6, 1e6);
    double d = dist(rng);
    return {JsonValue(d), QJsonValue(d)};
}

DualValue makeRandomObject(std::mt19937_64 &rng, int depth)
{
    JsonObject coreObj;
    QJsonObject qtObj;
    std::uniform_int_distribution<int> countDist(0, depth == 0 ? 12 : 6);
    int n = countDist(rng);
    for (int i = 0; i < n; ++i) {
        String key = makeRandomString(rng);
        QString qkey = key;
        DualValue child = makeRandomValue(rng, depth + 1);
        coreObj.insert(key, child.core);
        qtObj.insert(qkey, child.qt);
    }
    return {JsonValue(coreObj), QJsonValue(qtObj)};
}

DualValue makeRandomArray(std::mt19937_64 &rng, int depth)
{
    JsonArray coreArr;
    QJsonArray qtArr;
    std::uniform_int_distribution<int> countDist(0, depth == 0 ? 12 : 6);
    int n = countDist(rng);
    for (int i = 0; i < n; ++i) {
        DualValue child = makeRandomValue(rng, depth + 1);
        coreArr.append(child.core);
        qtArr.append(child.qt);
    }
    return {JsonValue(coreArr), QJsonValue(qtArr)};
}

DualValue makeRandomValue(std::mt19937_64 &rng, int depth)
{
    int maxChoice = (depth >= 4) ? 4 : 6;
    std::uniform_int_distribution<int> choiceDist(0, maxChoice - 1);
    int c = choiceDist(rng);
    switch (c) {
    case 0:
        return {JsonValue(JsonValue::Null), QJsonValue(QJsonValue::Null)};
    case 1: {
        bool b = (rng() & 1) != 0;
        return {JsonValue(b), QJsonValue(b)};
    }
    case 2:
        return makeRandomNumber(rng);
    case 3: {
        String s = makeRandomString(rng);
        QString qs = s;
        return {JsonValue(s), QJsonValue(qs)};
    }
    case 4:
        return makeRandomArray(rng, depth);
    default:
        return makeRandomObject(rng, depth);
    }
}

bool testRandomNestedJson()
{
    std::mt19937_64 rng(20261101ULL);

    for (int iter = 0; iter < 100; ++iter) {
        bool isObj = (iter % 2 == 0);
        JsonDocument coreDoc;
        QJsonDocument qtDoc;

        if (isObj) {
            DualValue dv = makeRandomObject(rng, 0);
            coreDoc.setObject(dv.core.toObject());
            qtDoc.setObject(dv.qt.toObject());

            // Also verify Bridge<QJsonObject> round-trip
            QJsonObject bridgedQt = coreDoc.object();
            JsonObject bridgedCore = bridgedQt;
            if (JsonDocument(bridgedCore).toJson(JsonDocument::Compact)
                != coreDoc.toJson(JsonDocument::Compact)) {
                std::cerr << "FAIL: Bridge<QJsonObject> mismatch on iter " << iter << "\n";
                return false;
            }
        } else {
            DualValue dv = makeRandomArray(rng, 0);
            coreDoc.setArray(dv.core.toArray());
            qtDoc.setArray(dv.qt.toArray());

            // Also verify Bridge<QJsonArray> round-trip
            QJsonArray bridgedQt = coreDoc.array();
            JsonArray bridgedCore = bridgedQt;
            if (JsonDocument(bridgedCore).toJson(JsonDocument::Compact)
                != coreDoc.toJson(JsonDocument::Compact)) {
                std::cerr << "FAIL: Bridge<QJsonArray> mismatch on iter " << iter << "\n"
                          << "  bridged: " << JsonDocument(bridgedCore).toJson(JsonDocument::Compact) << "\n"
                          << "  coreDoc: " << coreDoc.toJson(JsonDocument::Compact) << "\n"
                          << "  qtDoc  : " << qtDoc.toJson(QJsonDocument::Compact).toStdString() << "\n";
                return false;
            }
        }

        std::string coreIndented = coreDoc.toJson(JsonDocument::Indented);
        QByteArray qtIndented = qtDoc.toJson(QJsonDocument::Indented);
        if (!compareBytes(coreIndented, qtIndented, "random Indented"))
            return false;

        std::string coreCompact = coreDoc.toJson(JsonDocument::Compact);
        QByteArray qtCompact = qtDoc.toJson(QJsonDocument::Compact);
        if (!compareBytes(coreCompact, qtCompact, "random Compact"))
            return false;

        // Re-parse serialized output and compare again
        bool ok1 = false, ok2 = false;
        JsonDocument reparsed1 = JsonDocument::parse(coreIndented, &ok1);
        JsonDocument reparsed2 = JsonDocument::parse(coreCompact, &ok2);
        if (!ok1 || !ok2) {
            std::cerr << "FAIL: failed to re-parse generated JSON on iter " << iter << "\n";
            return false;
        }
        if (!compareBytes(reparsed1.toJson(JsonDocument::Indented), qtIndented, "reparsed Indented")
            || !compareBytes(reparsed2.toJson(JsonDocument::Compact), qtCompact, "reparsed Compact")) {
            return false;
        }
    }

    return true;
}

bool testParserEdgeCases()
{
    const char *cases[] = {
        "",
        "   ",
        "null",
        "true",
        "false",
        "123",
        "\"hello\"",
        "{}",
        "[]",
        "\xEF\xBB\xBF{}",
        "\xEF\xBB\xBF[1, 2, 3]",
        "{\"a\":1,}",
        "[1,2,]",
        "[,1]",
        "{,}",
        "{1:2}",
        "{\"a\" 1}",
        "{\"a\":1 \"b\":2}",
        "[1 2]",
        "{\"a\":01}",
        "{\"a\":-01}",
        "{\"a\":00}",
        "{\"a\":+5}",
        "{\"a\":.5}",
        "{\"a\":5.}",
        "{\"a\":-.5}",
        "{\"a\":.}",
        "{\"a\":-.}",
        "{\"a\":1e}",
        "{\"a\":1e+}",
        "{\"a\":1e-}",
        "{\"a\":1e300}",
        "{\"a\":-1e300}",
        "{\"a\":1e309}",
        "{\"a\":-1e309}",
        "{\"a\":-0}",
        "{\"a\":-0.0}",
        "{\"a\":1} trailing",
        "{\"a\":\"\\uD83D\\uDE00\"}",
        "{\"a\":\"\\uD83D\"}",
        "{\"a\":\"\\uDE00\"}",
        "{\"a\":\"\\u004g\"}",
        "{\"a\":\"\\u004\"}",
        "{\"a\":\"unterminated}",
        "{\"a\":\"unterminated\\}",
        "{\"a\":1",
        "[1, 2",
        "[\"\xFF\"]",
        "[\"\xC0\x80\"]",
        "[\"\xED\xA0\x80\"]"
    };

    for (const char *input : cases) {
        bool coreOk = false;
        JsonDocument coreDoc = JsonDocument::parse(input, -1, &coreOk);

        QJsonParseError qtErr;
        QJsonDocument qtDoc = QJsonDocument::fromJson(QByteArray(input), &qtErr);
        bool qtOk = (qtErr.error == QJsonParseError::NoError && !qtDoc.isNull());

        if (coreOk != qtOk) {
            std::cerr << "FAIL: parser acceptance mismatch on input=[" << input
                      << "] coreOk=" << coreOk << " qtOk=" << qtOk
                      << " qtErr=" << static_cast<int>(qtErr.error) << "\n";
            return false;
        }
        if (coreOk) {
            if (!compareBytes(coreDoc.toJson(JsonDocument::Compact),
                              qtDoc.toJson(QJsonDocument::Compact),
                              input)) {
                return false;
            }
        }
    }

    // Deep nesting > 1024 should fail in both
    std::string deepOpen(1030, '[');
    std::string deepClose(1030, ']');
    std::string deepJson = deepOpen + deepClose;
    bool deepCoreOk = true;
    JsonDocument::parse(deepJson, &deepCoreOk);
    QJsonParseError deepQtErr;
    QJsonDocument deepQtDoc = QJsonDocument::fromJson(QByteArray::fromStdString(deepJson), &deepQtErr);
    bool deepQtOk = (deepQtErr.error == QJsonParseError::NoError && !deepQtDoc.isNull());
    if (deepCoreOk != deepQtOk || deepCoreOk) {
        std::cerr << "FAIL: deep nesting > 1024 should be rejected\n";
        return false;
    }

    return true;
}

bool testContainerApiAndImplicitSharing()
{
    JsonObject obj;
    obj.insert(ANIME_STR("z"), 1);
    obj.insert(ANIME_STR("a"), ANIME_STR("hello"));
    obj[ANIME_STR("m")] = true;

    std::vector<String> keys = obj.keys();
    if (keys.size() != 3 || keys[0] != ANIME_STR("a") || keys[1] != ANIME_STR("m") || keys[2] != ANIME_STR("z")) {
        std::cerr << "FAIL: JsonObject key sorting\n";
        return false;
    }

    // Implicit sharing on JsonObject
    JsonObject objCopy = obj;
    objCopy.insert(ANIME_STR("a"), ANIME_STR("modified"));
    objCopy.remove(ANIME_STR("z"));
    if (obj.value(ANIME_STR("a")).toString() != ANIME_STR("hello") || !obj.contains(ANIME_STR("z"))
        || objCopy.value(ANIME_STR("a")).toString() != ANIME_STR("modified") || objCopy.contains(ANIME_STR("z"))) {
        std::cerr << "FAIL: JsonObject copy-on-write isolation\n";
        return false;
    }

    // Missing key returns Undefined and default values
    JsonValue missing = obj.value(ANIME_STR("nonexistent"));
    if (!missing.isUndefined() || !missing.toString().isNull()
        || missing.toString(ANIME_STR("fallback")) != ANIME_STR("fallback")
        || missing.toInt(77) != 77 || missing.toDouble(3.5) != 3.5 || missing.toBool(true) != true) {
        std::cerr << "FAIL: JsonValue Undefined default conversions\n";
        return false;
    }

    // Implicit sharing on JsonArray
    JsonArray arr;
    arr.append(10);
    arr.append(ANIME_STR("two"));
    arr.append(false);

    JsonArray arrCopy = arr;
    arrCopy.replace(1, ANIME_STR("TWO"));
    arrCopy.append(99);
    if (arr.size() != 3 || arr.at(1).toString() != ANIME_STR("two")
        || arrCopy.size() != 4 || arrCopy.at(1).toString() != ANIME_STR("TWO")) {
        std::cerr << "FAIL: JsonArray copy-on-write isolation\n";
        return false;
    }

    int count = 0;
    for (const JsonValue &v : arr) {
        if (v.isUndefined())
            return false;
        ++count;
    }
    if (count != 3) {
        std::cerr << "FAIL: JsonArray range-for\n";
        return false;
    }

    // 1 MB string copied 10,000 times within 50 ms
    std::string oneMb(1024 * 1024, 's');
    String scriptData = String::fromUtf8(oneMb);
    std::vector<String> copies(10000);
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 10000; ++i)
        copies[static_cast<std::size_t>(i)] = scriptData;
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (ms > 50.0) {
        std::cerr << "FAIL: 1 MB String 10000 copies took " << ms << " ms (> 50 ms)\n";
        return false;
    }

    return true;
}

} // namespace

int main()
{
    if (!testProjectDocuments())
        return 1;
    if (!testRandomNestedJson())
        return 1;
    if (!testParserEdgeCases())
        return 1;
    if (!testContainerApiAndImplicitSharing())
        return 1;
    return 0;
}
