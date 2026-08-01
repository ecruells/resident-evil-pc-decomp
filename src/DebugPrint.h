// DebugPrint.h - diagnostic output that is actually visible.
//
// The project links with /SUBSYSTEM:WINDOWS, so there is no stdout: every printf
// in the port has been silently discarded. Diagnostics have to go through
// OutputDebugStringA, which shows up in the Visual Studio Output window.
//
// Note this only works under a debugger. Writing debug output from a task running
// on a switched stack fail-fasts when no debugger is attached, so a build with
// these calls on the per-frame paths must be run from Visual Studio, not
// standalone.
#pragma once
#include <windows.h>
#include <cstdarg>
#include <cstdio>

static inline void dbg_printf(const char* fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
}
