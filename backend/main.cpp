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
#include "platform/process.h"
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
#include "pythonbind/python_environment.h"

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
#endif

using namespace AnimeCore;

int main(int argc, char *argv[])
{
    Process::optOutOfPowerThrottling();
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

    preparePythonEnvironment(PythonRuntimeChoice::PreferBundled);

    // No working-directory entry at the front of sys.path: started from Explorer
    // the working directory is the exe folder, and that entry would put the
    // copies beside the exe ahead of the source folders.
    py::scoped_interpreter guard(true, 0, nullptr, false);
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
