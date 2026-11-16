// POSIX side of native.h and environment.h. Linux is the system this is built
// and tested on. Text is UTF-8 at the system boundary.

#include "algorithm/core/native.h"

#include "algorithm/core/environment.h"
#include "algorithm/core/pathtext.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace AnimeCore {

namespace {

// The file layer reads both slashes as separators on every system (pathtext.h),
// so a path goes to the system with forward slashes only.
std::string toSystemPath(const String &path)
{
    return PathText::toForwardSlashes(path).toUtf8();
}

void setErrorText(String *errorText, int err)
{
    if (errorText)
        *errorText = String::fromUtf8(std::strerror(err));
}

// A handle is the descriptor plus one, so that descriptor 0 is not read as "no handle".
void *toHandle(int fd) noexcept
{
    return reinterpret_cast<void *>(static_cast<std::intptr_t>(fd) + 1);
}

int toFd(void *handle) noexcept
{
    return static_cast<int>(reinterpret_cast<std::intptr_t>(handle) - 1);
}

int openFlags(const Native::OpenRequest &request) noexcept
{
    int flags = O_CLOEXEC;
    if (request.read && request.write)
        flags |= O_RDWR;
    else if (request.write)
        flags |= O_WRONLY;
    else
        flags |= O_RDONLY;

    switch (request.creation) {
    case Native::Creation::OpenExisting:
        break;
    case Native::Creation::TruncateExisting:
        flags |= O_TRUNC;
        break;
    case Native::Creation::CreateNew:
        flags |= O_CREAT | O_EXCL;
        break;
    case Native::Creation::CreateAlways:
        flags |= O_CREAT | O_TRUNC;
        break;
    case Native::Creation::OpenAlways:
        flags |= O_CREAT;
        break;
    }
    return flags;
}

std::uint64_t modifiedStamp(const struct stat &st) noexcept
{
    return static_cast<std::uint64_t>(st.st_mtim.tv_sec) * 1000000000ULL
           + static_cast<std::uint64_t>(st.st_mtim.tv_nsec);
}

} // namespace

namespace Native {

const bool textModeWritesCrLf = false;

bool fileStatus(const String &path, FileStatus *out)
{
    if (path.isEmpty())
        return false;
    struct stat st {};
    if (::stat(toSystemPath(path).c_str(), &st) != 0)
        return false;
    out->isDir = S_ISDIR(st.st_mode);
    out->size = static_cast<std::uint64_t>(st.st_size);
    return true;
}

void *openFile(const String &path, const OpenRequest &request, OpenError *error, String *errorText)
{
    const std::string systemPath = toSystemPath(path);
    // O_NONBLOCK for the open itself only: without it, opening a named pipe
    // waits until something opens the other end, and the program hangs.
    int fd = -1;
    do {
        fd = ::open(systemPath.c_str(), openFlags(request) | O_NONBLOCK, 0666);
    } while (fd < 0 && errno == EINTR);

    // open() reads a folder happily; a file handle on one is never what the caller meant.
    if (fd >= 0) {
        struct stat st {};
        if (::fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
            ::close(fd);
            fd = -1;
            errno = EISDIR;
        }
    }
    if (fd >= 0) {
        // Reads and writes wait as usual.
        const int statusFlags = ::fcntl(fd, F_GETFL, 0);
        if (statusFlags >= 0)
            ::fcntl(fd, F_SETFL, statusFlags & ~O_NONBLOCK);
    }

    if (fd < 0) {
        const int err = errno;
        if (error)
            *error = (err == EEXIST) ? OpenError::AlreadyExists : OpenError::Other;
        setErrorText(errorText, err);
        return nullptr;
    }
    if (error)
        *error = OpenError::None;
    return toHandle(fd);
}

void closeFile(void *handle)
{
    if (handle)
        ::close(toFd(handle));
}

bool seekToEnd(void *handle)
{
    return ::lseek(toFd(handle), 0, SEEK_END) >= 0;
}

bool fileSize(void *handle, std::uint64_t *size)
{
    struct stat st {};
    if (::fstat(toFd(handle), &st) != 0)
        return false;
    *size = static_cast<std::uint64_t>(st.st_size);
    return true;
}

bool filePosition(void *handle, std::uint64_t *position)
{
    const off_t current = ::lseek(toFd(handle), 0, SEEK_CUR);
    if (current < 0)
        return false;
    *position = static_cast<std::uint64_t>(current);
    return true;
}

bool readFile(void *handle, char *buffer, std::size_t capacity, std::size_t *bytesRead, String *errorText)
{
    ssize_t got = -1;
    do {
        got = ::read(toFd(handle), buffer, capacity);
    } while (got < 0 && errno == EINTR);
    if (got < 0) {
        setErrorText(errorText, errno);
        return false;
    }
    *bytesRead = static_cast<std::size_t>(got);
    return true;
}

bool writeFile(void *handle, const char *data, std::size_t size, std::size_t *bytesWritten, String *errorText)
{
    ssize_t put = -1;
    do {
        put = ::write(toFd(handle), data, size);
    } while (put < 0 && errno == EINTR);
    if (put < 0) {
        setErrorText(errorText, errno);
        return false;
    }
    *bytesWritten = static_cast<std::size_t>(put);
    return true;
}

bool flushFile(void *handle, String *errorText)
{
    int result = -1;
    do {
        result = ::fsync(toFd(handle));
    } while (result != 0 && errno == EINTR);
    if (result == 0)
        return true;
    setErrorText(errorText, errno);
    return false;
}

bool removeFile(const String &path, String *errorText)
{
    if (::unlink(toSystemPath(path).c_str()) == 0)
        return true;
    setErrorText(errorText, errno);
    return false;
}

bool renameReplacing(const String &from, const String &to, String *errorText)
{
    const std::string source = toSystemPath(from);
    const std::string target = toSystemPath(to);

    // The file that gets replaced keeps its permissions: one that was made
    // private stays private, instead of taking the defaults of a new file.
    struct stat replaced {};
    if (::stat(target.c_str(), &replaced) == 0 && S_ISREG(replaced.st_mode))
        ::chmod(source.c_str(), replaced.st_mode & 07777);

    if (::rename(source.c_str(), target.c_str()) == 0)
        return true;
    setErrorText(errorText, errno);
    return false;
}

MakeDirResult makeDirectory(const String &path)
{
    if (::mkdir(toSystemPath(path).c_str(), 0777) == 0)
        return MakeDirResult::Created;
    return errno == EEXIST ? MakeDirResult::AlreadyExists : MakeDirResult::Failed;
}

bool listDirectory(const String &path, std::vector<DirEntry> *out)
{
    DIR *dir = ::opendir(toSystemPath(path).c_str());
    if (!dir)
        return false;

    const int dirFd = ::dirfd(dir);
    bool complete = true;
    for (;;) {
        // readdir answers nullptr both at the end and on an error; errno tells which.
        errno = 0;
        const struct dirent *item = ::readdir(dir);
        if (!item) {
            complete = (errno == 0);
            break;
        }
        DirEntry entry;
        entry.name = String::fromUtf8(item->d_name);
        const bool isDotEntry = std::strcmp(item->d_name, ".") == 0 || std::strcmp(item->d_name, "..") == 0;
        entry.hidden = item->d_name[0] == '.' && !isDotEntry;

        // A link counts as what it points at. One that points nowhere, a pipe,
        // a socket or a device is a system entry, which a plain listing leaves out.
        struct stat st {};
        if (::fstatat(dirFd, item->d_name, &st, 0) == 0) {
            entry.isDir = S_ISDIR(st.st_mode);
            entry.system = !S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode);
            entry.size = static_cast<std::uint64_t>(st.st_size);
            entry.modified = modifiedStamp(st);
        } else {
            entry.system = true;
        }
        out->push_back(std::move(entry));
    }

