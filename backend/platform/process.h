#ifndef ANIMEAN_BACKEND_PLATFORM_PROCESS_H
#define ANIMEAN_BACKEND_PLATFORM_PROCESS_H

// Settings of the running process that a system needs changed for the backend
// to work at full speed. A system with nothing to change does nothing.

namespace Process {

// A windowless process under sustained load counts as background work on
// Windows: it is moved to the efficiency cores, where everything takes twice as
// long, and it stays there. This asks to be left at full speed.
void optOutOfPowerThrottling();

} // namespace Process

#endif // ANIMEAN_BACKEND_PLATFORM_PROCESS_H
