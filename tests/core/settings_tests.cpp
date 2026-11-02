#include "algorithm/core/settings.h"
#include "algorithm/core/file.h"
#include "algorithm/core/qtbridge_string.h"

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

static String toCore(const QString &q)
{
    return q;
}

static void setSettingsDirEnv(const String &dirPath)
{
#ifdef _WIN32
    const std::wstring wdir(reinterpret_cast<const wchar_t *>(dirPath.utf16()),
                            static_cast<std::size_t>(dirPath.size()));
    _wputenv_s(L"ANIMEAN_SETTINGS_DIR", wdir.c_str());
#endif
}

static void testWriteReadAndGroups()
{
    QTemporaryDir tempDir;
    CHECK(tempDir.isValid());
    const String isolatedDir = toCore(tempDir.path()) + ANIME_STR("/设置 隔离目录");
    setSettingsDirEnv(isolatedDir);

    const String expectedFile = Dir::cleanPath(Dir(isolatedDir).filePath(ANIME_STR("settings.json")));

    // 1. Write top-level and grouped settings (matching theme.cpp and backendsession.cpp usage)
    {
        Settings settings;
        CHECK(settings.fileName() == expectedFile);

        // Default values before writing
        CHECK(settings.value(ANIME_STR("theme/mode")).toString().isEmpty());
        CHECK(settings.value(ANIME_STR("theme/mode"), ANIME_STR("dark")).toString() == ANIME_STR("dark"));

        settings.setValue(ANIME_STR("theme/mode"), ANIME_STR("light"));

        settings.beginGroup(ANIME_STR("timeline"));
        CHECK(settings.group() == ANIME_STR("timeline"));
        CHECK(settings.value(ANIME_STR("dockArea"), ANIME_STR("bottom")).toString() == ANIME_STR("bottom"));
        CHECK(settings.value(ANIME_STR("collapsed"), false).toBool() == false);
        CHECK(settings.value(ANIME_STR("lastExtent"), 220).toInt() == 220);

        settings.setValue(ANIME_STR("dockArea"), ANIME_STR("left"));
        settings.setValue(ANIME_STR("collapsed"), true);
        settings.setValue(ANIME_STR("lastExtent"), 360);
        settings.endGroup();
        CHECK(settings.group().isEmpty());

        // Nested groups and Chinese values
        settings.beginGroup(ANIME_STR("ui"));
        settings.beginGroup(ANIME_STR("canvas"));
        CHECK(settings.group() == ANIME_STR("ui/canvas"));
        settings.setValue(ANIME_STR("label"), ANIME_STR("画布 标题"));
        settings.setValue(ANIME_STR("zoom"), 1.75);
        settings.endGroup();
        settings.endGroup();

        // Extra endGroup on empty stack must be a safe no-op
        settings.endGroup();
    }

    CHECK(File::exists(expectedFile));

    // 2. Read back in a fresh Settings instance
    {
        Settings settings;
        CHECK(settings.value(ANIME_STR("theme/mode")).toString() == ANIME_STR("light"));

        settings.beginGroup(ANIME_STR("timeline"));
        CHECK(settings.value(ANIME_STR("dockArea"), ANIME_STR("bottom")).toString() == ANIME_STR("left"));
        CHECK(settings.value(ANIME_STR("collapsed"), false).toBool() == true);
        CHECK(settings.value(ANIME_STR("lastExtent"), 220).toInt() == 360);
        settings.endGroup();

        // Grouped keys must also be accessible via slash path and vice versa
        CHECK(settings.value(ANIME_STR("timeline/dockArea")).toString() == ANIME_STR("left"));
        CHECK(settings.value(ANIME_STR("ui/canvas/label")).toString() == ANIME_STR("画布 标题"));
        CHECK(settings.value(ANIME_STR("ui/canvas/zoom")).toDouble() == 1.75);

        // Missing keys return defaults without modifying stored settings
        CHECK(settings.value(ANIME_STR("nonexistent/key"), ANIME_STR("fallback")).toString() == ANIME_STR("fallback"));
        CHECK(settings.value(ANIME_STR("nonexistent/int"), 42).toInt() == 42);
        CHECK(settings.value(ANIME_STR("nonexistent/bool"), true).toBool() == true);
        CHECK(settings.value(ANIME_STR("nonexistent/double"), 3.5).toDouble() == 3.5);
        CHECK(settings.value(ANIME_STR("nonexistent/undef")).isUndefined());
    }
}

static void testCorruptedSettingsFile()
{
    QTemporaryDir tempDir;
    CHECK(tempDir.isValid());
    const String isolatedDir = toCore(tempDir.path()) + ANIME_STR("/损坏测试");
    setSettingsDirEnv(isolatedDir);
    CHECK(Dir().mkpath(isolatedDir));

    const String settingsFile = Dir(isolatedDir).filePath(ANIME_STR("settings.json"));

    // 1. Write malformed JSON bytes
    {
        File f(settingsFile);
        CHECK(f.open(IODevice::WriteOnly | IODevice::Truncate));
        CHECK(f.write("{ \"theme/mode\": \"dark\", broken json !!!") > 0);
        f.close();
    }

    // Loading corrupted file must not crash and must act as empty settings
    {
        Settings settings;
        CHECK(settings.value(ANIME_STR("theme/mode"), ANIME_STR("default_mode")).toString() == ANIME_STR("default_mode"));
        settings.setValue(ANIME_STR("theme/mode"), ANIME_STR("recovered"));
        settings.sync();
    }

    // Verify recovery overwrote the corrupted file cleanly
    {
        Settings settings;
        CHECK(settings.value(ANIME_STR("theme/mode")).toString() == ANIME_STR("recovered"));
    }

    // 2. Write a non-object JSON root (e.g. array)
    {
        File f(settingsFile);
        CHECK(f.open(IODevice::WriteOnly | IODevice::Truncate));
        CHECK(f.write("[1, 2, 3]") > 0);
        f.close();
    }
    {
        Settings settings;
        CHECK(settings.value(ANIME_STR("theme/mode"), ANIME_STR("fallback")).toString() == ANIME_STR("fallback"));
    }

    // 3. Write an empty (0-byte) file
    {
        File f(settingsFile);
        CHECK(f.open(IODevice::WriteOnly | IODevice::Truncate));
        f.close();
    }
    {
        Settings settings;
        CHECK(settings.value(ANIME_STR("any"), 99).toInt() == 99);
    }
}

static void testEnvIsolationSwitching()
{
    QTemporaryDir dirA;
    QTemporaryDir dirB;
    CHECK(dirA.isValid());
    CHECK(dirB.isValid());

    setSettingsDirEnv(toCore(dirA.path()));
    {
        Settings sA;
        sA.setValue(ANIME_STR("env"), ANIME_STR("A"));
        sA.sync();
    }

    setSettingsDirEnv(toCore(dirB.path()));
    {
        Settings sB;
        // Must not see dirA's settings
        CHECK(sB.value(ANIME_STR("env"), ANIME_STR("none")).toString() == ANIME_STR("none"));
        sB.setValue(ANIME_STR("env"), ANIME_STR("B"));
        sB.sync();
    }

    setSettingsDirEnv(toCore(dirA.path()));
    {
        Settings sA;
        CHECK(sA.value(ANIME_STR("env")).toString() == ANIME_STR("A"));
    }
}

int main()
{
    testWriteReadAndGroups();
    testCorruptedSettingsFile();
    testEnvIsolationSwitching();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s) in settings_tests\n", g_failures);
        return 1;
    }
    std::printf("settings_tests passed\n");
    return 0;
}
