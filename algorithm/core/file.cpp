#include "algorithm/core/file.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cstring>
#include <cwctype>

namespace AnimeCore {

namespace {

bool isSlash(Char c) noexcept
{
    return c == Char(u'/') || c == Char(u'\\');
}

bool isAsciiLetter(Char c) noexcept
{
    const char16_t u = c.unicode();
    return (u >= u'A' && u <= u'Z') || (u >= u'a' && u <= u'z');
}

bool hasDrivePrefix(const String &p) noexcept
{
    return p.size() >= 2 && isAsciiLetter(p.at(0)) && p.at(1) == Char(u':');
}

bool isUncPath(const String &p) noexcept
{
    return p.size() >= 2 && isSlash(p.at(0)) && isSlash(p.at(1));
}

// Length of the root prefix in a normalized (forward-slash) path:
// "C:/" -> 3, "C:" -> 2, "//server/share/" -> length up to slash after share, "/" -> 1, else 0.
std::ptrdiff_t rootPrefixLength(const String &normPath) noexcept
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

bool hasTrailingSeparatorNotRoot(const String &rawPath) noexcept
{
    if (rawPath.isEmpty())
        return false;
    const String norm = Dir::fromNativeSeparators(rawPath);
    if (!norm.endsWith(Char(u'/')))
        return false;
    const std::ptrdiff_t rootLen = rootPrefixLength(norm);
    return norm.size() > rootLen;
}

#ifdef _WIN32

std::wstring stringToWString(const String &s)
{
    if (s.isEmpty())
        return std::wstring();
    return std::wstring(reinterpret_cast<const wchar_t *>(s.utf16()),
                        static_cast<std::size_t>(s.size()));
}

String wstringToString(const wchar_t *w, int len = -1)
{
    if (!w)
        return String();
    return String::fromUtf16(reinterpret_cast<const char16_t *>(w), len);
}

String win32ErrorString(DWORD err)
{
    if (err == 0)
        return ANIME_STR("No error");
    wchar_t *buf = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD len = FormatMessageW(flags, nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                     reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    if (len == 0 || !buf)
        return ANIME_STR("OS error ") + String::number(static_cast<unsigned long long>(err));
    String result = wstringToString(buf, static_cast<int>(len)).trimmed();
    LocalFree(buf);
    return result;
}

// Convert a String path to a Win32 wide path that supports long paths (> 240 chars) via \\?\ prefix.
std::wstring toWin32Path(const String &path)
{
    if (path.isEmpty())
        return std::wstring();

    std::wstring raw = stringToWString(path);
    if (raw.rfind(L"\\\\?\\", 0) == 0) {
        std::replace(raw.begin(), raw.end(), L'/', L'\\');
        return raw;
    }

    const bool hadTrailingSlash = hasTrailingSeparatorNotRoot(path);

    // Resolve relative segments via GetFullPathNameW so \\?\ can be applied safely.
    DWORD needed = GetFullPathNameW(raw.c_str(), 0, nullptr, nullptr);
    std::wstring full;
    if (needed > 0) {
        full.resize(needed + 1);
        DWORD written = GetFullPathNameW(raw.c_str(), needed + 1, full.data(), nullptr);
        if (written > 0 && written <= needed) {
            full.resize(written);
        } else {
            full.clear();
        }
    }

    if (full.empty()) {
        // Fallback when path already exceeds MAX_PATH before GetFullPathNameW on older systems:
        // clean via Dir::cleanPath against currentPath if relative.
        String abs = path;
        if (FileInfo(path).isRelative())
            abs = Dir::currentPath() + ANIME_STR("/") + path;
        String cleaned = Dir::cleanPath(abs);
        if (hadTrailingSlash && !cleaned.endsWith(Char(u'/')))
            cleaned.append(Char(u'/'));
        full = stringToWString(Dir::toNativeSeparators(cleaned));
    } else {
        if (hadTrailingSlash && !full.empty() && full.back() != L'\\')
            full.push_back(L'\\');
    }

    // Apply \\?\ extended-length prefix for long paths (CreateDirectoryW limit is 248).
    if (full.size() >= 240 && full.rfind(L"\\\\?\\", 0) != 0) {
        if (full.rfind(L"\\\\", 0) == 0) {
            full = L"\\\\?\\UNC\\" + full.substr(2);
        } else if (full.size() >= 2 && full[1] == L':') {
            full = L"\\\\?\\" + full;
        }
    }
    return full;
}

bool queryWin32Attributes(const String &path, WIN32_FILE_ATTRIBUTE_DATA *outData)
{
    if (path.isEmpty())
        return false;
    const std::wstring wpath = toWin32Path(path);
    if (wpath.empty())
        return false;
    if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, outData))
        return false;
    // A regular file followed by a trailing slash (e.g. "file.txt/") is not a valid path.
    if (hasTrailingSeparatorNotRoot(path) && (outData->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return false;
    return true;
}

#endif // _WIN32

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

} // namespace

// ============================================================================
// File
// ============================================================================

File::File(const String &name)
    : m_fileName(name)
{
}

File::~File()
{
    close();
}

File::File(File &&other) noexcept
    : m_fileName(std::move(other.m_fileName))
    , m_errorString(std::move(other.m_errorString))
    , m_handle(other.m_handle)
    , m_isOpen(other.m_isOpen)
    , m_readable(other.m_readable)
    , m_writable(other.m_writable)
    , m_textMode(other.m_textMode)
{
    other.m_handle = nullptr;
    other.m_isOpen = false;
    other.m_readable = false;
    other.m_writable = false;
    other.m_textMode = false;
}

File &File::operator=(File &&other) noexcept
{
    if (this != &other) {
        close();
        m_fileName = std::move(other.m_fileName);
        m_errorString = std::move(other.m_errorString);
        m_handle = other.m_handle;
        m_isOpen = other.m_isOpen;
        m_readable = other.m_readable;
        m_writable = other.m_writable;
        m_textMode = other.m_textMode;
        other.m_handle = nullptr;
        other.m_isOpen = false;
        other.m_readable = false;
        other.m_writable = false;
        other.m_textMode = false;
    }
    return *this;
}

void File::setFileName(const String &name)
{
    if (m_isOpen)
        close();
    m_fileName = name;
}

bool File::exists() const
{
    return exists(m_fileName);
}

bool File::exists(const String &fileName)
{
    return FileInfo::exists(fileName);
}

bool File::remove()
{
    if (m_fileName.isEmpty()) {
        m_errorString = ANIME_STR("No file name specified");
        return false;
    }
    close();
#ifdef _WIN32
    const std::wstring wpath = toWin32Path(m_fileName);
    if (DeleteFileW(wpath.c_str())) {
        m_errorString = ANIME_STR("No error");
        return true;
    }
    m_errorString = win32ErrorString(GetLastError());
    return false;
#else
    return false;
#endif
}

bool File::remove(const String &fileName)
{
    File f(fileName);
    return f.remove();
}

bool File::open(OpenMode mode)
{
    if (m_isOpen)
        close();

    if (m_fileName.isEmpty()) {
        m_errorString = ANIME_STR("No file name specified");
        return false;
    }

    const bool read = (mode & ReadOnly) != 0;
    const bool write = (mode & WriteOnly) != 0 || (mode & Append) != 0;
    if (!read && !write) {
        m_errorString = ANIME_STR("Open mode not specified");
        return false;
    }
    if ((mode & NewOnly) && (mode & ExistingOnly)) {
        m_errorString = ANIME_STR("Invalid open mode");
        return false;
    }

#ifdef _WIN32
    DWORD access = 0;
    if (read)
        access |= GENERIC_READ;
    if (write)
        access |= GENERIC_WRITE;

    DWORD creation = OPEN_EXISTING;
    if (mode & NewOnly) {
        creation = CREATE_NEW;
    } else if (mode & ExistingOnly) {
        creation = ((mode & Truncate) && write) ? TRUNCATE_EXISTING : OPEN_EXISTING;
    } else if (write) {
        if ((mode & Truncate) || (!read && !(mode & Append))) {
            creation = CREATE_ALWAYS;
        } else {
            creation = OPEN_ALWAYS;
        }
    } else {
        creation = OPEN_EXISTING;
    }

    const std::wstring wpath = toWin32Path(m_fileName);
    const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE h = CreateFileW(wpath.c_str(), access, share, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        m_errorString = win32ErrorString(GetLastError());
        return false;
    }

    if (mode & Append) {
        LARGE_INTEGER zero{};
        SetFilePointerEx(h, zero, nullptr, FILE_END);
    }

    m_handle = reinterpret_cast<void *>(h);
    m_isOpen = true;
    m_readable = read;
    m_writable = write;
    m_textMode = (mode & Text) != 0;
    m_errorString = ANIME_STR("No error");
    return true;
#else
    return false;
#endif
}

void File::close()
{
    if (!m_isOpen)
        return;
#ifdef _WIN32
    if (m_handle) {
        CloseHandle(reinterpret_cast<HANDLE>(m_handle));
        m_handle = nullptr;
    }
#endif
    m_isOpen = false;
    m_readable = false;
    m_writable = false;
    m_textMode = false;
}

std::int64_t File::size() const
{
#ifdef _WIN32
    if (m_isOpen && m_handle) {
        LARGE_INTEGER sz{};
        if (GetFileSizeEx(reinterpret_cast<HANDLE>(m_handle), &sz))
            return static_cast<std::int64_t>(sz.QuadPart);
    }
#endif
    return FileInfo(m_fileName).size();
}

std::string File::readAll()
{
    if (!m_isOpen || !m_readable) {
        m_errorString = ANIME_STR("Device not open for reading");
        return std::string();
    }
#ifdef _WIN32
    HANDLE h = reinterpret_cast<HANDLE>(m_handle);
    std::string raw;
    LARGE_INTEGER fileSize{};
    if (GetFileSizeEx(h, &fileSize) && fileSize.QuadPart > 0) {
        LARGE_INTEGER curPos{};
        LARGE_INTEGER zero{};
        if (SetFilePointerEx(h, zero, &curPos, FILE_CURRENT) && fileSize.QuadPart > curPos.QuadPart) {
            raw.reserve(static_cast<std::size_t>(fileSize.QuadPart - curPos.QuadPart));
        }
    }

    char buf[65536];
    for (;;) {
        DWORD bytesRead = 0;
        if (!ReadFile(h, buf, sizeof(buf), &bytesRead, nullptr)) {
            m_errorString = win32ErrorString(GetLastError());
            break;
        }
        if (bytesRead == 0)
            break;
        raw.append(buf, bytesRead);
    }

    if (m_textMode && !raw.empty()) {
        std::string filtered;
        filtered.reserve(raw.size());
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n')
                continue;
            filtered.push_back(raw[i]);
        }
        return filtered;
    }
    return raw;
#else
    return std::string();
#endif
}

std::int64_t File::write(const char *data, std::int64_t len)
{
    if (!m_isOpen || !m_writable) {
        m_errorString = ANIME_STR("Device not open for writing");
        return -1;
    }
    if (len <= 0 || !data)
        return 0;

#ifdef _WIN32
    HANDLE h = reinterpret_cast<HANDLE>(m_handle);
    std::string textBuf;
    const char *ptr = data;
    std::size_t remaining = static_cast<std::size_t>(len);

    if (m_textMode) {
        textBuf.reserve(remaining + remaining / 16);
        for (std::size_t i = 0; i < remaining; ++i) {
            if (data[i] == '\n')
                textBuf.push_back('\r');
            textBuf.push_back(data[i]);
        }
        ptr = textBuf.data();
        remaining = textBuf.size();
    }

    while (remaining > 0) {
        const DWORD chunk = remaining > 0x40000000u ? 0x40000000u : static_cast<DWORD>(remaining);
        DWORD written = 0;
        if (!WriteFile(h, ptr, chunk, &written, nullptr)) {
            m_errorString = win32ErrorString(GetLastError());
            return -1;
        }
        ptr += written;
        remaining -= written;
    }
    return len;
#else
    return -1;
#endif
}

std::int64_t File::write(const char *data)
{
    if (!data)
        return 0;
    return write(data, static_cast<std::int64_t>(std::strlen(data)));
}

std::int64_t File::write(const std::string &data)
{
    return write(data.data(), static_cast<std::int64_t>(data.size()));
}

std::int64_t File::write(const std::vector<unsigned char> &data)
{
    return write(reinterpret_cast<const char *>(data.data()), static_cast<std::int64_t>(data.size()));
}

// ============================================================================
// SaveFile
// ============================================================================

SaveFile::SaveFile(const String &name)
    : m_fileName(name)
{
}

SaveFile::~SaveFile()
{
    discardTempFile();
}

void SaveFile::setFileName(const String &name)
{
    discardTempFile();
    m_fileName = name;
}

bool SaveFile::open(OpenMode mode)
{
    discardTempFile();
    m_writeError = false;
    m_cancelled = false;

    if (m_fileName.isEmpty()) {
        m_errorString = ANIME_STR("No file name specified");
        return false;
    }
    if ((mode & ReadOnly) != 0 || (mode & WriteOnly) == 0) {
        m_errorString = ANIME_STR("SaveFile requires WriteOnly mode without ReadOnly");
        return false;
    }

    const FileInfo targetInfo(m_fileName);
    if (targetInfo.isDir() || hasTrailingSeparatorNotRoot(m_fileName)) {
        m_errorString = ANIME_STR("Target path is a directory");
        return false;
    }

    const String parentDir = targetInfo.absolutePath();
    if (!FileInfo(parentDir).isDir()) {
        m_errorString = ANIME_STR("Parent directory does not exist");
        return false;
    }

#ifdef _WIN32
    const String absTarget = targetInfo.absoluteFilePath();
    const DWORD pid = GetCurrentProcessId();
    const ULONGLONG tick = GetTickCount64();

    HANDLE h = INVALID_HANDLE_VALUE;
    String chosenTemp;
    for (int attempt = 0; attempt < 64; ++attempt) {
        String candidate = absTarget + ANIME_STR(".tmp.")
            + String::number(static_cast<unsigned long long>(pid)) + ANIME_STR(".")
            + String::number(static_cast<unsigned long long>(tick + attempt));
        const std::wstring wtemp = toWin32Path(candidate);
        h = CreateFileW(wtemp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            chosenTemp = candidate;
            break;
        }
        const DWORD err = GetLastError();
        if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS) {
            m_errorString = win32ErrorString(err);
            return false;
        }
    }

