#include "algorithm/core/qtbridge_geometry.h"
#ifndef ANIMECORE_HAS_COREOF
namespace AnimeCore {
template <typename T>
struct CoreOf;
} // namespace AnimeCore
#define ANIMECORE_HAS_COREOF
#endif
#include "algorithm/core/qtbridge_string.h"
#include "algorithm/core/string.h"

static_assert(std::is_same_v<AnimeCore::CoreOf<QString>::type, AnimeCore::String>,
              "CoreOf<QString>::type must be AnimeCore::String");

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using AnimeCore::CaseInsensitive;
using AnimeCore::CaseSensitive;
using AnimeCore::Char;
using AnimeCore::KeepEmptyParts;
using AnimeCore::Latin1Char;
using AnimeCore::Latin1String;
using AnimeCore::SkipEmptyParts;
using AnimeCore::String;

bool sameText(const String &a, const QString &b)
{
    if (a.isNull() != b.isNull())
        return false;
    if (a.isEmpty() != b.isEmpty())
        return false;
    if (a.size() != b.size())
        return false;
    return a.toUtf8() == b.toUtf8().toStdString();
}

bool sameContent(const String &a, const QString &b)
{
    if (a.isEmpty() != b.isEmpty())
        return false;
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i) {
        if (a.at(i).unicode() != b.at(i).unicode())
            return false;
    }
    return true;
}

bool testNullAndEmpty()
{
    String sNull;
    QString qNull;
    if (!sameText(sNull, qNull)) {
        std::cerr << "FAIL: default constructed String vs QString\n";
        return false;
    }

    String sEmpty = ANIME_STR("");
    QString qEmpty = QStringLiteral("");
    if (!sameText(sEmpty, qEmpty)) {
        std::cerr << "FAIL: empty literal String vs QString\n";
        return false;
    }

    if (!(sNull == sEmpty) || (sNull != sEmpty)) {
        std::cerr << "FAIL: null == empty comparison\n";
        return false;
    }

    String sUtf8Null = String::fromUtf8(nullptr, 0);
    QString qUtf8Null = QString::fromUtf8(nullptr, 0);
    if (!sameText(sUtf8Null, qUtf8Null)) {
        std::cerr << "FAIL: fromUtf8(nullptr, 0)\n";
        return false;
    }

    String sUtf8Empty = String::fromUtf8("", 0);
    QString qUtf8Empty = QString::fromUtf8("", 0);
    if (!sameText(sUtf8Empty, qUtf8Empty)) {
        std::cerr << "FAIL: fromUtf8(\"\", 0)\n";
        return false;
    }

    String sCleared = ANIME_STR("hello");
    sCleared.clear();
    if (!sCleared.isNull() || !sCleared.isEmpty() || sCleared.size() != 0) {
        std::cerr << "FAIL: clear() should produce null string\n";
        return false;
    }

    return true;
}

bool testImplicitSharingAndPerf()
{
    String orig = ANIME_STR("图层_Layer_1");
    String copy = orig;
    copy.append(ANIME_STR("_副本"));
    if (orig != ANIME_STR("图层_Layer_1") || copy != ANIME_STR("图层_Layer_1_副本")) {
        std::cerr << "FAIL: copy-on-write isolation on append\n";
        return false;
    }

    String copy2 = orig;
    copy2.replace(ANIME_STR("1"), ANIME_STR("2"));
    if (orig != ANIME_STR("图层_Layer_1") || copy2 != ANIME_STR("图层_Layer_2")) {
        std::cerr << "FAIL: copy-on-write isolation on replace\n";
        return false;
    }

    String copy3 = orig;
    copy3.prepend(Latin1Char('#'));
    if (orig != ANIME_STR("图层_Layer_1") || copy3 != ANIME_STR("#图层_Layer_1")) {
        std::cerr << "FAIL: copy-on-write isolation on prepend\n";
        return false;
    }

    // 1 MB string copied 10,000 times must finish within 50 ms.
    std::string oneMbUtf8(1024 * 1024, 'x');
    String big = String::fromUtf8(oneMbUtf8);
    std::vector<String> copies(10000);

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 10000; ++i)
        copies[static_cast<std::size_t>(i)] = big;
    auto t1 = std::chrono::steady_clock::now();

    double elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (elapsedMs > 50.0) {
        std::cerr << "FAIL: 1 MB string 10000 copies took " << elapsedMs << " ms (> 50 ms)\n";
        return false;
    }

    copies[5000].replace(0, 1, ANIME_STR("y"));
    if (big.at(0) != Char(u'x') || copies[0].at(0) != Char(u'x') || copies[5000].at(0) != Char(u'y')) {
        std::cerr << "FAIL: 1 MB copy mutation leaked to original\n";
        return false;
    }

    return true;
}

