// File, SaveFile, FileInfo, Dir and the environment calls against the real
// file system, on every system the core is built for. No Qt: the comparison
// with Qt is tests/core/file_tests.cpp, which only builds where Qt is present.

#include "algorithm/core/environment.h"
#include "algorithm/core/file.h"
#include "algorithm/core/string.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#endif

using namespace AnimeCore;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (false)

String u8(const char *text)
{
    return String::fromUtf8(text);
}

bool writeWhole(const String &path, const std::string &bytes, IODevice::OpenMode mode)
{
    File f(path);
    if (!f.open(mode))
        return false;
    const bool ok = f.write(bytes) == static_cast<std::int64_t>(bytes.size());
    f.close();
    return ok;
}

std::string readWhole(const String &path, IODevice::OpenMode mode = IODevice::ReadOnly)
{
    File f(path);
    if (!f.open(mode))
        return std::string("<cannot open>");
    return f.readAll();
}

bool contains(const StringList &list, const String &item)
{
    for (const String &s : list) {
        if (s == item)
            return true;
    }
    return false;
}

// A fresh folder under the system's temporary folder, removed with everything
// in it when the test ends.
class ScratchFolder
{
public:
    ScratchFolder()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = Dir(Dir::tempPath()).filePath(u8("animean_fileio_")
                                               + String::number(static_cast<long long>(stamp)));
        m_ok = Dir().mkpath(m_path);
    }

    ~ScratchFolder()
    {
        std::error_code ignored;
        std::filesystem::remove_all(std::filesystem::path(m_path.u16String()), ignored);
    }

    bool ok() const { return m_ok; }
    String file(const char *name) const { return Dir(m_path).filePath(u8(name)); }
    const String &path() const { return m_path; }

private:
    String m_path;
    bool m_ok = false;
};

void testProcessFolders()
{
    const String current = Dir::currentPath();
    CHECK(!current.isEmpty());
    CHECK(FileInfo(current).isAbsolute());
    CHECK(FileInfo(current).isDir());

    const String temp = Dir::tempPath();
    CHECK(!temp.isEmpty());
    CHECK(FileInfo(temp).isDir());

    const String appDir = applicationDirPath();
    CHECK(!appDir.isEmpty());
    CHECK(FileInfo(appDir).isDir());
    CHECK(!appDir.contains(Char(u'\\')));
}

void testOpenModes(const ScratchFolder &scratch)
{
    const String path = scratch.file("modes.bin");

    File missing(path);
    CHECK(!missing.exists());
    CHECK(!missing.open(IODevice::ReadOnly));
    CHECK(!missing.open(IODevice::ReadWrite | IODevice::ExistingOnly));

    CHECK(writeWhole(path, "hello", IODevice::WriteOnly));
    CHECK(FileInfo(path).isFile());
    CHECK(!FileInfo(path).isDir());
    CHECK(FileInfo(path).size() == 5);
    CHECK(readWhole(path) == "hello");

    // WriteOnly alone empties the file; Append keeps what is there.
    CHECK(writeWhole(path, "abc", IODevice::WriteOnly));
    CHECK(readWhole(path) == "abc");
    CHECK(writeWhole(path, "def", IODevice::WriteOnly | IODevice::Append));
    CHECK(readWhole(path) == "abcdef");

    // ReadWrite keeps the content and writes from the start.
    {
        File f(path);
        CHECK(f.open(IODevice::ReadWrite));
        CHECK(f.size() == 6);
        CHECK(f.write("XY") == 2);
        f.close();
    }
    CHECK(readWhole(path) == "XYcdef");

    File again(path);
    CHECK(!again.open(IODevice::WriteOnly | IODevice::NewOnly));
    CHECK(again.open(IODevice::WriteOnly | IODevice::ExistingOnly | IODevice::Truncate));
    again.close();
    CHECK(FileInfo(path).size() == 0);

    // A folder is not a file, and neither is a file named with a trailing slash.
    File folder(scratch.path());
    CHECK(!folder.open(IODevice::ReadOnly));
    CHECK(!FileInfo(path + u8("/")).exists());

    CHECK(File::remove(path));
    CHECK(!File::exists(path));
    CHECK(!File::remove(path));
}

void testTextMode(const ScratchFolder &scratch)
{
    const String path = scratch.file("text.txt");
    CHECK(writeWhole(path, "one\ntwo\n", IODevice::WriteOnly | IODevice::Text));
#ifdef _WIN32
    CHECK(readWhole(path) == "one\r\ntwo\r\n");
#else
    CHECK(readWhole(path) == "one\ntwo\n");
#endif
    CHECK(readWhole(path, IODevice::ReadOnly | IODevice::Text) == "one\ntwo\n");

    // A file written on Windows reads the same in text mode everywhere.
    CHECK(writeWhole(path, "a\r\nb\rc\r\n", IODevice::WriteOnly));
    CHECK(readWhole(path, IODevice::ReadOnly | IODevice::Text) == "a\nb\rc\n");
}