    if (h == INVALID_HANDLE_VALUE) {
        m_errorString = ANIME_STR("Failed to create temporary file");
        return false;
    }

    m_handle = reinterpret_cast<void *>(h);
    m_tempFileName = chosenTemp;
    m_isOpen = true;
    m_textMode = (mode & Text) != 0;
    m_errorString = ANIME_STR("No error");
    return true;
#else
    return false;
#endif
}

std::int64_t SaveFile::write(const char *data, std::int64_t len)
{
    if (!m_isOpen || !m_handle) {
        m_errorString = ANIME_STR("SaveFile is not open");
        return -1;
    }
    if (m_cancelled) {
        m_errorString = ANIME_STR("Writing was cancelled");
        return -1;
    }
    if (len <= 0 || !data)
        return 0;

#ifdef _WIN32
    HANDLE h = reinterpret_cast<HANDLE>(m_handle);
    std::string textBuf;
    const char *ptr = data;
    std::size_t remaining = static_cast<std::size_t>(len);

    if (m_textMode) {
        textBuf.reserve(remaining + remaining / 16);
        for (std::size_t i = 0; i < remaining; ++i) {
            if (data[i] == '\n')
                textBuf.push_back('\r');
            textBuf.push_back(data[i]);
        }
        ptr = textBuf.data();
        remaining = textBuf.size();
    }

    while (remaining > 0) {
        const DWORD chunk = remaining > 0x40000000u ? 0x40000000u : static_cast<DWORD>(remaining);
        DWORD written = 0;
        if (!WriteFile(h, ptr, chunk, &written, nullptr)) {
            m_writeError = true;
            m_errorString = win32ErrorString(GetLastError());
            return -1;
        }
        ptr += written;
        remaining -= written;
    }
    return len;
#else
    return -1;
#endif
}

