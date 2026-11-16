// File and SaveFile. The behaviour lives here and is the same on every system;
// the system calls are in native_win32.cpp / native_posix.cpp (see native.h).
// FileInfo and Dir are in fileinfo.cpp.

#include "algorithm/core/file.h"

#include "algorithm/core/native.h"
#include "algorithm/core/pathtext.h"

#include <cstring>

namespace AnimeCore {

namespace {

constexpr std::size_t kReadChunkSize = 65536;

Native::Creation creationFor(IODevice::OpenMode mode, bool read, bool write) noexcept
{
    if (mode & IODevice::NewOnly)
        return Native::Creation::CreateNew;
    if (mode & IODevice::ExistingOnly) {
        return ((mode & IODevice::Truncate) && write) ? Native::Creation::TruncateExisting
                                                      : Native::Creation::OpenExisting;
    }
    if (write) {
        if ((mode & IODevice::Truncate) || (!read && !(mode & IODevice::Append)))
            return Native::Creation::CreateAlways;
        return Native::Creation::OpenAlways;
    }
    return Native::Creation::OpenExisting;
}

// Writes all of `data`. In text mode "\n" goes out as "\r\n" where the system
// stores text that way. False with *errorText set when the system refuses.
bool writeAll(void *handle, const char *data, std::size_t size, bool textMode, String *errorText)
{
    std::string textBuf;
    const char *ptr = data;
    std::size_t remaining = size;

    if (textMode && Native::textModeWritesCrLf) {
        textBuf.reserve(remaining + remaining / 16);
        for (std::size_t i = 0; i < remaining; ++i) {
            if (data[i] == '\n')
                textBuf.push_back('\r');
            textBuf.push_back(data[i]);
        }
        ptr = textBuf.data();
        remaining = textBuf.size();
    }

    while (remaining > 0) {
        std::size_t written = 0;
        if (!Native::writeFile(handle, ptr, remaining, &written, errorText))
            return false;
        if (written == 0) {
            // The system took nothing and reported no error: trying again would never end.
            if (errorText)
                *errorText = ANIME_STR("The file accepted no more data");
            return false;
        }
        ptr += written;
        remaining -= written;
    }
    return true;
}

} // namespace

// ============================================================================
// File
// ============================================================================

File::File(const String &name)
    : m_fileName(name)
{
}

File::~File()
{
    close();
}

File::File(File &&other) noexcept
    : m_fileName(std::move(other.m_fileName))
    , m_errorString(std::move(other.m_errorString))
    , m_handle(other.m_handle)
    , m_isOpen(other.m_isOpen)
    , m_readable(other.m_readable)
    , m_writable(other.m_writable)
    , m_textMode(other.m_textMode)
{
    other.m_handle = nullptr;
    other.m_isOpen = false;
    other.m_readable = false;
    other.m_writable = false;
    other.m_textMode = false;
}

File &File::operator=(File &&other) noexcept
{
    if (this != &other) {
        close();
        m_fileName = std::move(other.m_fileName);
        m_errorString = std::move(other.m_errorString);
        m_handle = other.m_handle;
        m_isOpen = other.m_isOpen;
        m_readable = other.m_readable;
        m_writable = other.m_writable;
        m_textMode = other.m_textMode;
        other.m_handle = nullptr;
        other.m_isOpen = false;
        other.m_readable = false;
        other.m_writable = false;
        other.m_textMode = false;
    }
    return *this;
}

void File::setFileName(const String &name)
{
    if (m_isOpen)
        close();
    m_fileName = name;
}

bool File::exists() const
{
    return exists(m_fileName);
}

bool File::exists(const String &fileName)
{
    return FileInfo::exists(fileName);
}

bool File::remove()
{
    if (m_fileName.isEmpty()) {
        m_errorString = ANIME_STR("No file name specified");
        return false;
    }
    close();
    String errorText;
    if (Native::removeFile(m_fileName, &errorText)) {
        m_errorString = ANIME_STR("No error");
        return true;
    }
    m_errorString = errorText;
    return false;
}

bool File::remove(const String &fileName)
{
    File f(fileName);
    return f.remove();
}

bool File::open(OpenMode mode)
{
    if (m_isOpen)
        close();

    if (m_fileName.isEmpty()) {
        m_errorString = ANIME_STR("No file name specified");
        return false;
    }

    const bool read = (mode & ReadOnly) != 0;
    const bool write = (mode & WriteOnly) != 0 || (mode & Append) != 0;
    if (!read && !write) {
        m_errorString = ANIME_STR("Open mode not specified");
        return false;
    }
    if ((mode & NewOnly) && (mode & ExistingOnly)) {
        m_errorString = ANIME_STR("Invalid open mode");
        return false;
    }

    Native::OpenRequest request;
    request.read = read;
    request.write = write;
    request.creation = creationFor(mode, read, write);

    Native::OpenError openError = Native::OpenError::None;
    String errorText;
    void *handle = Native::openFile(m_fileName, request, &openError, &errorText);
    if (!handle) {
        m_errorString = errorText;
        return false;
    }

    if (mode & Append)
        Native::seekToEnd(handle);

    m_handle = handle;
    m_isOpen = true;
    m_readable = read;
    m_writable = write;
    m_textMode = (mode & Text) != 0;
    m_errorString = ANIME_STR("No error");
    return true;
}

void File::close()
{
    if (!m_isOpen)
        return;
    if (m_handle) {
        Native::closeFile(m_handle);
        m_handle = nullptr;
    }
    m_isOpen = false;
    m_readable = false;
    m_writable = false;
    m_textMode = false;
}

std::int64_t File::size() const
{
    if (m_isOpen && m_handle) {
        std::uint64_t openSize = 0;
        if (Native::fileSize(m_handle, &openSize))
            return static_cast<std::int64_t>(openSize);
    }
    return FileInfo(m_fileName).size();
}

std::string File::readAll()
{
    if (!m_isOpen || !m_readable) {
        m_errorString = ANIME_STR("Device not open for reading");
        return std::string();
    }

    std::string raw;
    std::uint64_t total = 0;
    std::uint64_t position = 0;
    if (Native::fileSize(m_handle, &total) && total > 0
        && Native::filePosition(m_handle, &position) && total > position) {
        raw.reserve(static_cast<std::size_t>(total - position));
    }

    char buf[kReadChunkSize];
    for (;;) {
        std::size_t bytesRead = 0;
        String errorText;
        if (!Native::readFile(m_handle, buf, sizeof(buf), &bytesRead, &errorText)) {
            m_errorString = errorText;
            break;
        }
        if (bytesRead == 0)
            break;
        raw.append(buf, bytesRead);
    }

    // Text mode on the way in is the same everywhere: "\r\n" reads as "\n",
    // whichever system wrote the file.
    if (m_textMode && !raw.empty()) {
        std::string filtered;
        filtered.reserve(raw.size());
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n')
                continue;
            filtered.push_back(raw[i]);
        }
        return filtered;
    }
    return raw;
}

