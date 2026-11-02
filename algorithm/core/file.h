#pragma once

#include "algorithm/core/string.h"

#include <cstdint>
#include <string>
#include <vector>

namespace AnimeCore {

class IODevice
{
public:
    enum OpenModeFlag {
        NotOpen = 0x0000,
        ReadOnly = 0x0001,
        WriteOnly = 0x0002,
        ReadWrite = ReadOnly | WriteOnly,
        Append = 0x0004,
        Truncate = 0x0008,
        Text = 0x0010,
        Unbuffered = 0x0020,
        NewOnly = 0x0040,
        ExistingOnly = 0x0080
    };
    using OpenMode = int;
};

constexpr IODevice::OpenMode operator|(IODevice::OpenModeFlag a, IODevice::OpenModeFlag b) noexcept
{
    return static_cast<int>(a) | static_cast<int>(b);
}

constexpr IODevice::OpenMode operator|(IODevice::OpenMode a, IODevice::OpenModeFlag b) noexcept
{
    return a | static_cast<int>(b);
}

constexpr IODevice::OpenMode operator|(IODevice::OpenModeFlag a, IODevice::OpenMode b) noexcept
{
    return static_cast<int>(a) | b;
}

class File : public IODevice
{
public:
    File() noexcept = default;
    explicit File(const String &name);
    ~File();

    File(const File &) = delete;
    File &operator=(const File &) = delete;
    File(File &&other) noexcept;
    File &operator=(File &&other) noexcept;

    String fileName() const { return m_fileName; }
    void setFileName(const String &name);

    bool exists() const;
    static bool exists(const String &fileName);

    bool remove();
    static bool remove(const String &fileName);

    bool open(OpenMode mode);
    bool isOpen() const noexcept { return m_isOpen; }
    void close();

    std::int64_t size() const;
    std::string readAll();

    std::int64_t write(const char *data, std::int64_t len);
    std::int64_t write(const char *data);
    std::int64_t write(const std::string &data);
    std::int64_t write(const std::vector<unsigned char> &data);

    String errorString() const { return m_errorString; }

private:
    String m_fileName;
    String m_errorString = ANIME_STR("Unknown error");
    void *m_handle = nullptr;
    bool m_isOpen = false;
    bool m_readable = false;
    bool m_writable = false;
    bool m_textMode = false;
};

class SaveFile : public IODevice
{
public:
    SaveFile() noexcept = default;
    explicit SaveFile(const String &name);
    ~SaveFile();

    SaveFile(const SaveFile &) = delete;
    SaveFile &operator=(const SaveFile &) = delete;

    String fileName() const { return m_fileName; }
    void setFileName(const String &name);

    bool open(OpenMode mode);
    bool isOpen() const noexcept { return m_isOpen; }

    std::int64_t write(const char *data, std::int64_t len);
    std::int64_t write(const char *data);
    std::int64_t write(const std::string &data);
    std::int64_t write(const std::vector<unsigned char> &data);

    bool commit();
    void cancelWriting();

    String errorString() const { return m_errorString; }

private:
    void discardTempFile();

    String m_fileName;
    String m_tempFileName;
    String m_errorString = ANIME_STR("Unknown error");
    void *m_handle = nullptr;
    bool m_isOpen = false;
    bool m_textMode = false;
    bool m_writeError = false;
    bool m_cancelled = false;
};

class Dir;

class FileInfo
{
public:
    FileInfo() noexcept = default;
    explicit FileInfo(const String &file);
    FileInfo(const Dir &dir, const String &file);

    void setFile(const String &file);

    bool exists() const;
    static bool exists(const String &file);

    bool isFile() const;
    bool isDir() const;
    bool isRelative() const;
    bool isAbsolute() const { return !isRelative(); }

    String filePath() const { return m_filePath; }
    String fileName() const;
    String baseName() const;
    String completeBaseName() const;
    String suffix() const;
    String completeSuffix() const;

    String path() const;
    String absolutePath() const;
    String absoluteFilePath() const;

    Dir dir() const;
    Dir absoluteDir() const;

    std::int64_t size() const;

private:
    String m_filePath;
};

class Dir
{
public:
    enum Filter {
        Dirs = 0x001,
        Files = 0x002,
        Drives = 0x004,
        NoSymLinks = 0x008,
        AllEntries = Dirs | Files | Drives,
        TypeMask = 0x00f,
        Readable = 0x010,
        Writable = 0x020,
        Executable = 0x040,
        PermissionMask = 0x070,
        Modified = 0x080,
        Hidden = 0x100,
        System = 0x200,
        AccessMask = 0x3F0,
        AllDirs = 0x400,
        CaseSensitive = 0x800,
        NoDot = 0x2000,
        NoDotDot = 0x4000,
        NoDotAndDotDot = NoDot | NoDotDot,
        NoFilter = -1
    };
    using Filters = int;

    enum SortFlag {
        Name = 0x00,
        Time = 0x01,
        Size = 0x02,
        Unsorted = 0x03,
        SortByMask = 0x03,
        DirsFirst = 0x04,
        Reversed = 0x08,
        IgnoreCase = 0x10,
        DirsLast = 0x20,
        LocaleAware = 0x40,
        Type = 0x80,
        NoSort = -1
    };
    using SortFlags = int;

    Dir(const String &path = String());

    void setPath(const String &path);
    String path() const { return m_path; }
    String absolutePath() const;
    String dirName() const;

    String filePath(const String &fileName) const;
    String absoluteFilePath(const String &fileName) const;

    bool exists() const;
    bool exists(const String &name) const;

    bool mkdir(const String &dirName) const;
    bool mkpath(const String &dirPath) const;
    bool cdUp();

    StringList entryList(Filters filters = NoFilter, SortFlags sort = NoSort) const;
    StringList entryList(const StringList &nameFilters, Filters filters = NoFilter, SortFlags sort = NoSort) const;

    static String toNativeSeparators(const String &pathName);
    static String fromNativeSeparators(const String &pathName);
    static String cleanPath(const String &path);
    static String currentPath();
    static String tempPath();
    static Char separator() noexcept
    {
#ifdef _WIN32
        return Char(u'\\');
#else
        return Char(u'/');
#endif
    }

private:
    String m_path;
};

constexpr Dir::Filters operator|(Dir::Filter a, Dir::Filter b) noexcept
{
    return static_cast<int>(a) | static_cast<int>(b);
}

constexpr Dir::Filters operator|(Dir::Filters a, Dir::Filter b) noexcept
{
    return a | static_cast<int>(b);
}

constexpr Dir::Filters operator|(Dir::Filter a, Dir::Filters b) noexcept
{
    return static_cast<int>(a) | b;
}

constexpr Dir::SortFlags operator|(Dir::SortFlag a, Dir::SortFlag b) noexcept
{
    return static_cast<int>(a) | static_cast<int>(b);
}

constexpr Dir::SortFlags operator|(Dir::SortFlags a, Dir::SortFlag b) noexcept
{
    return a | static_cast<int>(b);
}

constexpr Dir::SortFlags operator|(Dir::SortFlag a, Dir::SortFlags b) noexcept
{
    return static_cast<int>(a) | b;
}

String applicationDirPath();

class CoreApplication
{
public:
    static String applicationDirPath() { return ::AnimeCore::applicationDirPath(); }
};

} // namespace AnimeCore