void testSaveFile(const ScratchFolder &scratch)
{
    const String path = scratch.file("scene.json");

    {
        SaveFile sf(path);
        CHECK(sf.open(IODevice::WriteOnly));
        CHECK(sf.write("{\"version\":1}") == 13);
        // Nothing is at the target until commit.
        CHECK(!FileInfo(path).exists());
        CHECK(sf.commit());
    }
    CHECK(readWhole(path) == "{\"version\":1}");

    {
        SaveFile sf(path);
        CHECK(sf.open(IODevice::WriteOnly));
        CHECK(sf.write("{\"version\":999}") == 15);
        sf.cancelWriting();
        CHECK(!sf.commit());
    }
    CHECK(readWhole(path) == "{\"version\":1}");

    {
        // Dropped without commit: the target is untouched.
        SaveFile sf(path);
        CHECK(sf.open(IODevice::WriteOnly));
        CHECK(sf.write("{\"half\":true}") > 0);
    }
    CHECK(readWhole(path) == "{\"version\":1}");

    {
        SaveFile sf(path);
        CHECK(sf.open(IODevice::WriteOnly));
        CHECK(sf.write("{\"version\":2}") == 13);
        CHECK(sf.commit());
    }
    CHECK(readWhole(path) == "{\"version\":2}");

    // No temporary file is left beside the target in any of the cases above.
    const StringList left = Dir(scratch.path()).entryList(Dir::Files | Dir::Hidden | Dir::NoDotAndDotDot);
    CHECK(contains(left, u8("scene.json")));
    for (const String &name : left) {
        CHECK(!name.contains(u8(".tmp.")));
    }

    SaveFile intoNowhere(scratch.file("no_such_folder/scene.json"));
    CHECK(!intoNowhere.open(IODevice::WriteOnly));
    SaveFile ontoFolder(scratch.path());
    CHECK(!ontoFolder.open(IODevice::WriteOnly));

    CHECK(File::remove(path));
}

void testFoldersAndListing(const ScratchFolder &scratch)
{
    const Dir root(scratch.file("listing"));
    CHECK(!root.exists());
    CHECK(Dir().mkpath(root.filePath(u8("deep/er/est"))));
    CHECK(Dir().mkpath(root.filePath(u8("deep/er/est")))); // already there is fine
    CHECK(root.exists());
    CHECK(root.exists(u8("deep")));
    CHECK(root.mkdir(u8("sub")));
    CHECK(!root.mkdir(u8("sub")));
    CHECK(!root.mkdir(u8("missing/child")));

    CHECK(writeWhole(root.filePath(u8("b.txt")), "bb", IODevice::WriteOnly));
    CHECK(writeWhole(root.filePath(u8("A.txt")), "a", IODevice::WriteOnly));
    CHECK(writeWhole(root.filePath(u8("c.json")), "cccc", IODevice::WriteOnly));
    CHECK(writeWhole(root.filePath(u8(".hidden")), "h", IODevice::WriteOnly));
    // A folder cannot be made where a file is.
    CHECK(!Dir().mkpath(root.filePath(u8("b.txt/inside"))));

    const StringList files = root.entryList(Dir::Files, Dir::Name | Dir::IgnoreCase);
    CHECK(contains(files, u8("A.txt")) && contains(files, u8("b.txt")) && contains(files, u8("c.json")));
    CHECK(!contains(files, u8("sub")) && !contains(files, u8("deep")));
    if (files.size() >= 3) {
        const StringList visible = root.entryList(StringList{u8("*.txt"), u8("*.json")},
                                                  Dir::Files, Dir::Name | Dir::IgnoreCase);
        CHECK(visible.size() == 3);
        CHECK(visible.at(0) == u8("A.txt") && visible.at(1) == u8("b.txt") && visible.at(2) == u8("c.json"));
    }

    const StringList folders = root.entryList(Dir::Dirs | Dir::NoDotAndDotDot, Dir::Name);
    CHECK(folders.size() == 2);
    CHECK(contains(folders, u8("deep")) && contains(folders, u8("sub")));
    const StringList withDots = root.entryList(Dir::Dirs, Dir::Name);
    CHECK(contains(withDots, u8(".")) && contains(withDots, u8("..")));

    // Name filters ignore case unless asked not to.
    CHECK(root.entryList(StringList{u8("a.TXT")}, Dir::Files).size() == 1);
    CHECK(root.entryList(StringList{u8("a.TXT")}, Dir::Files | Dir::CaseSensitive).empty());

    const StringList bySize = root.entryList(StringList{u8("*.txt"), u8("*.json")}, Dir::Files, Dir::Size);
    CHECK(bySize.size() == 3);
    if (bySize.size() == 3) {
        CHECK(bySize.at(0) == u8("c.json") && bySize.at(2) == u8("A.txt")); // largest first
    }

    Dir walker(root.filePath(u8("deep/er/est")));
    CHECK(walker.cdUp());
    CHECK(walker.dirName() == u8("er"));
    CHECK(FileInfo(root.filePath(u8("deep/../b.txt"))).absoluteFilePath()
          == Dir::cleanPath(root.absoluteFilePath(u8("b.txt"))));
    CHECK(Dir(u8("no/such/folder/anywhere")).entryList().empty());
}

