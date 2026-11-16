#pragma once

// The process environment, the same call on every system. Names and values are
// full Unicode (wide on Windows, UTF-8 elsewhere). Implemented beside the rest
// of the system layer in native_win32.cpp / native_posix.cpp.

#include "algorithm/core/string.h"

namespace AnimeCore {

// Empty when the variable is not set.
String environmentVariable(const String &name);
bool setEnvironmentVariable(const String &name, const String &value);

// What separates the entries of a list such as PATH or PYTHONPATH: ';' on Windows, ':' elsewhere.
Char pathListSeparator() noexcept;

} // namespace AnimeCore