bool testDifferentialRandomStrings()
{
    const std::vector<const char *> pool = {
        "a", "B", "z", "0", "9", "-", "_", ".", "/", ":", ">", " ", "\t", "\n",
        "图层", "帧", "画布", "动画", "背景",
        "あ", "い", "う", "ア", "イ", "ウ", "東京",
        "😀", "🎨", "🚀", "🔥", "✨", "🧩"
    };

    std::mt19937 rng(20261030);
    std::uniform_int_distribution<int> lenDist(0, 25);
    std::uniform_int_distribution<std::size_t> tokDist(0, pool.size() - 1);

    std::vector<std::pair<String, QString>> samples;
    samples.emplace_back(String(), QString());
    samples.emplace_back(ANIME_STR(""), QStringLiteral(""));
    samples.emplace_back(ANIME_STR("   \t\n  "), QStringLiteral("   \t\n  "));

    for (int i = 0; i < 300; ++i) {
        int n = lenDist(rng);
        std::string utf8;
        for (int j = 0; j < n; ++j)
            utf8 += pool[tokDist(rng)];
        samples.emplace_back(String::fromUtf8(utf8), QString::fromUtf8(utf8.c_str(), static_cast<int>(utf8.size())));
    }

    for (std::size_t idx = 0; idx < samples.size(); ++idx) {
        const String &s = samples[idx].first;
        const QString &q = samples[idx].second;

        if (!sameText(s, q)) {
            std::cerr << "FAIL: sample " << idx << " base mismatch\n";
            return false;
        }

        if (s.toLatin1() != q.toLatin1().toStdString()) {
            std::cerr << "FAIL: sample " << idx << " toLatin1 mismatch\n";
            return false;
        }

        // Bridge round-trip
        QString qFromS = s;
        String sFromQ = q;
        if (!sameText(sFromQ, q) || !sameText(String(qFromS), q)) {
            std::cerr << "FAIL: sample " << idx << " Bridge<QString> mismatch\n";
            return false;
        }

        // trimmed / simplified / case folding
        if (!sameContent(s.trimmed(), q.trimmed())) {
            std::cerr << "FAIL: sample " << idx << " trimmed mismatch\n";
            return false;
        }
        if (!sameContent(s.simplified(), q.simplified())) {
            std::cerr << "FAIL: sample " << idx << " simplified mismatch\n";
            return false;
        }
        if (!sameContent(s.toLower(), q.toLower()) || !sameContent(s.toUpper(), q.toUpper())) {
            std::cerr << "FAIL: sample " << idx << " toLower/toUpper mismatch\n";
            return false;
        }

        // left / right / mid across edge indices
        for (int pos : {-5, -1, 0, 1, 2, s.size() / 2, s.size() - 1, s.size(), s.size() + 3}) {
            if (!sameContent(s.left(pos), q.left(pos))) {
                std::cerr << "FAIL: sample " << idx << " left(" << pos << ") mismatch\n";
                return false;
            }
            if (!sameContent(s.right(pos), q.right(pos))) {
                std::cerr << "FAIL: sample " << idx << " right(" << pos << ") mismatch\n";
                return false;
            }
            for (int n : {-1, 0, 1, 3, s.size(), s.size() + 5}) {
                if (!sameContent(s.mid(pos, n), q.mid(pos, n))) {
                    std::cerr << "FAIL: sample " << idx << " (size=" << s.size() << ") mid(" << pos << "," << n << ") mismatch: "
                              << "s.mid.size=" << s.mid(pos, n).size() << " q.mid.size=" << q.mid(pos, n).size()
                              << " s.utf8=[" << s.mid(pos, n).toUtf8() << "] q.utf8=[" << q.mid(pos, n).toUtf8().toStdString() << "]\n";
                    return false;
                }
            }
        }

        // Pairwise operations with another sample
        const auto &otherPair = samples[(idx * 37 + 13) % samples.size()];
        const String &s2 = otherPair.first;
        const QString &q2 = otherPair.second;

        if (!sameContent(s + s2, q + q2)) {
            std::cerr << "FAIL: sample " << idx << " operator+ mismatch\n";
            return false;
        }
        if ((s == s2) != (q == q2) || (s != s2) != (q != q2) || (s < s2) != (q < q2)) {
            std::cerr << "FAIL: sample " << idx << " relational operator mismatch\n";
            return false;
        }
        if ((s.compare(s2, CaseSensitive) < 0) != (q.compare(q2, Qt::CaseSensitive) < 0)
            || (s.compare(s2, CaseSensitive) == 0) != (q.compare(q2, Qt::CaseSensitive) == 0)) {
            std::cerr << "FAIL: sample " << idx << " compare(CaseSensitive) mismatch\n";
            return false;
        }
        if ((s.compare(s2, CaseInsensitive) < 0) != (q.compare(q2, Qt::CaseInsensitive) < 0)
            || (s.compare(s2, CaseInsensitive) == 0) != (q.compare(q2, Qt::CaseInsensitive) == 0)) {
            std::cerr << "FAIL: sample " << idx << " compare(CaseInsensitive) mismatch\n";
            return false;
        }

        // Substring probes on valid UTF-16 codepoint boundaries
        int subPos = s.size() / 3;
        if (subPos > 0 && s.at(subPos).isLowSurrogate())
            --subPos;
        int subLen = 2;
        if (subPos + subLen > 0 && subPos + subLen <= s.size()
            && s.at(subPos + subLen - 1).isHighSurrogate()) {
            ++subLen;
        }
        String sub = s.mid(subPos, subLen);
        QString qsub = q.mid(subPos, subLen);
        for (const auto &needlePair : {std::make_pair(sub, qsub), std::make_pair(s2, q2),
                                       std::make_pair(ANIME_STR("a"), QStringLiteral("a")),
                                       std::make_pair(ANIME_STR("图层"), QStringLiteral("图层")),
                                       std::make_pair(ANIME_STR(""), QStringLiteral(""))}) {
            const String &nd = needlePair.first;
            const QString &qnd = needlePair.second;

            if (s.startsWith(nd, CaseSensitive) != q.startsWith(qnd, Qt::CaseSensitive)
                || s.startsWith(nd, CaseInsensitive) != q.startsWith(qnd, Qt::CaseInsensitive)) {
                std::cerr << "FAIL: sample " << idx << " startsWith mismatch\n";
                return false;
            }
            if (s.endsWith(nd, CaseSensitive) != q.endsWith(qnd, Qt::CaseSensitive)
                || s.endsWith(nd, CaseInsensitive) != q.endsWith(qnd, Qt::CaseInsensitive)) {
                std::cerr << "FAIL: sample " << idx << " endsWith mismatch\n";
                return false;
            }
            for (int from : {-3, -1, 0, 1, s.size() / 2, s.size(), s.size() + 2}) {
                if (s.indexOf(nd, from, CaseSensitive) != q.indexOf(qnd, from, Qt::CaseSensitive)
                    || s.indexOf(nd, from, CaseInsensitive) != q.indexOf(qnd, from, Qt::CaseInsensitive)) {
                    std::cerr << "FAIL: sample " << idx << " (len=" << s.size() << ", nd.len=" << nd.size() << ") indexOf from=" << from
                              << " CS: got=" << s.indexOf(nd, from, CaseSensitive) << " exp=" << q.indexOf(qnd, from, Qt::CaseSensitive)
                              << " CI: got=" << s.indexOf(nd, from, CaseInsensitive) << " exp=" << q.indexOf(qnd, from, Qt::CaseInsensitive) << "\n";
                    return false;
                }
                if (s.lastIndexOf(nd, from, CaseSensitive) != q.lastIndexOf(qnd, from, Qt::CaseSensitive)
                    || s.lastIndexOf(nd, from, CaseInsensitive) != q.lastIndexOf(qnd, from, Qt::CaseInsensitive)) {
                    std::cerr << "FAIL: sample " << idx << " (len=" << s.size() << ", nd.len=" << nd.size() << ") lastIndexOf from=" << from
                              << " CS: got=" << s.lastIndexOf(nd, from, CaseSensitive) << " exp=" << q.lastIndexOf(qnd, from, Qt::CaseSensitive)
                              << " CI: got=" << s.lastIndexOf(nd, from, CaseInsensitive) << " exp=" << q.lastIndexOf(qnd, from, Qt::CaseInsensitive) << "\n";
                    return false;
                }
            }
            if (s.lastIndexOf(nd, CaseSensitive) != q.lastIndexOf(qnd, Qt::CaseSensitive)) {
                std::cerr << "FAIL: sample " << idx << " 1-arg lastIndexOf mismatch\n";
                return false;
            }

            // split & replace (skip empty needle for split/replace except dedicated check)
            if (!nd.isEmpty()) {
                auto spKeep = s.split(nd, KeepEmptyParts, CaseSensitive);
                auto qspKeep = q.split(qnd, Qt::KeepEmptyParts, Qt::CaseSensitive);
                if (static_cast<int>(spKeep.size()) != qspKeep.size()) {
                    std::cerr << "FAIL: sample " << idx << " split KeepEmptyParts size mismatch\n";
                    return false;
                }
                for (int k = 0; k < qspKeep.size(); ++k) {
                    if (!sameContent(spKeep[static_cast<std::size_t>(k)], qspKeep[k])) {
                        std::cerr << "FAIL: sample " << idx << " split KeepEmptyParts element mismatch\n";
                        return false;
                    }
                }

                auto spSkip = s.split(nd, SkipEmptyParts, CaseSensitive);
                auto qspSkip = q.split(qnd, Qt::SkipEmptyParts, Qt::CaseSensitive);
                if (static_cast<int>(spSkip.size()) != qspSkip.size()) {
                    std::cerr << "FAIL: sample " << idx << " split SkipEmptyParts size mismatch\n";
                    return false;
                }
                for (int k = 0; k < qspSkip.size(); ++k) {
                    if (!sameContent(spSkip[static_cast<std::size_t>(k)], qspSkip[k])) {
                        std::cerr << "FAIL: sample " << idx << " split SkipEmptyParts element mismatch\n";
                        return false;
                    }
                }

                String sRep = s;
                QString qRep = q;
                sRep.replace(nd, ANIME_STR("#"), CaseSensitive);
                qRep.replace(qnd, QStringLiteral("#"), Qt::CaseSensitive);
                if (!sameContent(sRep, qRep)) {
                    std::cerr << "FAIL: sample " << idx << " replace mismatch\n";
                    return false;
                }
            }
        }
    }

    return true;
}

