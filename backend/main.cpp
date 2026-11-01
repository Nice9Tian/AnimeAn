#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "algorithm/core/color.h"
#include "algorithm/core/containers.h"
#include "algorithm/core/file.h"
#include "algorithm/core/geometry.h"
#include "algorithm/core/image.h"
#include "algorithm/core/json.h"
#include "algorithm/core/path.h"
#include "algorithm/core/string.h"
#include "net/wsserver.h"
#include "platform/eventloop.h"
#include "session/backendsession.h"
#include "theme.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

#ifdef ANIMEAN_WITH_PYTHON
#ifdef slots
#undef slots
#endif
#include <pybind11/embed.h>

#include "pythonbind/python_bindings.h"

namespace py = pybind11;

static AnimeCore::String pythonStartupText()
{
    try {
        py::module_::import("animean_python");
        const std::string text = py::module_::import("hello_world").attr("hello_world")().cast<std::string>();
        return AnimeCore::String::fromUtf8(text.c_str());
    } catch (const py::error_already_set &error) {
        return AnimeCore::String("Python error: %1").arg(AnimeCore::String::fromUtf8(error.what()));
    }
}

static AnimeCore::String getWideEnv(const wchar_t *name)
{
    const DWORD needed = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) {
        return AnimeCore::String();
    }
    std::wstring buf(needed, L'\0');
    const DWORD written = ::GetEnvironmentVariableW(name, buf.data(), needed);
    if (written == 0 || written >= needed) {
        return AnimeCore::String();
    }
    buf.resize(written);
    return AnimeCore::String(std::u16string(buf.begin(), buf.end()));
}

static void setWideEnv(const wchar_t *name, const AnimeCore::String &value)
{
    const std::u16string u16 = value.u16String();
    const std::wstring wval(u16.begin(), u16.end());
    ::SetEnvironmentVariableW(name, wval.c_str());
    ::_wputenv_s(name, wval.c_str());
}
#endif

using namespace AnimeCore;

int main(int argc, char *argv[])
{
    std::uint16_t port = 0;
    String token;
    for (int i = 1; i < argc; ++i) {
        const String arg = String::fromUtf8(argv[i]);
        if (arg == String("--port") && i + 1 < argc) {
            bool ok = false;
            const unsigned int parsed = String::fromUtf8(argv[++i]).toUInt(&ok);
            if (ok && parsed <= 65535) {
                port = static_cast<std::uint16_t>(parsed);
            }
        } else if (arg.startsWith(String("--port="))) {
            bool ok = false;
            const unsigned int parsed = arg.mid(7).toUInt(&ok);
            if (ok && parsed <= 65535) {
                port = static_cast<std::uint16_t>(parsed);
            }
        } else if (arg == String("--token") && i + 1 < argc) {
            token = String::fromUtf8(argv[++i]);
        } else if (arg.startsWith(String("--token="))) {
            token = arg.mid(8);
        }
    }

    (void)AnimeTheme::instance();
    const AnimeTheme::Mode initialThemeMode = AnimeTheme::mode();
#ifdef ANIMEAN_WITH_PYTHON
    setAnimeanUiTheme(AnimeTheme::modeName(initialThemeMode));

    const String appDir = CoreApplication::applicationDirPath();
    String pythonHome = Dir(appDir).filePath(String(ANIMEAN_PYTHON_DIR_NAME));
    if (!FileInfo::exists(Dir(pythonHome).filePath(String("python312.dll")))) {
        pythonHome = String(ANIMEAN_PYTHON_HOME);
    }

    StringList pythonPath;
    if (FileInfo::exists(Dir(String(ANIMEAN_PYFILE_DIR)).filePath(String("hello_world.py")))) {
        pythonPath << String(ANIMEAN_PYFILE_DIR);
    }
    pythonPath << appDir;
    if (FileInfo::exists(Dir(String(ANIMEAN_PYTHONBIND_DIR)).filePath(String("animemodel.py")))) {
        pythonPath << String(ANIMEAN_PYTHONBIND_DIR);
    }
    if (FileInfo::exists(Dir(String(ANIMEAN_OPENTOONZ_TOOLS_DIR)).filePath(String("toonz_to_dict.py")))) {
        pythonPath << String(ANIMEAN_OPENTOONZ_TOOLS_DIR);
    }

    StringList dllPath;
    dllPath << pythonHome << Dir(pythonHome).filePath(String("DLLs"));
    const String existingPath = getWideEnv(L"PATH");
    if (!existingPath.isEmpty()) {
        dllPath << existingPath;
    }

    setWideEnv(L"PATH", Dir::toNativeSeparators(dllPath.join(Latin1Char(';'))));
    setWideEnv(L"PYTHONHOME", Dir::toNativeSeparators(pythonHome));
    setWideEnv(L"PYTHONPATH", Dir::toNativeSeparators(pythonPath.join(Latin1Char(';'))));

    py::scoped_interpreter guard;
#endif

    WsServer ws(token);
    BackendSession session(&ws);
    session.initializePythonStartup();

#ifdef ANIMEAN_WITH_PYTHON
    session.setStatusText(pythonStartupText());
#endif

    if (!ws.listen(port)) {
        std::fprintf(stderr, "Failed to listen on port %u\n", static_cast<unsigned int>(port));
        std::fflush(stderr);
        return 1;
    }

    std::printf("ANIMEAN_BACKEND_PORT=%u\n", static_cast<unsigned int>(ws.port()));
    std::fflush(stdout);

    return EventLoop::instance().exec();
}
