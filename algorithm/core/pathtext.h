#pragma once

// Path text helpers shared by the portable file layer (file.cpp, fileinfo.cpp)
// and the Windows native layer. Internal to algorithm/core; nothing here touches
// the operating system.
//
// A path is read the same way on every system: both slashes separate, a leading
// "X:" is a drive and "//server/share" is a UNC root. A project written on
// Windows keeps its meaning when it is opened elsewhere.

#include "algorithm/core/string.h"

#include <cstddef>

namespace AnimeCore {
namespace PathText {

inline bool isSlash(Char c) noexcept
{
    return c == Char(u'/') || c == Char(u'\\');
}

inline bool isAsciiLetter(Char c) noexcept
{
    const char16_t u = c.unicode();
    return (u >= u'A' && u <= u'Z') || (u >= u'a' && u <= u'z');
}

inline bool hasDrivePrefix(const String &p) noexcept
{
    return p.size() >= 2 && isAsciiLetter(p.at(0)) && p.at(1) == Char(u':');
}

inline bool isUncPath(const String &p) noexcept
{
    return p.size() >= 2 && isSlash(p.at(0)) && isSlash(p.at(1));
}

inline String toForwardSlashes(const String &pathName)
{
    String out = pathName;
    out.replace(Char(u'\\'), Char(u'/'));
    return out;
}

// Length of the root prefix in a normalized (forward-slash) path:
// "C:/" -> 3, "C:" -> 2, "//server/share/" -> length up to slash after share, "/" -> 1, else 0.
inline std::ptrdiff_t rootPrefixLength(const String &normPath) noexcept
{
    if (normPath.isEmpty())
        return 0;
    if (hasDrivePrefix(normPath)) {
        if (normPath.size() >= 3 && normPath.at(2) == Char(u'/'))
            return 3;
        return 2;
    }
    if (normPath.size() >= 2 && normPath.at(0) == Char(u'/') && normPath.at(1) == Char(u'/')) {
        // UNC path //server/share
        std::ptrdiff_t i = 2;
        while (i < normPath.size() && normPath.at(i) == Char(u'/'))
            ++i;
        std::ptrdiff_t serverStart = i;
        while (i < normPath.size() && normPath.at(i) != Char(u'/'))
            ++i;
        if (i == serverStart)
            return 1;
        if (i < normPath.size()) {
            ++i; // skip '/'
            std::ptrdiff_t shareStart = i;
            while (i < normPath.size() && normPath.at(i) != Char(u'/'))
                ++i;
            if (i > shareStart) {
                if (i < normPath.size() && normPath.at(i) == Char(u'/'))
                    return i + 1;
                return i;
            }
        }
        return 2;
    }
    if (normPath.at(0) == Char(u'/'))
        return 1;
    return 0;
}

inline bool hasTrailingSeparatorNotRoot(const String &rawPath)
{
    if (rawPath.isEmpty())
        return false;
    const String norm = toForwardSlashes(rawPath);
    if (!norm.endsWith(Char(u'/')))
        return false;
    const std::ptrdiff_t rootLen = rootPrefixLength(norm);
    return norm.size() > rootLen;
}

} // namespace PathText
} // namespace AnimeCore
