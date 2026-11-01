#include "algorithm/core/file.h"
#include "algorithm/core/qtbridge_string.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QSaveFile>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

using namespace AnimeCore;

static int g_failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                           \
        }                                                                           \
    } while (0)

static QString toQ(const String &s)
{
    return s;
}

static String toCore(const QString &q)
{
    return q;
}

static void checkStrEq(const char *file, int line, const char *expr,
                       const String &actual, const QString &expected)
{
    const QString actualQ = toQ(actual);
    if (actualQ != expected) {
        std::fprintf(stderr, "FAIL %s:%d: %s\n  actual  : \"%s\"\n  expected: \"%s\"\n",
                     file, line, expr,
                     actualQ.toUtf8().constData(),
                     expected.toUtf8().constData());
        ++g_failures;
    }
}

#define CHECK_STR_EQ(actual, expected) \
    checkStrEq(__FILE__, __LINE__, #actual " == " #expected, (actual), (expected))

static void checkFileInfoDiff(const QString &qpath)
{
    const String cpath = toCore(qpath);
    const QFileInfo qfi(qpath);
    const FileInfo cfi(cpath);

    if (cfi.exists() != qfi.exists()) {
        std::fprintf(stderr, "FAIL exists() mismatch for \"%s\": core=%d qt=%d\n",
                     qpath.toUtf8().constData(), int(cfi.exists()), int(qfi.exists()));
        ++g_failures;
    }
    if (FileInfo::exists(cpath) != QFileInfo::exists(qpath)) {
        std::fprintf(stderr, "FAIL static exists() mismatch for \"%s\": core=%d qt=%d\n",
                     qpath.toUtf8().constData(), int(FileInfo::exists(cpath)), int(QFileInfo::exists(qpath)));
        ++g_failures;
    }
    if (cfi.isFile() != qfi.isFile()) {
        std::fprintf(stderr, "FAIL isFile() mismatch for \"%s\": core=%d qt=%d\n",
                     qpath.toUtf8().constData(), int(cfi.isFile()), int(qfi.isFile()));
        ++g_failures;
    }
    if (cfi.isDir() != qfi.isDir()) {
        std::fprintf(stderr, "FAIL isDir() mismatch for \"%s\": core=%d qt=%d\n",
                     qpath.toUtf8().constData(), int(cfi.isDir()), int(qfi.isDir()));
        ++g_failures;
    }

    CHECK_STR_EQ(cfi.fileName(), qfi.fileName());
    CHECK_STR_EQ(cfi.baseName(), qfi.baseName());
    CHECK_STR_EQ(cfi.completeBaseName(), qfi.completeBaseName());
    CHECK_STR_EQ(cfi.suffix(), qfi.suffix());
    CHECK_STR_EQ(cfi.completeSuffix(), qfi.completeSuffix());
    CHECK_STR_EQ(cfi.path(), qfi.path());
    CHECK_STR_EQ(cfi.absolutePath(), qfi.absolutePath());
    CHECK_STR_EQ(cfi.absoluteFilePath(), qfi.absoluteFilePath());
}

static void testFileInfoPaths()
{
    const QStringList syntheticPaths = {
        QString(),
        QStringLiteral(""),
        QStringLiteral("."),
        QStringLiteral(".."),
        QStringLiteral("C:/projects/demo/scene.json"),
        QStringLiteral("C:\\projects\\demo\\scene.json"),
        QStringLiteral("C:/项目 目录/子 文件夹/动画 场景.v2.final.json"),
        QStringLiteral("C:\\项目 目录\\子 文件夹\\动画 场景.v2.final.json"),
        QStringLiteral("C:/项目 目录/README"),
        QStringLiteral("Makefile"),
        QStringLiteral("C:/dir.with.dots/archive.tar.gz"),
        QStringLiteral("a.b.c.d"),
        QStringLiteral(".gitignore"),
        QStringLiteral("C:/dir/.gitignore"),
        QStringLiteral(".config.json"),
        QStringLiteral("C:/dir/.config.local.json"),
        QStringLiteral("C:/projects/demo/"),
        QStringLiteral("C:/项目 目录/子 文件夹/"),
        QStringLiteral("C:/"),
        QStringLiteral("relative/path/file.txt"),
        QStringLiteral("relative/path/dir/"),
        QStringLiteral("./local.png"),
        QStringLiteral("../parent/item.svg"),
        QStringLiteral("plain_name"),
        QStringLiteral("C:/nonexistent_dir_987654/nonexistent_file.xyz"),
        QStringLiteral("C:/不存在的目录 空格/不存在的文件.tar.gz"),
    };

    for (const QString &p : syntheticPaths) {
        checkFileInfoDiff(p);
    }
}

static void testFilesystemDifferential()
{
    QTemporaryDir tempDir;
    CHECK(tempDir.isValid());
    const QString rootQ = tempDir.path();
    const String rootC = toCore(rootQ);

    // Subdirectories with Chinese, spaces, dots
    const QString subDirQ = rootQ + QStringLiteral("/中文 文件夹/子.目.录");
    CHECK(Dir(rootC).mkpath(toCore(subDirQ)));
    CHECK(QDir(subDirQ).exists());
    CHECK(Dir(toCore(subDirQ)).exists());

    // Create test files with various naming patterns
    const QStringList relFiles = {
        QStringLiteral("normal.json"),
        QStringLiteral("带 空格 的 中文文件.txt"),
        QStringLiteral("无后缀文件"),
        QStringLiteral("multi.dot.name.tar.gz"),
        QStringLiteral(".leadingdot"),
        QStringLiteral(".leading.multi.dot.cfg"),
    };

    for (const QString &rel : relFiles) {
        const QString fullQ = subDirQ + QLatin1Char('/') + rel;
        File f(toCore(fullQ));
        CHECK(f.open(IODevice::WriteOnly | IODevice::Truncate));
        const std::string payload = "hello-" + rel.toStdString();
        CHECK(f.write(payload) == static_cast<std::int64_t>(payload.size()));
        f.close();

        // Verify Qt QFile reads identical bytes
        QFile qf(fullQ);
        CHECK(qf.open(QIODevice::ReadOnly));
        const QByteArray qbytes = qf.readAll();
        qf.close();
        CHECK(std::string(qbytes.constData(), static_cast<std::size_t>(qbytes.size())) == payload);

        // Verify AnimeCore::File reads identical bytes
        File rf(toCore(fullQ));
        CHECK(rf.open(IODevice::ReadOnly));
        CHECK(rf.readAll() == payload);
        rf.close();

        // Differential FileInfo checks on existing file and with trailing slash
        checkFileInfoDiff(fullQ);
        checkFileInfoDiff(fullQ + QLatin1Char('/'));
    }

    // Differential FileInfo checks on existing directory with and without trailing slash
    checkFileInfoDiff(subDirQ);
    checkFileInfoDiff(subDirQ + QLatin1Char('/'));
    checkFileInfoDiff(subDirQ + QStringLiteral("/不存在的.json"));

    // Dir differential checks
    const QDir qd(subDirQ);
    const Dir cd(toCore(subDirQ));

    CHECK_STR_EQ(cd.path(), qd.path());
    CHECK_STR_EQ(cd.absolutePath(), qd.absolutePath());
    CHECK_STR_EQ(cd.filePath(ANIME_STR("子项.json")), qd.filePath(QStringLiteral("子项.json")));
    CHECK_STR_EQ(cd.absoluteFilePath(ANIME_STR("子项.json")), qd.absoluteFilePath(QStringLiteral("子项.json")));
    CHECK(cd.exists() == qd.exists());
    CHECK(cd.exists(ANIME_STR("带 空格 的 中文文件.txt")) == qd.exists(QStringLiteral("带 空格 的 中文文件.txt")));
    CHECK(cd.exists(ANIME_STR("missing.txt")) == qd.exists(QStringLiteral("missing.txt")));
    CHECK(cd.exists(String()) == qd.exists(QString()));

    // Create child directories for entryList testing
    CHECK(cd.mkdir(ANIME_STR("目录A")));
    CHECK(cd.mkdir(ANIME_STR("目录B 空格")));

    auto checkEntryListEq = [&](const StringList &coreList, const QStringList &qtList) {
        CHECK(static_cast<int>(coreList.size()) == qtList.size());
        const int n = std::min(static_cast<int>(coreList.size()), static_cast<int>(qtList.size()));
        for (int i = 0; i < n; ++i) {
            CHECK_STR_EQ(coreList[static_cast<std::size_t>(i)], qtList[i]);
        }
    };

    checkEntryListEq(
        cd.entryList(Dir::Dirs | Dir::NoDotAndDotDot, Dir::Name),
        qd.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name));

    checkEntryListEq(
        cd.entryList(Dir::Files, Dir::Name),
        qd.entryList(QDir::Files, QDir::Name));

    checkEntryListEq(
        cd.entryList(StringList{ANIME_STR("*.json"), ANIME_STR("*.gz")}, Dir::Files, Dir::Name),
        qd.entryList(QStringList{QStringLiteral("*.json"), QStringLiteral("*.gz")}, QDir::Files, QDir::Name));

    // Separator & cleanPath differential checks
    const QStringList sepSamples = {
        QStringLiteral("C:/a/b\\c/d"),
        QStringLiteral("a/b/../c/./d//e/"),
        QStringLiteral("../a/./b/.."),
        QStringLiteral("C:/../a/b/.."),
        QStringLiteral("中文/路径\\测试/../结果"),
    };
    for (const QString &s : sepSamples) {
        CHECK_STR_EQ(Dir::toNativeSeparators(toCore(s)), QDir::toNativeSeparators(s));
        CHECK_STR_EQ(Dir::fromNativeSeparators(toCore(s)), QDir::fromNativeSeparators(s));
        CHECK_STR_EQ(Dir::cleanPath(toCore(s)), QDir::cleanPath(s));
    }

    // Error string check when opening non-existent file
    File missingFile(toCore(subDirQ + QStringLiteral("/no_such_file.bin")));
    QFile qMissingFile(subDirQ + QStringLiteral("/no_such_file.bin"));
    CHECK(missingFile.open(IODevice::ReadOnly) == qMissingFile.open(QIODevice::ReadOnly));
    CHECK(!missingFile.errorString().isEmpty());
}