std::int64_t File::write(const char *data, std::int64_t len)
{
    if (!m_isOpen || !m_writable) {
        m_errorString = ANIME_STR("Device not open for writing");
        return -1;
    }
    if (len <= 0 || !data)
        return 0;

    String errorText;
    if (!writeAll(m_handle, data, static_cast<std::size_t>(len), m_textMode, &errorText)) {
        m_errorString = errorText;
        return -1;
    }
    return len;
}

std::int64_t File::write(const char *data)
{
    if (!data)
        return 0;
    return write(data, static_cast<std::int64_t>(std::strlen(data)));
}

std::int64_t File::write(const std::string &data)
{
    return write(data.data(), static_cast<std::int64_t>(data.size()));
}

std::int64_t File::write(const std::vector<unsigned char> &data)
{
    return write(reinterpret_cast<const char *>(data.data()), static_cast<std::int64_t>(data.size()));
}

// ============================================================================
// SaveFile
// ============================================================================

SaveFile::SaveFile(const String &name)
    : m_fileName(name)
{
}

SaveFile::~SaveFile()
{
    discardTempFile();
}

void SaveFile::setFileName(const String &name)
{
    discardTempFile();
    m_fileName = name;
}

bool SaveFile::open(OpenMode mode)
{
    discardTempFile();
    m_writeError = false;
    m_cancelled = false;

    if (m_fileName.isEmpty()) {
        m_errorString = ANIME_STR("No file name specified");
        return false;
    }
    if ((mode & ReadOnly) != 0 || (mode & WriteOnly) == 0) {
        m_errorString = ANIME_STR("SaveFile requires WriteOnly mode without ReadOnly");
        return false;
    }

    const FileInfo targetInfo(m_fileName);
    if (targetInfo.isDir() || PathText::hasTrailingSeparatorNotRoot(m_fileName)) {
        m_errorString = ANIME_STR("Target path is a directory");
        return false;
    }

    const String parentDir = targetInfo.absolutePath();
    if (!FileInfo(parentDir).isDir()) {
        m_errorString = ANIME_STR("Parent directory does not exist");
        return false;
    }

    // The temporary file sits beside the target, so the final rename stays on
    // one volume. Its name carries the process id and a clock reading; a name
    // that is taken moves on to the next reading.
    const String absTarget = targetInfo.absoluteFilePath();
    const std::uint64_t pid = Native::processId();
    const std::uint64_t tick = Native::tickCount();

    Native::OpenRequest request;
    request.write = true;
    request.creation = Native::Creation::CreateNew;
    request.exclusiveWrite = true;

    void *handle = nullptr;
    String chosenTemp;
    for (int attempt = 0; attempt < 64; ++attempt) {
        const String candidate = absTarget + ANIME_STR(".tmp.")
            + String::number(static_cast<unsigned long long>(pid)) + ANIME_STR(".")
            + String::number(static_cast<unsigned long long>(tick + attempt));
        Native::OpenError openError = Native::OpenError::None;
        String errorText;
        handle = Native::openFile(candidate, request, &openError, &errorText);
        if (handle) {
            chosenTemp = candidate;
            break;
        }
        if (openError != Native::OpenError::AlreadyExists) {
            m_errorString = errorText;
            return false;
        }
    }

    if (!handle) {
        m_errorString = ANIME_STR("Failed to create temporary file");
        return false;
    }

    m_handle = handle;
    m_tempFileName = chosenTemp;
    m_isOpen = true;
    m_textMode = (mode & Text) != 0;
    m_errorString = ANIME_STR("No error");
    return true;
}

