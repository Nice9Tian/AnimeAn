// Windows side of native.h and environment.h. Wide (UTF-16) system calls
// throughout, long paths through the \\?\ prefix.

#include "algorithm/core/native.h"

#include "algorithm/core/environment.h"
#include "algorithm/core/file.h"
#include "algorithm/core/pathtext.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <string>

namespace AnimeCore {

namespace {

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

void setLastErrorText(String *errorText)
{
    if (errorText)
        *errorText = win32ErrorString(GetLastError());
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

    const bool hadTrailingSlash = PathText::hasTrailingSeparatorNotRoot(path);

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

HANDLE toHandle(void *handle) noexcept
{
    return reinterpret_cast<HANDLE>(handle);
}

DWORD creationDisposition(Native::Creation creation) noexcept
{
    switch (creation) {
    case Native::Creation::OpenExisting:
        return OPEN_EXISTING;
    case Native::Creation::TruncateExisting:
        return TRUNCATE_EXISTING;
    case Native::Creation::CreateNew:
        return CREATE_NEW;
    case Native::Creation::CreateAlways:
        return CREATE_ALWAYS;
    case Native::Creation::OpenAlways:
        return OPEN_ALWAYS;
    }
    return OPEN_EXISTING;
}

std::uint64_t joinParts(DWORD low, DWORD high) noexcept
{
    ULARGE_INTEGER value{};
    value.LowPart = low;
    value.HighPart = high;
    return value.QuadPart;
}

} // namespace

namespace Native {

const bool textModeWritesCrLf = true;

bool fileStatus(const String &path, FileStatus *out)
{
    if (path.isEmpty())
        return false;
    const std::wstring wpath = toWin32Path(path);
    if (wpath.empty())
        return false;
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &fad))
        return false;
    out->isDir = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    out->size = joinParts(fad.nFileSizeLow, fad.nFileSizeHigh);
    return true;
}

void *openFile(const String &path, const OpenRequest &request, OpenError *error, String *errorText)
{
    DWORD access = 0;
    if (request.read)
        access |= GENERIC_READ;
    if (request.write)
        access |= GENERIC_WRITE;
    const DWORD share = request.exclusiveWrite
                            ? FILE_SHARE_READ
                            : (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);

    const std::wstring wpath = toWin32Path(path);
    HANDLE h = CreateFileW(wpath.c_str(), access, share, nullptr,
                           creationDisposition(request.creation), FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        if (error) {
            *error = (err == ERROR_FILE_EXISTS || err == ERROR_ALREADY_EXISTS)
                         ? OpenError::AlreadyExists
                         : OpenError::Other;
        }
        if (errorText)
            *errorText = win32ErrorString(err);
        return nullptr;
    }
    if (error)
        *error = OpenError::None;
    return reinterpret_cast<void *>(h);
}

void closeFile(void *handle)
{
    if (handle)
        CloseHandle(toHandle(handle));
}

bool seekToEnd(void *handle)
{
    LARGE_INTEGER zero{};
    return SetFilePointerEx(toHandle(handle), zero, nullptr, FILE_END) != 0;
}

bool fileSize(void *handle, std::uint64_t *size)
{
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(toHandle(handle), &sz))
        return false;
    *size = static_cast<std::uint64_t>(sz.QuadPart);
    return true;
}

bool filePosition(void *handle, std::uint64_t *position)
{
    LARGE_INTEGER current{};
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(toHandle(handle), zero, &current, FILE_CURRENT))
        return false;
    *position = static_cast<std::uint64_t>(current.QuadPart);
    return true;
}

bool readFile(void *handle, char *buffer, std::size_t capacity, std::size_t *bytesRead, String *errorText)
{
    DWORD got = 0;
    const DWORD want = capacity > 0x40000000u ? 0x40000000u : static_cast<DWORD>(capacity);
    if (!ReadFile(toHandle(handle), buffer, want, &got, nullptr)) {
        setLastErrorText(errorText);
        return false;
    }
    *bytesRead = got;
    return true;
}

bool writeFile(void *handle, const char *data, std::size_t size, std::size_t *bytesWritten, String *errorText)
{
    DWORD written = 0;
    const DWORD chunk = size > 0x40000000u ? 0x40000000u : static_cast<DWORD>(size);
    if (!WriteFile(toHandle(handle), data, chunk, &written, nullptr)) {
        setLastErrorText(errorText);
        return false;
    }
    *bytesWritten = written;
    return true;
}

