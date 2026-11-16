// FileInfo, Dir and applicationDirPath. Path text is handled here, the same way
// on every system (see pathtext.h); what has to ask the system goes through
// native.h. File and SaveFile are in file.cpp.

#include "algorithm/core/file.h"

#include "algorithm/core/native.h"
#include "algorithm/core/pathtext.h"

#include <algorithm>
#include <cwctype>

namespace AnimeCore {

using PathText::hasDrivePrefix;
using PathText::hasTrailingSeparatorNotRoot;
using PathText::isSlash;
using PathText::isUncPath;
using PathText::rootPrefixLength;

namespace {

bool statPath(const String &path, Native::FileStatus *status)
{
    if (!Native::fileStatus(path, status))
        return false;
    // A regular file followed by a trailing slash (e.g. "file.txt/") is not a valid path.
    if (hasTrailingSeparatorNotRoot(path) && !status->isDir)
        return false;
    return true;
}

bool matchWildcardImpl(const char16_t *pat, std::ptrdiff_t patLen,
                       const char16_t *str, std::ptrdiff_t strLen,
                       bool caseSensitive) noexcept
{
    std::ptrdiff_t p = 0;
    std::ptrdiff_t s = 0;
    std::ptrdiff_t starIdx = -1;
    std::ptrdiff_t matchIdx = 0;

    auto charEq = [caseSensitive](char16_t a, char16_t b) noexcept {
        if (caseSensitive)
            return a == b;
        return std::towlower(static_cast<wint_t>(a))
            == std::towlower(static_cast<wint_t>(b));
    };

    while (s < strLen) {
        if (p < patLen && (pat[p] == u'?' || charEq(pat[p], str[s]))) {
            ++p;
            ++s;
        } else if (p < patLen && pat[p] == u'*') {
            starIdx = p;
            matchIdx = s;
            ++p;
        } else if (starIdx != -1) {
            p = starIdx + 1;
            ++matchIdx;
            s = matchIdx;
        } else {
            return false;
        }
    }
    while (p < patLen && pat[p] == u'*')
        ++p;
    return p == patLen;
}

bool matchesAnyFilter(const String &name, const StringList &filters, bool caseSensitive)
{
    for (const String &f : filters) {
        const String trimmed = f.trimmed();
        if (matchWildcardImpl(trimmed.utf16(), trimmed.size(), name.utf16(), name.size(), caseSensitive))
            return true;
    }
    return false;
}

// Whether a folder entry belongs in a listing made with these filters.
bool entryPassesFilters(const Native::DirEntry &entry, Dir::Filters filters, const StringList &nameFilters)
{
    const bool isDot = (entry.name == ANIME_STR("."));
    const bool isDotDot = (entry.name == ANIME_STR(".."));

    if (isDot && (filters & Dir::NoDot) != 0)
        return false;
    if (isDotDot && (filters & Dir::NoDotDot) != 0)
        return false;

    if (entry.isDir) {
        if ((filters & (Dir::Dirs | Dir::AllDirs)) == 0)
            return false;
    } else {
        if ((filters & Dir::Files) == 0)
            return false;
    }

    if (!isDot && !isDotDot) {
        if ((filters & Dir::Hidden) == 0 && entry.hidden)
            return false;
        if ((filters & Dir::System) == 0 && entry.system)
            return false;
    }

    if (!nameFilters.empty()) {
        const bool allDirs = (filters & Dir::AllDirs) != 0;
        if (!(entry.isDir && allDirs)) {
            const bool caseSensitive = (filters & Dir::CaseSensitive) != 0;
            if (!matchesAnyFilter(entry.name, nameFilters, caseSensitive))
                return false;
        }
    }
    return true;
}

int compareNames(const String &a, const String &b, bool ignoreCase) noexcept
{
    if (ignoreCase) {
        const std::ptrdiff_t minLen = std::min(a.size(), b.size());
        for (std::ptrdiff_t i = 0; i < minLen; ++i) {
            const wint_t ca = std::towlower(static_cast<wint_t>(a.at(i).unicode()));
            const wint_t cb = std::towlower(static_cast<wint_t>(b.at(i).unicode()));
            if (ca != cb)
                return ca < cb ? -1 : 1;
        }
        if (a.size() != b.size())
            return a.size() < b.size() ? -1 : 1;
    }
    if (a < b)
        return -1;
    if (b < a)
        return 1;
    return 0;
}

void sortEntries(std::vector<Native::DirEntry> &items, Dir::SortFlags sort)
{
    const int sortBy = sort & Dir::SortByMask;
    if (sortBy == Dir::Unsorted && (sort & (Dir::DirsFirst | Dir::DirsLast | Dir::Reversed)) == 0)
        return;

    const bool dirsFirst = (sort & Dir::DirsFirst) != 0;
    const bool dirsLast = (sort & Dir::DirsLast) != 0;
    const bool reversed = (sort & Dir::Reversed) != 0;
    const bool ignoreCase = (sort & Dir::IgnoreCase) != 0;

    std::stable_sort(items.begin(), items.end(), [&](const Native::DirEntry &a, const Native::DirEntry &b) {
        if (dirsFirst && a.isDir != b.isDir)
            return a.isDir;
        if (dirsLast && a.isDir != b.isDir)
            return !a.isDir;

        int cmp = 0;
        if (sortBy == Dir::Time) {
            if (a.modified != b.modified)
                cmp = (a.modified > b.modified) ? -1 : 1; // Qt Time sort: newest first
            else
                cmp = compareNames(a.name, b.name, ignoreCase);
        } else if (sortBy == Dir::Size) {
            if (a.size != b.size)
                cmp = (a.size > b.size) ? -1 : 1; // Qt Size sort: largest first
            else
                cmp = compareNames(a.name, b.name, ignoreCase);
        } else if (sortBy == Dir::Name) {
            cmp = compareNames(a.name, b.name, ignoreCase);
        }
        if (reversed)
            cmp = -cmp;
        return cmp < 0;
    });
}

} // namespace

// ============================================================================
// FileInfo
// ============================================================================

FileInfo::FileInfo(const String &file)
    : m_filePath(file)
{
}

FileInfo::FileInfo(const Dir &dir, const String &file)
    : m_filePath(dir.filePath(file))
{
}

void FileInfo::setFile(const String &file)
{
    m_filePath = file;
}

bool FileInfo::exists() const
{
    return exists(m_filePath);
}

bool FileInfo::exists(const String &file)
{
    Native::FileStatus status;
    return statPath(file, &status);
}

bool FileInfo::isFile() const
{
    Native::FileStatus status;
    return statPath(m_filePath, &status) && !status.isDir;
}

bool FileInfo::isDir() const
{
    Native::FileStatus status;
    return statPath(m_filePath, &status) && status.isDir;
}

bool FileInfo::isRelative() const
{
    if (m_filePath.isEmpty())
        return true;
    if (isUncPath(m_filePath))
        return false;
    if (hasDrivePrefix(m_filePath)) {
        return !(m_filePath.size() >= 3 && isSlash(m_filePath.at(2)));
    }
    if (isSlash(m_filePath.at(0)))
        return false;
    return true;
}

String FileInfo::fileName() const
{
    if (m_filePath.isEmpty())
        return String();

    const String norm = Dir::fromNativeSeparators(m_filePath);
    if (hasDrivePrefix(norm) && norm.size() == 2)
        return String();

    const std::ptrdiff_t rootLen = rootPrefixLength(norm);
    if (norm.size() <= rootLen) {
        // Root itself ("/", "C:/", "//server/share/"): fileName is empty.
        return String();
    }

    const std::ptrdiff_t slashPos = norm.lastIndexOf(Char(u'/'));
    if (slashPos == -1) {
        if (hasDrivePrefix(norm))
            return norm.mid(2);
        return norm;
    }
    return norm.mid(slashPos + 1);
}

String FileInfo::baseName() const
{
    const String name = fileName();
    if (name.isEmpty())
        return String();
    const std::ptrdiff_t dot = name.indexOf(Char(u'.'));
    if (dot == -1)
        return name;
    return name.left(dot);
}

String FileInfo::completeBaseName() const
{
    const String name = fileName();
    if (name.isEmpty())
        return String();
    const std::ptrdiff_t dot = name.lastIndexOf(Char(u'.'));
    if (dot == -1)
        return name;
    return name.left(dot);
}

String FileInfo::suffix() const
{
    const String name = fileName();
    if (name.isEmpty())
        return String();
    const std::ptrdiff_t dot = name.lastIndexOf(Char(u'.'));
    if (dot == -1)
        return String();
    return name.mid(dot + 1);
}

String FileInfo::completeSuffix() const
{
    const String name = fileName();
    if (name.isEmpty())
        return String();
    const std::ptrdiff_t dot = name.indexOf(Char(u'.'));
    if (dot == -1)
        return String();
    return name.mid(dot + 1);
}

String FileInfo::path() const
{
    if (m_filePath.isEmpty())
        return String();

    const String norm = Dir::fromNativeSeparators(m_filePath);
    const std::ptrdiff_t rootLen = rootPrefixLength(norm);
    if (norm.size() <= rootLen) {
        return norm;
    }

    const std::ptrdiff_t slashPos = norm.lastIndexOf(Char(u'/'));
    if (slashPos == -1) {
        if (hasDrivePrefix(norm))
            return norm.left(2);
        return ANIME_STR(".");
    }
    if (slashPos < rootLen) {
        return norm.left(rootLen);
    }
    return norm.left(slashPos);
}

String FileInfo::absoluteFilePath() const
{
    if (m_filePath.isEmpty())
        return String();

    const String norm = Dir::fromNativeSeparators(m_filePath);
    const bool relative = isRelative();
    const bool trailingSlash = !relative && hasTrailingSeparatorNotRoot(norm);

    String combined;
    if (!relative) {
        if (norm.startsWith(Char(u'/')) && !norm.startsWith(ANIME_STR("//"))) {
            // Root-relative path like "/foo": where the current folder has a
            // drive (Windows) it goes on that drive, elsewhere it is already complete.
            const String cur = Dir::currentPath();
            if (hasDrivePrefix(cur))
                combined = cur.left(2) + norm;
            else
                combined = norm;
        } else {
            combined = norm;
        }
    } else if (hasDrivePrefix(norm)) {
        // Drive-relative like "C:foo"
        const String cur = Dir::currentPath();
        if (hasDrivePrefix(cur) && std::towupper(cur.at(0).unicode()) == std::towupper(norm.at(0).unicode())) {
            combined = cur + ANIME_STR("/") + norm.mid(2);
        } else {
            combined = norm.left(2) + ANIME_STR("/") + norm.mid(2);
        }
    } else {
        combined = Dir::currentPath() + ANIME_STR("/") + norm;
    }

    String cleaned = Dir::cleanPath(combined);
    if (trailingSlash && !cleaned.endsWith(Char(u'/')))
        cleaned.append(Char(u'/'));
    return cleaned;
}

String FileInfo::absolutePath() const
{
    if (m_filePath.isEmpty())
        return String();

    const String absFile = absoluteFilePath();
    const FileInfo absInfo(absFile);
    return Dir::cleanPath(absInfo.path());
}

Dir FileInfo::dir() const
{
    return Dir(path());
}

Dir FileInfo::absoluteDir() const
{
    return Dir(absolutePath());
}

std::int64_t FileInfo::size() const
{
    Native::FileStatus status;
    if (!statPath(m_filePath, &status) || status.isDir)
        return 0;
    return static_cast<std::int64_t>(status.size);
}

// ============================================================================
// Dir
// ============================================================================

Dir::Dir(const String &path)
{
    setPath(path);
}

void Dir::setPath(const String &path)
{
    if (path.isEmpty()) {
        m_path = ANIME_STR(".");
        return;
    }
    String norm = fromNativeSeparators(path);
    const std::ptrdiff_t rootLen = rootPrefixLength(norm);
    while (norm.size() > rootLen && norm.endsWith(Char(u'/')))
        norm.chop(1);
    m_path = norm;
}

String Dir::absolutePath() const
{
    if (m_path.isEmpty() || m_path == ANIME_STR("."))
        return currentPath();
    return cleanPath(FileInfo(m_path).absoluteFilePath());
}

String Dir::dirName() const
{
    const String cleaned = (m_path == ANIME_STR(".")) ? m_path : cleanPath(m_path);
    const std::ptrdiff_t pos = cleaned.lastIndexOf(Char(u'/'));
    if (pos == -1)
        return cleaned;
    return cleaned.mid(pos + 1);
}

String Dir::filePath(const String &fileName) const
{
    const String normFile = fromNativeSeparators(fileName);
    if (!FileInfo(normFile).isRelative())
        return fileName;

    if (m_path.isEmpty())
        return fileName;

    if (m_path.endsWith(Char(u'/')) || m_path.endsWith(Char(u'\\')))
        return m_path + fileName;
    return m_path + ANIME_STR("/") + fileName;
}

String Dir::absoluteFilePath(const String &fileName) const
{
    const String normFile = fromNativeSeparators(fileName);
    if (!FileInfo(normFile).isRelative())
        return FileInfo(fileName).absoluteFilePath();

    const String base = absolutePath();
    if (fileName.isEmpty())
        return base;
    if (base.endsWith(Char(u'/')))
        return base + normFile;
    return base + ANIME_STR("/") + normFile;
}

bool Dir::exists() const
{
    return FileInfo(m_path.isEmpty() ? ANIME_STR(".") : m_path).isDir();
}

bool Dir::exists(const String &name) const
{
    if (name.isEmpty())
        return false;
    return FileInfo(filePath(name)).exists();
}

bool Dir::mkdir(const String &dirName) const
{
    if (dirName.isEmpty())
        return false;
    return Native::makeDirectory(filePath(dirName)) == Native::MakeDirResult::Created;
}

bool Dir::mkpath(const String &dirPath) const
{
    if (dirPath.isEmpty())
        return false;

    const String target = FileInfo(filePath(dirPath)).absoluteFilePath();
    const String cleaned = cleanPath(target);
    if (cleaned.isEmpty())
        return false;

    if (FileInfo(cleaned).isDir())
        return true;
    if (FileInfo(cleaned).exists())
        return false;

    const std::ptrdiff_t rootLen = rootPrefixLength(cleaned);
    std::ptrdiff_t pos = rootLen;
    while (pos <= cleaned.size()) {
        std::ptrdiff_t nextSlash = cleaned.indexOf(Char(u'/'), pos);
        if (nextSlash == -1)
            nextSlash = cleaned.size();

        const String prefix = cleaned.left(nextSlash);
        if (!prefix.isEmpty() && prefix.size() > rootLen && !FileInfo(prefix).isDir()) {
            // Someone else may make the same folder at the same moment; that is fine.
            const Native::MakeDirResult made = Native::makeDirectory(prefix);
            if (made == Native::MakeDirResult::Failed)
                return false;
            if (made == Native::MakeDirResult::AlreadyExists && !FileInfo(prefix).isDir())
                return false;
        }
        if (nextSlash == cleaned.size())
            break;
        pos = nextSlash + 1;
    }

    return FileInfo(cleaned).isDir();
}

bool Dir::cdUp()
{
    const String abs = absolutePath();
    const std::ptrdiff_t rootLen = rootPrefixLength(abs);
    if (abs.size() <= rootLen)
        return false;
    const String parent = FileInfo(abs).path();
    if (parent.isEmpty() || !FileInfo(parent).isDir())
        return false;
    m_path = parent;
    return true;
}

StringList Dir::entryList(Filters filters, SortFlags sort) const
{
    return entryList(StringList(), filters, sort);
}

StringList Dir::entryList(const StringList &nameFilters, Filters filters, SortFlags sort) const
{
    const String targetDir = m_path.isEmpty() ? ANIME_STR(".") : m_path;
    if (!FileInfo(targetDir).isDir())
        return StringList();

    const Filters effFilters = (filters == NoFilter) ? AllEntries : filters;
    const SortFlags effSort = (sort == NoSort) ? SortFlags(Name) : sort;

    std::vector<Native::DirEntry> all;
    if (!Native::listDirectory(targetDir, &all))
        return StringList();

    std::vector<Native::DirEntry> items;
    items.reserve(all.size());
    for (Native::DirEntry &entry : all) {
        if (entryPassesFilters(entry, effFilters, nameFilters))
            items.push_back(std::move(entry));
    }

    sortEntries(items, effSort);

    StringList result;
    result.reserve(items.size());
    for (Native::DirEntry &item : items)
        result.push_back(std::move(item.name));
    return result;
}

String Dir::toNativeSeparators(const String &pathName)
{
    if (separator() == Char(u'/'))
        return pathName;
    String out = pathName;
    out.replace(Char(u'/'), separator());
    return out;
}

String Dir::fromNativeSeparators(const String &pathName)
{
    return PathText::toForwardSlashes(pathName);
}

String Dir::cleanPath(const String &path)
{
    if (path.isEmpty())
        return String();

    const String norm = fromNativeSeparators(path);
    String prefix;
    std::ptrdiff_t start = 0;

    if (norm.size() >= 2 && norm.at(0) == Char(u'/') && norm.at(1) == Char(u'/')
        && (norm.size() == 2 || norm.at(2) != Char(u'/'))) {
        // UNC path //server/share
        std::ptrdiff_t s1 = norm.indexOf(Char(u'/'), 2);
        if (s1 == -1) {
            return norm;
        }
        std::ptrdiff_t s2 = norm.indexOf(Char(u'/'), s1 + 1);
        if (s2 == -1) {
            return norm;
        }
        prefix = norm.left(s2 + 1);
        start = s2 + 1;
    } else if (hasDrivePrefix(norm)) {
        if (norm.size() >= 3 && norm.at(2) == Char(u'/')) {
            prefix = norm.left(3);
            start = 3;
        } else {
            prefix = norm.left(2);
            start = 2;
        }
    } else if (norm.startsWith(Char(u'/'))) {
        prefix = ANIME_STR("/");
        start = 1;
    }

    std::vector<String> stack;
    std::ptrdiff_t i = start;
    while (i < norm.size()) {
        while (i < norm.size() && norm.at(i) == Char(u'/'))
            ++i;
        if (i >= norm.size())
            break;
        std::ptrdiff_t j = i;
        while (j < norm.size() && norm.at(j) != Char(u'/'))
            ++j;
        const String seg = norm.mid(i, j - i);
        i = j;

        if (seg == ANIME_STR(".")) {
            continue;
        } else if (seg == ANIME_STR("..")) {
            if (!stack.empty() && stack.back() != ANIME_STR("..")) {
                stack.pop_back();
            } else {
                stack.push_back(seg);
            }
        } else {
            stack.push_back(seg);
        }
    }

    String out = prefix;
    for (std::size_t k = 0; k < stack.size(); ++k) {
        if (k > 0)
            out.append(Char(u'/'));
        out.append(stack[k]);
    }

    if (out.isEmpty())
        return ANIME_STR(".");
    return out;
}

String Dir::currentPath()
{
    return cleanPath(Native::currentDirectory());
}

String Dir::tempPath()
{
    return cleanPath(Native::tempDirectory());
}

// ============================================================================
// applicationDirPath
// ============================================================================

String applicationDirPath()
{
    const String exePath = Dir::fromNativeSeparators(Native::executablePath());
    if (exePath.isEmpty())
        return String();
    return FileInfo(exePath).absolutePath();
}

} // namespace AnimeCore