std::int64_t SaveFile::write(const char *data, std::int64_t len)
{
    if (!m_isOpen || !m_handle) {
        m_errorString = ANIME_STR("SaveFile is not open");
        return -1;
    }
    if (m_cancelled) {
        m_errorString = ANIME_STR("Writing was cancelled");
        return -1;
    }
    if (len <= 0 || !data)
        return 0;

    String errorText;
    if (!writeAll(m_handle, data, static_cast<std::size_t>(len), m_textMode, &errorText)) {
        m_writeError = true;
        m_errorString = errorText;
        return -1;
    }
    return len;
}

std::int64_t SaveFile::write(const char *data)
{
    if (!data)
        return 0;
    return write(data, static_cast<std::int64_t>(std::strlen(data)));
}

std::int64_t SaveFile::write(const std::string &data)
{
    return write(data.data(), static_cast<std::int64_t>(data.size()));
}

std::int64_t SaveFile::write(const std::vector<unsigned char> &data)
{
    return write(reinterpret_cast<const char *>(data.data()), static_cast<std::int64_t>(data.size()));
}

void SaveFile::cancelWriting()
{
    if (!m_isOpen)
        return;
    m_cancelled = true;
    m_errorString = ANIME_STR("Writing was cancelled");
}

bool SaveFile::commit()
{
    if (!m_isOpen || !m_handle) {
        return false;
    }

    String flushErrorText;
    const bool flushOk = Native::flushFile(m_handle, &flushErrorText);
    Native::closeFile(m_handle);
    m_handle = nullptr;
    m_isOpen = false;

    if (m_cancelled || m_writeError || !flushOk) {
        if (!flushOk && !m_cancelled && !m_writeError)
            m_errorString = flushErrorText;
        Native::removeFile(m_tempFileName, nullptr);
        m_tempFileName.clear();
        return false;
    }

    String renameErrorText;
    if (!Native::renameReplacing(m_tempFileName, m_fileName, &renameErrorText)) {
        m_errorString = renameErrorText;
        Native::removeFile(m_tempFileName, nullptr);
        m_tempFileName.clear();
        return false;
    }

    m_tempFileName.clear();
    m_errorString = ANIME_STR("No error");
    return true;
}

void SaveFile::discardTempFile()
{
    if (m_handle) {
        Native::closeFile(m_handle);
        m_handle = nullptr;
    }
    if (!m_tempFileName.isEmpty()) {
        Native::removeFile(m_tempFileName, nullptr);
        m_tempFileName.clear();
    }
    m_isOpen = false;
    m_textMode = false;
}

} // namespace AnimeCore
