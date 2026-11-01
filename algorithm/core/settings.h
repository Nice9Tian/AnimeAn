#pragma once

#include "algorithm/core/json.h"
#include "algorithm/core/string.h"

#include <vector>

namespace AnimeCore {

class Settings
{
public:
    Settings();
    ~Settings();

    Settings(const Settings &) = delete;
    Settings &operator=(const Settings &) = delete;

    void beginGroup(const String &prefix);
    void endGroup();
    String group() const;

    JsonValue value(const String &key, const JsonValue &defaultValue = JsonValue(JsonValue::Undefined)) const;
    JsonValue value(const String &key, const String &defaultValue) const
    {
        return value(key, JsonValue(defaultValue));
    }
    JsonValue value(const String &key, Latin1String defaultValue) const
    {
        return value(key, JsonValue(String(defaultValue)));
    }
    JsonValue value(const String &key, const char *defaultValue) const
    {
        return value(key, JsonValue(String::fromUtf8(defaultValue ? defaultValue : "")));
    }
    JsonValue value(const String &key, bool defaultValue) const
    {
        return value(key, JsonValue(defaultValue));
    }
    JsonValue value(const String &key, int defaultValue) const
    {
        return value(key, JsonValue(defaultValue));
    }
    JsonValue value(const String &key, std::int64_t defaultValue) const
    {
        return value(key, JsonValue(defaultValue));
    }
    JsonValue value(const String &key, double defaultValue) const
    {
        return value(key, JsonValue(defaultValue));
    }

    void setValue(const String &key, const JsonValue &val);
    void setValue(const String &key, const String &val)
    {
        setValue(key, JsonValue(val));
    }
    void setValue(const String &key, Latin1String val)
    {
        setValue(key, JsonValue(String(val)));
    }
    void setValue(const String &key, const char *val)
    {
        setValue(key, JsonValue(String::fromUtf8(val ? val : "")));
    }
    void setValue(const String &key, bool val)
    {
        setValue(key, JsonValue(val));
    }
    void setValue(const String &key, int val)
    {
        setValue(key, JsonValue(val));
    }
    void setValue(const String &key, std::int64_t val)
    {
        setValue(key, JsonValue(val));
    }
    void setValue(const String &key, double val)
    {
        setValue(key, JsonValue(val));
    }

    bool contains(const String &key) const;
    void remove(const String &key);
    void clear();

    void sync();

    String fileName() const { return m_filePath; }

private:
    String fullKey(const String &key) const;
    void loadFromDisk();
    bool saveToDisk();

    String m_filePath;
    JsonObject m_data;
    std::vector<String> m_groupStack;
    bool m_dirty = false;
};

} // namespace AnimeCore