std::int64_t SaveFile::write(const char *data)
{
    if (!data)
        return 0;
    return write(data, static_cast<std::int64_t>(std::strlen(data)));
}

std::int64_t SaveFile::write(const std::string &data)
{
    return write(data.data(), static_cast<std::int64_t>(data.size()));
}

std::int64_t SaveFile::write(const std::vector<unsigned char> &data)
{
    return write(reinterpret_cast<const char *>(data.data()), static_cast<std::int64_t>(data.size()));
}

void SaveFile::cancelWriting()
{
    if (!m_isOpen)
        return;
    m_cancelled = true;
    m_errorString = ANIME_STR("Writing was cancelled");
}

bool SaveFile::commit()
{
    if (!m_isOpen || !m_handle) {
        return false;
    }

#ifdef _WIN32
    HANDLE h = reinterpret_cast<HANDLE>(m_handle);
    const bool flushOk = FlushFileBuffers(h) != 0;
    const DWORD flushErr = flushOk ? 0 : GetLastError();
    CloseHandle(h);
    m_handle = nullptr;
    m_isOpen = false;

    const std::wstring wtemp = toWin32Path(m_tempFileName);
    if (m_cancelled || m_writeError || !flushOk) {
        if (!flushOk && !m_cancelled && !m_writeError)
            m_errorString = win32ErrorString(flushErr);
        DeleteFileW(wtemp.c_str());
        m_tempFileName.clear();
        return false;
    }

    const std::wstring wtarget = toWin32Path(m_fileName);
    if (!MoveFileExW(wtemp.c_str(), wtarget.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        m_errorString = win32ErrorString(GetLastError());
        DeleteFileW(wtemp.c_str());
        m_tempFileName.clear();
        return false;
    }

    m_tempFileName.clear();
    m_errorString = ANIME_STR("No error");
    return true;
#else
    return false;
#endif
}

void SaveFile::discardTempFile()
{
#ifdef _WIN32
    if (m_handle) {
        CloseHandle(reinterpret_cast<HANDLE>(m_handle));
        m_handle = nullptr;
    }
    if (!m_tempFileName.isEmpty()) {
        const std::wstring wtemp = toWin32Path(m_tempFileName);
        DeleteFileW(wtemp.c_str());
        m_tempFileName.clear();
    }
#endif
    m_isOpen = false;
    m_textMode = false;
}

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
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    return queryWin32Attributes(file, &fad);
#else
    return false;
#endif
}