bool flushFile(void *handle, String *errorText)
{
    if (FlushFileBuffers(toHandle(handle)) != 0)
        return true;
    setLastErrorText(errorText);
    return false;
}

bool removeFile(const String &path, String *errorText)
{
    const std::wstring wpath = toWin32Path(path);
    if (DeleteFileW(wpath.c_str()))
        return true;
    setLastErrorText(errorText);
    return false;
}

bool renameReplacing(const String &from, const String &to, String *errorText)
{
    const std::wstring wfrom = toWin32Path(from);
    const std::wstring wto = toWin32Path(to);
    if (MoveFileExW(wfrom.c_str(), wto.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    setLastErrorText(errorText);
    return false;
}

MakeDirResult makeDirectory(const String &path)
{
    const std::wstring wpath = toWin32Path(path);
    if (CreateDirectoryW(wpath.c_str(), nullptr))
        return MakeDirResult::Created;
    return GetLastError() == ERROR_ALREADY_EXISTS ? MakeDirResult::AlreadyExists : MakeDirResult::Failed;
}

bool listDirectory(const String &path, std::vector<DirEntry> *out)
{
    std::wstring searchPattern = toWin32Path(path);
    if (!searchPattern.empty() && searchPattern.back() != L'\\')
        searchPattern.push_back(L'\\');
    searchPattern.push_back(L'*');

    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return false;

    do {
        DirEntry entry;
        entry.name = wstringToString(fd.cFileName);
        entry.isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.hidden = (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        entry.system = (fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0;
        entry.size = joinParts(fd.nFileSizeLow, fd.nFileSizeHigh);
        entry.modified = joinParts(fd.ftLastWriteTime.dwLowDateTime, fd.ftLastWriteTime.dwHighDateTime);
        out->push_back(std::move(entry));
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return true;
}

String currentDirectory()
{
    const DWORD needed = GetCurrentDirectoryW(0, nullptr);
    if (needed == 0)
        return String();
    std::wstring buf(needed, L'\0');
    const DWORD written = GetCurrentDirectoryW(needed, buf.data());
    if (written == 0 || written >= needed)
        return String();
    buf.resize(written);
    return wstringToString(buf.data(), static_cast<int>(buf.size()));
}

String tempDirectory()
{
    const DWORD needed = GetTempPathW(0, nullptr);
    if (needed == 0)
        return String();
    std::wstring buf(needed, L'\0');
    const DWORD written = GetTempPathW(needed, buf.data());
    if (written == 0 || written >= needed)
        return String();
    buf.resize(written);
    return wstringToString(buf.data(), static_cast<int>(buf.size()));
}

String userConfigDirectory()
{
    return environmentVariable(ANIME_STR("APPDATA"));
}

String executablePath()
{
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
    return wstringToString(buf.data(), static_cast<int>(buf.size()));
}

std::uint64_t processId()
{
    return GetCurrentProcessId();
}

std::uint64_t tickCount()
{
    return GetTickCount64();
}

} // namespace Native

// ---- environment.h -------------------------------------------------------------

String environmentVariable(const String &name)
{
    const std::wstring wname = stringToWString(name);
    const DWORD needed = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
    if (needed > 0) {
        std::wstring buf(needed, L'\0');
        const DWORD written = GetEnvironmentVariableW(wname.c_str(), buf.data(), needed);
        if (written > 0 && written < needed) {
            buf.resize(written);
            return wstringToString(buf.data(), static_cast<int>(buf.size()));
        }
    }
    // The C runtime keeps its own copy of the environment; a value set through
    // it alone is only visible there.
    if (const wchar_t *val = _wgetenv(wname.c_str()))
        return wstringToString(val);
    return String();
}

bool setEnvironmentVariable(const String &name, const String &value)
{
    const std::wstring wname = stringToWString(name);
    const std::wstring wvalue = stringToWString(value);
    // Both copies: the system's, which child processes and DLL loading read,
    // and the C runtime's, which getenv reads.
    const bool systemOk = SetEnvironmentVariableW(wname.c_str(), wvalue.c_str()) != 0;
    const bool runtimeOk = _wputenv_s(wname.c_str(), wvalue.c_str()) == 0;
    return systemOk && runtimeOk;
}

Char pathListSeparator() noexcept
{
    return Char(u';');
}

} // namespace AnimeCore
