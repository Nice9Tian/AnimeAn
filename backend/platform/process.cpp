#include "process.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace Process {

// Measured on an i7-14700KF: a bare Python loop 46 ms before, 88 ms after a few
// seconds of work, 45 ms with this opt-out. Looked up by name because the call
// only exists from Windows 8 on.
void optOutOfPowerThrottling()
{
    struct ThrottlingState {
        ULONG version;
        ULONG controlMask;
        ULONG stateMask;
    };
    constexpr int kProcessPowerThrottling = 4;
    constexpr ULONG kExecutionSpeed = 1;
    using SetInformation = BOOL(WINAPI *)(HANDLE, int, LPVOID, DWORD);
    const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    const auto set = kernel ? reinterpret_cast<SetInformation>(reinterpret_cast<void *>(
                                  GetProcAddress(kernel, "SetProcessInformation")))
                            : nullptr;
    if (set) {
        ThrottlingState state{1, kExecutionSpeed, 0};
        set(GetCurrentProcess(), kProcessPowerThrottling, &state, sizeof(state));
    }
}

} // namespace Process

#else

namespace Process {

void optOutOfPowerThrottling() {}

} // namespace Process

#endif
