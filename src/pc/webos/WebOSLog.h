#pragma once
// Tiny logging helper for the webOS port.
//
// Messages go to stderr and to a log file that can be read on the TV:
//
//   cat /tmp/mcbetacpp-webos.log
//
// The file name can be changed with the MCBETA_WEBOS_LOG environment variable.
#include <cstdarg>

namespace webos
{
void log(const char *format, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

// Records what the main thread is doing. The watchdog thread and the crash
// handlers print this, so a hang or a crash can be located from the log.
void stage(const char *name);

// Call once per rendered frame (lets the watchdog tell "hung" from "running").
void frame();

// Installs signal handlers (SEGV/ABRT/BUS/FPE/ILL), a std::terminate logger
// and the watchdog thread. Call as early as possible in main().
void installDiagnostics();
}