bool FileInfo::isFile() const
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!queryWin32Attributes(m_filePath, &fad))
        return false;
    return (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    return false;
#endif
}

bool FileInfo::isDir() const
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!queryWin32Attributes(m_filePath, &fad))
        return false;
    return (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    return false;
#endif
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
            // Root-relative path on Windows like "/foo" -> prepend current drive "C:"
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
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!queryWin32Attributes(m_filePath, &fad))
        return 0;
    if ((fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return 0;
    ULARGE_INTEGER sz{};
    sz.LowPart = fad.nFileSizeLow;
    sz.HighPart = fad.nFileSizeHigh;
    return static_cast<std::int64_t>(sz.QuadPart);
#else
    return 0;
#endif
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
#ifdef _WIN32
    const String target = filePath(dirName);
    const std::wstring wpath = toWin32Path(target);
    return CreateDirectoryW(wpath.c_str(), nullptr) != 0;
#else
    return false;
#endif
}

bool Dir::mkpath(const String &dirPath) const
{
    if (dirPath.isEmpty())
        return false;

#ifdef _WIN32
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
        if (!prefix.isEmpty() && prefix.size() > rootLen) {
            if (!FileInfo(prefix).isDir()) {
                const std::wstring wprefix = toWin32Path(prefix);
                if (!CreateDirectoryW(wprefix.c_str(), nullptr)) {
                    const DWORD err = GetLastError();
                    if (err != ERROR_ALREADY_EXISTS || !FileInfo(prefix).isDir())
                        return false;
                }
            }
        }
        if (nextSlash == cleaned.size())
            break;
        pos = nextSlash + 1;
    }

    return FileInfo(cleaned).isDir();
#else
    return false;
#endif
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
#ifdef _WIN32
    const String targetDir = m_path.isEmpty() ? ANIME_STR(".") : m_path;
    if (!FileInfo(targetDir).isDir())
        return StringList();

    const Filters effFilters = (filters == NoFilter) ? AllEntries : filters;
    const SortFlags effSort = (sort == NoSort) ? SortFlags(Name) : sort;

    const bool wantDirs = (effFilters & (Dirs | AllDirs)) != 0;
    const bool wantFiles = (effFilters & Files) != 0;
    const bool wantHidden = (effFilters & Hidden) != 0;
    const bool wantSystem = (effFilters & System) != 0;
    const bool noDot = (effFilters & NoDot) != 0;
    const bool noDotDot = (effFilters & NoDotDot) != 0;
    const bool allDirs = (effFilters & AllDirs) != 0;
    const bool caseSensitiveFilter = (effFilters & CaseSensitive) != 0;
    const bool hasNameFilters = !nameFilters.empty();

    std::wstring searchPattern = toWin32Path(targetDir);
    if (!searchPattern.empty() && searchPattern.back() != L'\\')
        searchPattern.push_back(L'\\');
    searchPattern.push_back(L'*');

    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return StringList();

    struct EntryItem {
        String name;
        bool isDir = false;
        std::uint64_t size = 0;
         std::uint64_t mtime = 0;
    };
    std::vector<EntryItem> items;

    do {
        const String name = wstringToString(fd.cFileName);
        const bool isDot = (name == ANIME_STR("."));
        const bool isDotDot = (name == ANIME_STR(".."));
        const bool isDirectory = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (isDot && noDot)
            continue;
        if (isDotDot && noDotDot)
            continue;

        if (isDirectory) {
            if (!wantDirs)
                continue;
        } else {
            if (!wantFiles)
                continue;
        }

        if (!isDot && !isDotDot) {
            if (!wantHidden && (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0)
                continue;
            if (!wantSystem && (fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0)
                continue;
        }

        if (hasNameFilters) {
            if (!(isDirectory && allDirs)) {
                if (!matchesAnyFilter(name, nameFilters, caseSensitiveFilter))
                    continue;
            }
        }

        ULARGE_INTEGER fsize{};
        fsize.LowPart = fd.nFileSizeLow;
        fsize.HighPart = fd.nFileSizeHigh;

        ULARGE_INTEGER fmtime{};
        fmtime.LowPart = fd.ftLastWriteTime.dwLowDateTime;
        fmtime.HighPart = fd.ftLastWriteTime.dwHighDateTime;

        items.push_back(EntryItem{name, isDirectory, fsize.QuadPart, fmtime.QuadPart});
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);

    const int sortBy = effSort & SortByMask;
    if (sortBy != Unsorted || (effSort & (DirsFirst | DirsLast | Reversed)) != 0) {
        const bool dirsFirst = (effSort & DirsFirst) != 0;
        const bool dirsLast = (effSort & DirsLast) != 0;
        const bool reversed = (effSort & Reversed) != 0;
        const bool ignoreCase = (effSort & IgnoreCase) != 0;

        auto compareNames = [ignoreCase](const String &a, const String &b) noexcept -> int {
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
        };

        std::stable_sort(items.begin(), items.end(), [&](const EntryItem &a, const EntryItem &b) {
            if (dirsFirst && a.isDir != b.isDir)
                return a.isDir;
            if (dirsLast && a.isDir != b.isDir)
                return !a.isDir;

            int cmp = 0;
            if (sortBy == Time) {
                if (a.mtime != b.mtime)
                    cmp = (a.mtime > b.mtime) ? -1 : 1; // Qt Time sort: newest first
                else
                    cmp = compareNames(a.name, b.name);
            } else if (sortBy == Size) {
                if (a.size != b.size)
                    cmp = (a.size > b.size) ? -1 : 1; // Qt Size sort: largest first
                else
                    cmp = compareNames(a.name, b.name);
            } else if (sortBy == Name) {
                cmp = compareNames(a.name, b.name);
            }
            if (reversed)
                cmp = -cmp;
            return cmp < 0;
        });
    }

    StringList result;
    result.reserve(items.size());
    for (EntryItem &item : items)
        result.push_back(std::move(item.name));
    return result;
#else
    return StringList();
#endif
}

String Dir::toNativeSeparators(const String &pathName)
{
#ifdef _WIN32
    String out = pathName;
    out.replace(Char(u'/'), Char(u'\\'));
    return out;
#else
    return pathName;
#endif
}

String Dir::fromNativeSeparators(const String &pathName)
{
    String out = pathName;
    out.replace(Char(u'\\'), Char(u'/'));
    return out;
}

String Dir::cleanPath(const String &path)
{
    if (path.isEmpty())
        return String();

    const String norm = fromNativeSeparators(path);
    String prefix;
    bool rooted = false;
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
        rooted = true;
        start = s2 + 1;
    } else if (hasDrivePrefix(norm)) {
        if (norm.size() >= 3 && norm.at(2) == Char(u'/')) {
            prefix = norm.left(3);
            rooted = true;
            start = 3;
        } else {
            prefix = norm.left(2);
            rooted = false;
            start = 2;
        }
    } else if (norm.startsWith(Char(u'/'))) {
        prefix = ANIME_STR("/");
        rooted = true;
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
#ifdef _WIN32
    const DWORD needed = GetCurrentDirectoryW(0, nullptr);
    if (needed == 0)
        return String();
    std::wstring buf(needed, L'\0');
    const DWORD written = GetCurrentDirectoryW(needed, buf.data());
    if (written == 0 || written >= needed)
        return String();
    buf.resize(written);
    return cleanPath(wstringToString(buf.data(), static_cast<int>(buf.size())));
#else
    return String();
#endif
}

String Dir::tempPath()
{
#ifdef _WIN32
    const DWORD needed = GetTempPathW(0, nullptr);
    if (needed == 0)
        return String();
    std::wstring buf(needed, L'\0');
    const DWORD written = GetTempPathW(needed, buf.data());
    if (written == 0 || written >= needed)
        return String();
    buf.resize(written);
    return cleanPath(wstringToString(buf.data(), static_cast<int>(buf.size())));
#else
    return ANIME_STR("/tmp");
#endif
}

// ============================================================================
// applicationDirPath
// ============================================================================

String applicationDirPath()
{
#ifdef _WIN32
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0)
            return String();
        if (len < buf.size() - 1) {
            buf.resize(len);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    const String exePath = Dir::fromNativeSeparators(
        wstringToString(buf.data(), static_cast<int>(buf.size())));
    return FileInfo(exePath).absolutePath();
#else
    return String();
#endif
}

} // namespace AnimeCore