static void testSaveFileDifferential()
{
    QTemporaryDir tempDir;
    CHECK(tempDir.isValid());
    const QString dirQ = tempDir.path() + QStringLiteral("/保存 测试");
    CHECK(Dir().mkpath(toCore(dirQ)));

    const QString targetQ = dirQ + QStringLiteral("/project.aep.json");
    const String targetC = toCore(targetQ);

    // 1. Initial atomic write and commit
    {
        SaveFile sf(targetC);
        CHECK(sf.open(IODevice::WriteOnly));
        CHECK(sf.write("{\"version\":1}") == 13);
        CHECK(sf.commit());
        // Second commit must return false just like QSaveFile
        CHECK(!sf.commit());
    }
    CHECK(File::exists(targetC));
    {
        File f(targetC);
        CHECK(f.open(IODevice::ReadOnly));
        CHECK(f.readAll() == "{\"version\":1}");
    }

    // 2. cancelWriting must keep original content intact and commit() must return false
    {
        SaveFile sf(targetC);
        CHECK(sf.open(IODevice::WriteOnly));
        CHECK(sf.write("{\"version\":999}") == 15);
        sf.cancelWriting();
        CHECK(!sf.commit());
    }
    {
        File f(targetC);
        CHECK(f.open(IODevice::ReadOnly));
        CHECK(f.readAll() == "{\"version\":1}");
    }

    // Compare with QSaveFile cancelWriting behavior
    const QString qTarget = dirQ + QStringLiteral("/qt_project.json");
    {
        QSaveFile qsf(qTarget);
        CHECK(qsf.open(QIODevice::WriteOnly));
        qsf.write("{\"v\":1}");
        CHECK(qsf.commit());
        CHECK(!qsf.commit());
    }
    {
        QSaveFile qsf(qTarget);
        CHECK(qsf.open(QIODevice::WriteOnly));
        qsf.write("{\"v\":2}");
        qsf.cancelWriting();
        CHECK(!qsf.commit());
    }

    // 3. Destructor without commit() must discard temp file and keep original content
    {
        SaveFile sf(targetC);
        CHECK(sf.open(IODevice::WriteOnly));
        CHECK(sf.write("{\"corrupted\":true}") > 0);
    }
    {
        File f(targetC);
        CHECK(f.open(IODevice::ReadOnly));
        CHECK(f.readAll() == "{\"version\":1}");
    }

    // 4. Overwrite existing file atomically via commit()
    {
        SaveFile sf(targetC);
        CHECK(sf.open(IODevice::WriteOnly));
        CHECK(sf.write("{\"version\":2}") == 13);
        CHECK(sf.commit());
    }
    {
        File f(targetC);
        CHECK(f.open(IODevice::ReadOnly));
        CHECK(f.readAll() == "{\"version\":2}");
    }

    // Ensure no leftover temporary files remain in dirQ
    const StringList remaining = Dir(toCore(dirQ)).entryList(Dir::Files, Dir::Name);
    CHECK(remaining.size() == 2); // project.aep.json and qt_project.json
}

