#include "algorithm/core/settings.h"

#include "algorithm/core/file.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <cstdlib>

namespace AnimeCore {

namespace {

String trimSlashes(const String &s)
{
    std::ptrdiff_t start = 0;
    std::ptrdiff_t end = s.size();
    while (start < end && (s.at(start) == Char(u'/') || s.at(start) == Char(u'\\')))
        ++start;
    while (end > start && (s.at(end - 1) == Char(u'/') || s.at(end - 1) == Char(u'\\')))
        --end;
    String out = s.mid(start, end - start);
    out.replace(Char(u'\\'), Char(u'/'));
    return out;
}

String readEnvVar(const wchar_t *name)
{
#ifdef _WIN32
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed > 0) {
        std::wstring buf(needed, L'\0');
        const DWORD written = GetEnvironmentVariableW(name, buf.data(), needed);
        if (written > 0 && written < needed) {
            buf.resize(written);
            return String::fromUtf16(reinterpret_cast<const char16_t *>(buf.data()),
                                     static_cast<int>(buf.size()));
        }
    }
    if (const wchar_t *val = _wgetenv(name)) {
        return String::fromUtf16(reinterpret_cast<const char16_t *>(val));
    }
#endif
    return String();
}

String resolveSettingsFilePath()
{
    const String customDir = readEnvVar(L"ANIMEAN_SETTINGS_DIR").trimmed();
    if (!customDir.isEmpty()) {
        return Dir::cleanPath(Dir(customDir).filePath(ANIME_STR("settings.json")));
    }

    const String appData = readEnvVar(L"APPDATA").trimmed();
    if (!appData.isEmpty()) {
        return Dir::cleanPath(Dir(appData).filePath(ANIME_STR("AnimeAn/settings.json")));
    }

    return Dir::cleanPath(Dir(Dir::currentPath()).filePath(ANIME_STR("AnimeAn/settings.json")));
}

} // namespace

Settings::Settings()
    : m_filePath(resolveSettingsFilePath())
{
    loadFromDisk();
}

Settings::~Settings()
{
    if (m_dirty)
        saveToDisk();
}

void Settings::beginGroup(const String &prefix)
{
    m_groupStack.push_back(trimSlashes(prefix));
}

void Settings::endGroup()
{
    if (!m_groupStack.empty())
        m_groupStack.pop_back();
}

String Settings::group() const
{
    String result;
    for (const String &part : m_groupStack) {
        if (part.isEmpty())
            continue;
        if (!result.isEmpty())
            result.append(Char(u'/'));
        result.append(part);
    }
    return result;
}

String Settings::fullKey(const String &key) const
{
    const String grp = group();
    const String cleanKey = trimSlashes(key);
    if (grp.isEmpty())
        return cleanKey;
    if (cleanKey.isEmpty())
        return grp;
    return grp + ANIME_STR("/") + cleanKey;
}

JsonValue Settings::value(const String &key, const JsonValue &defaultValue) const
{
    const String k = fullKey(key);
    if (k.isEmpty())
        return defaultValue;
    if (!m_data.contains(k))
        return defaultValue;
    const JsonValue v = m_data.value(k);
    if (v.isUndefined())
        return defaultValue;
    return v;
}

void Settings::setValue(const String &key, const JsonValue &val)
{
    const String k = fullKey(key);
    if (k.isEmpty())
        return;
    if (val.isUndefined()) {
        m_data.remove(k);
    } else {
        m_data.insert(k, val);
    }
    m_dirty = true;
}

bool Settings::contains(const String &key) const
{
    const String k = fullKey(key);
    if (k.isEmpty())
        return false;
    return m_data.contains(k);
}

void Settings::remove(const String &key)
{
    const String k = fullKey(key);
    if (k.isEmpty()) {
        clear();
        return;
    }

    const String prefix = k + ANIME_STR("/");
    const StringList keys = m_data.keys();
    for (const String &existing : keys) {
        if (existing == k || existing.startsWith(prefix)) {
            m_data.remove(existing);
            m_dirty = true;
        }
    }
}

void Settings::clear()
{
    if (!m_data.isEmpty()) {
        m_data = JsonObject();
        m_dirty = true;
    }
}

void Settings::sync()
{
    if (m_dirty) {
        if (saveToDisk())
            m_dirty = false;
    } else {
        loadFromDisk();
    }
}

void Settings::loadFromDisk()
{
    m_data = JsonObject();
    if (m_filePath.isEmpty())
        return;

    File f(m_filePath);
    if (!f.exists() || !f.open(IODevice::ReadOnly))
        return;

    const std::string raw = f.readAll();
    f.close();
    if (raw.empty())
        return;

    JsonParseError err{};
    const JsonDocument doc = JsonDocument::fromJson(raw, &err);
    if (err.error != JsonParseError::NoError || !doc.isObject())
        return;

    m_data = doc.object();
}

bool Settings::saveToDisk()
{
    if (m_filePath.isEmpty())
        return false;

    const String parentDir = FileInfo(m_filePath).absolutePath();
    if (!parentDir.isEmpty() && !Dir().mkpath(parentDir))
        return false;

    SaveFile sf(m_filePath);
    if (!sf.open(IODevice::WriteOnly))
        return false;

    const std::string bytes = JsonDocument(m_data).toJson(JsonDocument::Indented);
    if (sf.write(bytes) != static_cast<std::int64_t>(bytes.size())) {
        sf.cancelWriting();
        return false;
    }
    return sf.commit();
}

} // namespace AnimeCore