void testHiddenEntries(const ScratchFolder &scratch)
{
#ifndef _WIN32
    // A leading dot hides an entry here; on Windows hiding is a file attribute,
    // which this layer has no call to set.
    const Dir root(scratch.file("listing"));
    CHECK(!contains(root.entryList(Dir::Files), u8(".hidden")));
    CHECK(contains(root.entryList(Dir::Files | Dir::Hidden), u8(".hidden")));
#else
    (void)scratch;
#endif
}

void testSaveKeepsPermissions(const ScratchFolder &scratch)
{
#ifndef _WIN32
    // Saving over a file that was made private leaves it private.
    const String path = scratch.file("private.json");
    CHECK(writeWhole(path, "{}", IODevice::WriteOnly));
    const std::string systemPath = path.toUtf8();
    CHECK(::chmod(systemPath.c_str(), 0600) == 0);
    {
        SaveFile sf(path);
        CHECK(sf.open(IODevice::WriteOnly));
        CHECK(sf.write("{\"saved\":true}") > 0);
        CHECK(sf.commit());
    }
    struct stat st {};
    CHECK(::stat(systemPath.c_str(), &st) == 0);
    CHECK((st.st_mode & 0777) == 0600);
    CHECK(readWhole(path) == "{\"saved\":true}");
#else
    (void)scratch;
#endif
}

void testUnicodeNames(const ScratchFolder &scratch)
{
    // 动画 场景.v2.json inside 项目 目录
    const String folder = scratch.file("\xE9\xA1\xB9\xE7\x9B\xAE \xE7\x9B\xAE\xE5\xBD\x95");
    const String name = u8("\xE5\x8A\xA8\xE7\x94\xBB \xE5\x9C\xBA\xE6\x99\xAF.v2.json");
    CHECK(Dir().mkpath(folder));
    const String path = Dir(folder).filePath(name);
    CHECK(writeWhole(path, "{}", IODevice::WriteOnly));
    CHECK(FileInfo(path).isFile());
    CHECK(readWhole(path) == "{}");
    CHECK(contains(Dir(folder).entryList(Dir::Files), name));
    CHECK(FileInfo(path).suffix() == u8("json"));
    CHECK(FileInfo(path).completeBaseName() == u8("\xE5\x8A\xA8\xE7\x94\xBB \xE5\x9C\xBA\xE6\x99\xAF.v2"));
}

void testEnvironment()
{
    const String name = u8("ANIMEAN_FILEIO_TEST_VARIABLE");
    CHECK(environmentVariable(name).isEmpty());
    // 动画 フォルダ
    const String value = u8("\xE5\x8A\xA8\xE7\x94\xBB \xE3\x83\x95\xE3\x82\xA9\xE3\x83\xAB\xE3\x83\x80");
    CHECK(setEnvironmentVariable(name, value));
    CHECK(environmentVariable(name) == value);
    CHECK(setEnvironmentVariable(name, u8("plain")));
    CHECK(environmentVariable(name) == u8("plain"));
    CHECK(!environmentVariable(u8("PATH")).isEmpty());
    CHECK(pathListSeparator() == Char(u';') || pathListSeparator() == Char(u':'));
}

} // namespace

int main()
{
    testProcessFolders();
    testEnvironment();

    ScratchFolder scratch;
    if (!scratch.ok()) {
        std::fprintf(stderr, "FAIL could not make a scratch folder under the temporary folder\n");
        return 1;
    }
    testOpenModes(scratch);
    testTextMode(scratch);
    testSaveFile(scratch);
    testFoldersAndListing(scratch);
    testHiddenEntries(scratch);
    testSaveKeepsPermissions(scratch);
    testUnicodeNames(scratch);

    if (g_failures > 0) {
        std::fprintf(stderr, "%d failure(s) in fileio_tests\n", g_failures);
        return 1;
    }
    std::printf("FILEIO_TESTS_OK\n");
    return 0;
}
