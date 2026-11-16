#include "pythonbind/python_environment.h"

#include "algorithm/core/environment.h"
#include "algorithm/core/file.h"
#include "algorithm/core/string.h"

using namespace AnimeCore;

namespace {

// ANIMEAN_PYTHON_RUNTIME_PROBE names a file every usable runtime folder has
// (the build knows the layout and the version, see backend/CMakeLists.txt).
bool isUsableRuntime(const String &home)
{
    return FileInfo::exists(Dir(home).filePath(String(ANIMEAN_PYTHON_RUNTIME_PROBE)));
}

String chooseRuntimeHome(const String &appDir, PythonRuntimeChoice choice)
{
    const String bundled = Dir(appDir).filePath(String(ANIMEAN_PYTHON_DIR_NAME));
    const String configured = String(ANIMEAN_PYTHON_HOME);
    if (choice == PythonRuntimeChoice::PreferBundled)
        return isUsableRuntime(bundled) ? bundled : configured;
    return isUsableRuntime(configured) ? configured : bundled;
}

StringList scriptFolders(const String &appDir)
{
    StringList folders;
    if (FileInfo::exists(Dir(String(ANIMEAN_PYFILE_DIR)).filePath(String("hello_world.py")))) {
        folders << String(ANIMEAN_PYFILE_DIR);
    }
    if (FileInfo::exists(Dir(String(ANIMEAN_PYTHONBIND_DIR)).filePath(String("animemodel.py")))) {
        folders << String(ANIMEAN_PYTHONBIND_DIR);
    }
    if (FileInfo::exists(Dir(String(ANIMEAN_OPENTOONZ_TOOLS_DIR)).filePath(String("toonz_to_dict.py")))) {
        folders << String(ANIMEAN_OPENTOONZ_TOOLS_DIR);
    }
    folders << appDir;
    return folders;
}

// The runtime's folders that go to the front of PATH.
StringList runtimeSearchFolders(const String &pythonHome)
{
    StringList folders;
#ifdef _WIN32
    // Where Windows looks for the DLLs the interpreter and its extension modules load.
    folders << pythonHome << Dir(pythonHome).filePath(String("DLLs"));
#else
    // So a script that starts "python3" gets this runtime, not whatever the system has.
    folders << Dir(pythonHome).filePath(String("bin"));
#endif
    return folders;
}

} // namespace

void preparePythonEnvironment(PythonRuntimeChoice choice)
{
    const String appDir = CoreApplication::applicationDirPath();
    const String pythonHome = chooseRuntimeHome(appDir, choice);
    const Char separator = pathListSeparator();

    StringList searchPath = runtimeSearchFolders(pythonHome);
    const String existingPath = environmentVariable(String("PATH"));
    if (!existingPath.isEmpty()) {
        searchPath << existingPath;
    }

    setEnvironmentVariable(String("PATH"), Dir::toNativeSeparators(searchPath.join(separator)));
    setEnvironmentVariable(String("PYTHONHOME"), Dir::toNativeSeparators(pythonHome));
    setEnvironmentVariable(String("PYTHONPATH"),
                           Dir::toNativeSeparators(scriptFolders(appDir).join(separator)));
}