static void testLongPaths()
{
    QTemporaryDir tempDir;
    CHECK(tempDir.isValid());
    String deepDir = toCore(tempDir.path());
    for (int i = 0; i < 12; ++i) {
        deepDir += ANIME_STR("/长路径子目录_带空格_segment_") + String::number(i) + ANIME_STR("_abcdefghij");
    }
    CHECK(deepDir.size() > 280);
    CHECK(Dir().mkpath(deepDir));
    CHECK(FileInfo(deepDir).exists());
    CHECK(FileInfo(deepDir).isDir());

    const String longFile = deepDir + ANIME_STR("/测试长文件名.multi.dot.json");
    {
        SaveFile sf(longFile);
        CHECK(sf.open(IODevice::WriteOnly));
        CHECK(sf.write("long-path-content") == 17);
        CHECK(sf.commit());
    }
    CHECK(FileInfo(longFile).exists());
    CHECK(FileInfo(longFile).isFile());
    {
        File f(longFile);
        CHECK(f.open(IODevice::ReadOnly));
        CHECK(f.readAll() == "long-path-content");
    }
}

static void testApplicationDirPath()
{
    const QString qtAppDir = QCoreApplication::applicationDirPath();
    const String coreAppDir = applicationDirPath();
    CHECK_STR_EQ(coreAppDir, qtAppDir);
    CHECK_STR_EQ(CoreApplication::applicationDirPath(), qtAppDir);
    CHECK(!coreAppDir.contains(Char(u'\\')));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testApplicationDirPath();
    testFileInfoPaths();
    testFilesystemDifferential();
    testSaveFileDifferential();
    testLongPaths();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s) in file_tests\n", g_failures);
        return 1;
    }
    std::printf("file_tests passed\n");
    return 0;
}