    ::closedir(dir);
    return complete;
}

String currentDirectory()
{
    std::string buffer(256, '\0');
    for (;;) {
        if (::getcwd(buffer.data(), buffer.size()))
            return String::fromUtf8(buffer.c_str());
        if (errno != ERANGE)
            return String();
        buffer.resize(buffer.size() * 2);
    }
}

String tempDirectory()
{
    const char *fromEnv = std::getenv("TMPDIR");
    if (fromEnv && fromEnv[0] != '\0')
        return String::fromUtf8(fromEnv);
    return ANIME_STR("/tmp");
}

String userConfigDirectory()
{
    // The XDG base directory rule: a relative $XDG_CONFIG_HOME does not count.
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] == '/')
        return String::fromUtf8(xdg);
    const char *home = std::getenv("HOME");
    if (home && home[0] != '\0')
        return String::fromUtf8(home) + ANIME_STR("/.config");
    return String();
}

String executablePath()
{
#if defined(__linux__)
    std::string buffer(256, '\0');
    for (;;) {
        const ssize_t length = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (length < 0)
            return String();
        if (static_cast<std::size_t>(length) < buffer.size())
            return String::fromUtf8(buffer.data(), static_cast<int>(length));
        buffer.resize(buffer.size() * 2);
    }
#else
    return String();
#endif
}

std::uint64_t processId()
{
    return static_cast<std::uint64_t>(::getpid());
}

std::uint64_t tickCount()
{
    struct timespec now {};
    ::clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::uint64_t>(now.tv_sec) * 1000ULL
           + static_cast<std::uint64_t>(now.tv_nsec) / 1000000ULL;
}

} // namespace Native

// ---- environment.h -------------------------------------------------------------

String environmentVariable(const String &name)
{
    const char *value = std::getenv(name.toUtf8().c_str());
    return value ? String::fromUtf8(value) : String();
}

bool setEnvironmentVariable(const String &name, const String &value)
{
    return ::setenv(name.toUtf8().c_str(), value.toUtf8().c_str(), 1) == 0;
}

Char pathListSeparator() noexcept
{
    return Char(u':');
}

} // namespace AnimeCore
