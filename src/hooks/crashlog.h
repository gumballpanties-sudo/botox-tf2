#pragma once

// ==== crash log -- C:otox-tf2\crash.log =================================================
// Independent of the debug log ON PURPOSE: its own file, its own lock, no g_debugLogEnable
// gate, installed from DllMain before a single hook exists. debug.log is off by default and
// is recreated empty every launch, so the one thing nobody ever has after a crash is the
// record of the crash. This file appends, survives the session, and is written from the
// faulting thread itself.

void CrashLog_Install(void* hSelfModule);

// Breadcrumb. `tag` MUST be a string literal -- only the pointer is stored, nothing is copied.
// The last 16 are printed with every record, so a fault that lands inside client.dll still
// names what dih was doing when it got there. Cost: two stores and one atomic increment.
void CrashLog_Mark(const char* tag);

// One call per main-thread frame (CVGui::RunFrame). A frame gap over the threshold writes a
// HITCH line carrying the same breadcrumbs -- the stutter half of "random crashes/stutters",
// in the same file, with no debug log needed.
void CrashLog_Frame();

// One timestamped line, no debug-log toggle needed. For rare diagnostics a feature wants to
// see from a user's session (a draw path silently falling back) -- callers rate-limit it.
void CrashLog_Note(const char* fmt, ...);
