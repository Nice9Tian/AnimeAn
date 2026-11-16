#pragma once

// The one place algorithm/core touches the operating system: files, folders,
// the process and its environment. Internal to algorithm/core. file.cpp,
// fileinfo.cpp and settings.cpp hold the behaviour and call down to here;
// native_win32.cpp and native_posix.cpp each implement all of it.
//
// Paths arrive as the caller wrote them (either slash, relative or absolute).
// Functions that can fail for an outside reason report it as text in
// *errorText, worded by the system.

#include "algorithm/core/string.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace AnimeCore {
namespace Native {

// ---- files -------------------------------------------------------------------

struct FileStatus {
    bool isDir = false;
    std::uint64_t size = 0;
};

// False when nothing is there.
bool fileStatus(const String &path, FileStatus *out);

enum class Creation {
    OpenExisting,     // must exist
    TruncateExisting, // must exist, emptied
    CreateNew,        // must not exist
    CreateAlways,     // created or emptied
    OpenAlways,       // created when missing, kept otherwise
};

enum class OpenError { None, AlreadyExists, Other };

struct OpenRequest {
    bool read = false;
    bool write = false;
    Creation creation = Creation::OpenExisting;
    // Nobody else may write while this handle is open (Windows share mode;
    // other systems have no such lock and ignore it).
    bool exclusiveWrite = false;
};

// nullptr on failure, with *error and *errorText set. A folder never opens.
void *openFile(const String &path, const OpenRequest &request, OpenError *error, String *errorText);
void closeFile(void *handle);
bool seekToEnd(void *handle);
bool fileSize(void *handle, std::uint64_t *size);
bool filePosition(void *handle, std::uint64_t *position);
// *bytesRead is 0 at the end of the file.
bool readFile(void *handle, char *buffer, std::size_t capacity, std::size_t *bytesRead, String *errorText);
bool writeFile(void *handle, const char *data, std::size_t size, std::size_t *bytesWritten, String *errorText);
// Returns once the data is on the disk, not just handed to the system.
bool flushFile(void *handle, String *errorText);

bool removeFile(const String &path, String *errorText);
// Replaces `to` when it exists; on one volume a reader sees the old file or the new one, never a mix.
bool renameReplacing(const String &from, const String &to, String *errorText);

enum class MakeDirResult { Created, AlreadyExists, Failed };
MakeDirResult makeDirectory(const String &path);

struct DirEntry {
    String name;
    bool isDir = false;
    bool hidden = false;
    bool system = false;
    std::uint64_t size = 0;
    std::uint64_t modified = 0; // larger is later; the unit differs per system
};

// Every entry including "." and "..", in the order the system gives them.
bool listDirectory(const String &path, std::vector<DirEntry> *out);

// True where a text-mode file stores "\n" as "\r\n" (Windows).
extern const bool textModeWritesCrLf;

// ---- process -----------------------------------------------------------------

String currentDirectory();
String tempDirectory();
// Where per-user settings go: %APPDATA% on Windows, $XDG_CONFIG_HOME (by default
// ~/.config) elsewhere. Empty when the system does not say.
String userConfigDirectory();
// Full path of the running program, empty when the system cannot tell.
String executablePath();
std::uint64_t processId();
// Milliseconds on a clock that never goes back.
std::uint64_t tickCount();

} // namespace Native
} // namespace AnimeCore