bool testNumbersAndArg()
{
    const int intBoundary[] = {
        0, 1, -1, 42, -42, 100000, -100000,
        std::numeric_limits<int>::max(),
        std::numeric_limits<int>::min()
    };
    for (int v : intBoundary) {
        String s = String::number(v);
        QString q = QString::number(v);
        if (!sameContent(s, q)) {
            std::cerr << "FAIL: String::number(int) for " << v << "\n";
            return false;
        }
        bool okS = false, okQ = false;
        int parsedS = s.toInt(&okS);
        int parsedQ = q.toInt(&okQ);
        if (okS != okQ || parsedS != parsedQ) {
            std::cerr << "FAIL: toInt round-trip for " << v << "\n";
            return false;
        }
    }

    const long long llBoundary[] = {
        0LL, 1LL, -1LL,
        std::numeric_limits<long long>::max(),
        std::numeric_limits<long long>::min()
    };
    for (long long v : llBoundary) {
        if (!sameContent(String::number(v), QString::number(v))) {
            std::cerr << "FAIL: String::number(long long) for " << v << "\n";
            return false;
        }
    }

    const double dblSpecials[] = {
        0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 123456.0, 1234567.0,
        0.0001, 0.00001, 0.000001, 1e-5, 1e-4, 1e5, 1e6, 1e7,
        1e100, -1e100, 1e-100, -1e-100, 1.23456789,
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()
    };
    for (double d : dblSpecials) {
        String s = String::number(d);
        QString q = QString::number(d);
        if (!sameContent(s, q)) {
            std::cerr << "FAIL: String::number(double) for " << d
                      << " got=" << s.toStdString() << " expected=" << q.toStdString() << "\n";
            return false;
        }
    }

    std::mt19937_64 rng(987654321ULL);
    for (int i = 0; i < 5000; ++i) {
        uint64_t bits = rng();
        double d = 0.0;
        std::memcpy(&d, &bits, sizeof(d));
        String s = String::number(d);
        QString q = QString::number(d);
        if (!sameContent(s, q)) {
            std::cerr << "FAIL: random double String::number mismatch: got="
                      << s.toStdString() << " expected=" << q.toStdString() << "\n";
            return false;
        }
    }

    const char *parseInputs[] = {
        "", "   ", "0", "-0", "+0", " 42 ", "\t\n -42 \r\v\f", "42a", "--1", "++1",
        "0x10", "010", "2147483647", "-2147483648", "2147483648", "-2147483649",
        "1.5", ".5", "5.", "+.5", "-.5", ".", "+.", "-.", "1e5", "1e-5",
        "1e309", "-1e309", "inf", "-inf", "+inf", "infinity", "-infinity", "nan", "NaN",
        "4294967295", "4294967296", "18446744073709551615", "18446744073709551616",
        "图层123", "123图层"
    };
    for (const char *in : parseInputs) {
        String s = String::fromUtf8(in);
        QString q = QString::fromUtf8(in);
        for (int base : {0, 2, 8, 10, 16}) {
            bool okS = false, okQ = false;
            int viS = s.toInt(&okS, base);
            bool okUS = false, okUQ = false;
            unsigned int vuS = s.toUInt(&okUS, base);
            unsigned int vuQ = q.toUInt(&okUQ, base);
            if (okUS != okUQ || vuS != vuQ) {
                std::cerr << "FAIL: toUInt(" << in << ", base=" << base << ") got=("
                          << okUS << "," << vuS << ") expected=(" << okUQ << "," << vuQ << ")\n";
                return false;
            }
            bool okULLS = false, okULLQ = false;
            unsigned long long vullS = s.toULongLong(&okULLS, base);
            unsigned long long vullQ = q.toULongLong(&okULLQ, base);
            if (okULLS != okULLQ || vullS != vullQ) {
                std::cerr << "FAIL: toULongLong(" << in << ", base=" << base << ") got=("
                          << okULLS << "," << vullS << ") expected=(" << okULLQ << "," << vullQ << ")\n";
                return false;
            }
            int viQ = q.toInt(&okQ, base);
            if (okS != okQ || viS != viQ) {
                std::cerr << "FAIL: toInt(" << in << ", base=" << base << ") got=("
                          << okS << "," << viS << ") expected=(" << okQ << "," << viQ << ")\n";
                return false;
            }
        }
        bool okDS = false, okDQ = false;
        double vdS = s.toDouble(&okDS);
        double vdQ = q.toDouble(&okDQ);
        bool sameVal = (std::isnan(vdS) && std::isnan(vdQ)) || (vdS == vdQ);
        if (okDS != okDQ || !sameVal) {
            std::cerr << "FAIL: toDouble(" << in << ") got=(" << okDS << "," << vdS
                      << ") expected=(" << okDQ << "," << vdQ << ")\n";
            return false;
        }
    }

    // String::arg tests matching codebase usage
    String a1 = ANIME_STR("%1 %2").arg(ANIME_STR("图层"), String::number(3));
    QString qa1 = QStringLiteral("%1 %2").arg(QStringLiteral("图层"), QString::number(3));
    if (!sameContent(a1, qa1)) {
        std::cerr << "FAIL: arg(%1 %2) mismatch\n";
        return false;
    }

    String a2 = ANIME_STR("Layer %1: %2 (%3)").arg(1).arg(ANIME_STR("背景")).arg(2.5);
    QString qa2 = QStringLiteral("Layer %1: %2 (%3)").arg(1).arg(QStringLiteral("背景")).arg(2.5);
    if (!sameContent(a2, qa2)) {
        std::cerr << "FAIL: chained arg mismatch\n";
        return false;
    }

    // Hash and ordered container usage
    std::unordered_set<String> hashSet;
    hashSet.insert(String());
    hashSet.insert(ANIME_STR(""));
    if (hashSet.size() != 1) {
        std::cerr << "FAIL: null and empty String should hash equal in unordered_set\n";
        return false;
    }
    hashSet.insert(ANIME_STR("图层1"));
    hashSet.insert(ANIME_STR("😀"));
    if (hashSet.count(ANIME_STR("图层1")) != 1 || hashSet.count(ANIME_STR("😀")) != 1) {
        std::cerr << "FAIL: unordered_set lookup failed\n";
        return false;
    }

    // leftJustified / rightJustified / fromLocal8Bit / toLocal8Bit
    const char *justInputs[] = {"", "a", "abc", "图层12", "😀🎉"};
    for (const char *in : justInputs) {
        String s = String::fromUtf8(in);
        QString q = QString::fromUtf8(in);
        for (int width : {-2, 0, 1, 3, 5, 10}) {
            for (bool trunc : {false, true}) {
                String ljS = s.leftJustified(width, Char(u'_'), trunc);
                QString ljQ = q.leftJustified(width, QChar(u'_'), trunc);
                if (!sameContent(ljS, ljQ)) {
                    std::cerr << "FAIL: leftJustified(" << in << ", " << width << ", " << trunc << ")\n";
                    return false;
                }
                String rjS = s.rightJustified(width, Char(u'0'), trunc);
                QString rjQ = q.rightJustified(width, QChar(u'0'), trunc);
                if (!sameContent(rjS, rjQ)) {
                    std::cerr << "FAIL: rightJustified(" << in << ", " << width << ", " << trunc << ")\n";
                    return false;
                }
            }
        }
        String locS = String::fromLocal8Bit(in);
        if (locS.toLocal8Bit() != std::string(in)) {
            std::cerr << "FAIL: fromLocal8Bit/toLocal8Bit roundtrip\n";
            return false;
        }
    }

    // StringList differential tests against QStringList
    {
        using AnimeCore::StringList;
        StringList sl;
        QStringList qsl;
        if (sl.isEmpty() != qsl.isEmpty() || sl.size() != qsl.size()) {
            std::cerr << "FAIL: empty StringList\n";
            return false;
        }
        if (!sameContent(sl.join(ANIME_STR(",")), qsl.join(QStringLiteral(",")))) {
            std::cerr << "FAIL: empty StringList::join\n";
            return false;
        }

        sl << ANIME_STR("Beta") << ANIME_STR("alpha") << ANIME_STR("Gamma")
           << ANIME_STR("beta") << ANIME_STR("alpha") << ANIME_STR("图层");
        qsl << QStringLiteral("Beta") << QStringLiteral("alpha") << QStringLiteral("Gamma")
            << QStringLiteral("beta") << QStringLiteral("alpha") << QStringLiteral("图层");

        if (!sameContent(sl.join(ANIME_STR(" | ")), qsl.join(QStringLiteral(" | ")))
            || !sameContent(sl.join(Char(u'/')), qsl.join(QChar(u'/')))) {
            std::cerr << "FAIL: StringList::join mismatch\n";
            return false;
        }

        for (auto cs : {CaseSensitive, CaseInsensitive}) {
            Qt::CaseSensitivity qcs = (cs == CaseSensitive) ? Qt::CaseSensitive : Qt::CaseInsensitive;
            if (sl.contains(ANIME_STR("BETA"), cs) != qsl.contains(QStringLiteral("BETA"), qcs)
                || sl.contains(ANIME_STR("missing"), cs) != qsl.contains(QStringLiteral("missing"), qcs)) {
                std::cerr << "FAIL: StringList::contains mismatch\n";
                return false;
            }
            if (sl.indexOf(ANIME_STR("beta"), 0, cs) != qsl.indexOf(QStringLiteral("beta"), 0, qcs)
                || sl.lastIndexOf(ANIME_STR("alpha"), -1, cs) != qsl.lastIndexOf(QStringLiteral("alpha"), -1, qcs)) {
                std::cerr << "FAIL: StringList::indexOf/lastIndexOf mismatch\n";
                return false;
            }
            StringList fS = sl.filter(ANIME_STR("a"), cs);
            QStringList fQ = qsl.filter(QStringLiteral("a"), qcs);
            if (!sameContent(fS.join(ANIME_STR(",")), fQ.join(QStringLiteral(",")))) {
                std::cerr << "FAIL: StringList::filter mismatch\n";
                return false;
            }
        }

        for (int pos : {-2, 0, 1, 3, 10}) {
            for (int len : {-1, 0, 2, 10}) {
                StringList mS = sl.mid(pos, len);
                QStringList mQ = qsl.mid(pos, len);
                if (!sameContent(mS.join(ANIME_STR(",")), mQ.join(QStringLiteral(",")))) {
                    std::cerr << "FAIL: StringList::mid(" << pos << ", " << len << ") mismatch\n";
                    return false;
                }
            }
        }

        StringList dupS = sl;
        QStringList dupQ = qsl;
        if (dupS.removeDuplicates() != dupQ.removeDuplicates()
            || !sameContent(dupS.join(ANIME_STR(",")), dupQ.join(QStringLiteral(",")))) {
            std::cerr << "FAIL: StringList::removeDuplicates mismatch\n";
            return false;
        }

        StringList remS = sl;
        QStringList remQ = qsl;
        if (remS.removeAll(ANIME_STR("alpha")) != remQ.removeAll(QStringLiteral("alpha"))
            || remS.removeOne(ANIME_STR("beta")) != remQ.removeOne(QStringLiteral("beta"))
            || !sameContent(remS.join(ANIME_STR(",")), remQ.join(QStringLiteral(",")))) {
            std::cerr << "FAIL: StringList::removeAll/removeOne mismatch\n";
            return false;
        }

        for (auto cs : {CaseSensitive, CaseInsensitive}) {
            Qt::CaseSensitivity qcs = (cs == CaseSensitive) ? Qt::CaseSensitive : Qt::CaseInsensitive;
            StringList sortS = sl;
            QStringList sortQ = qsl;
            sortS.sort(cs);
            sortQ.sort(qcs);
            if (!sameContent(sortS.join(ANIME_STR(",")), sortQ.join(QStringLiteral(",")))) {
                std::cerr << "FAIL: StringList::sort mismatch\n";
                return false;
            }
        }

        // Bridge<QStringList> roundtrip and mixed String == QString
        QStringList bridgedQ = sl;
        StringList bridgedS = bridgedQ;
        if (bridgedS != sl || !(sl.first() == bridgedQ.first()) || (sl.first() != bridgedQ.first())) {
            std::cerr << "FAIL: Bridge<QStringList> or mixed String==QString mismatch\n";
            return false;
        }
    }

    std::map<String, int> orderedMap;
    orderedMap[ANIME_STR("b")] = 2;
    orderedMap[ANIME_STR("a")] = 1;
    if (orderedMap.begin()->first != ANIME_STR("a")) {
        std::cerr << "FAIL: std::map<String, int> ordering failed\n";
        return false;
    }

    return true;
}

} // namespace

int main()
{
    if (!testNullAndEmpty())
        return 1;
    if (!testImplicitSharingAndPerf())
        return 1;
    if (!testDifferentialRandomStrings())
        return 1;
    if (!testNumbersAndArg())
        return 1;
    return 0;
}
