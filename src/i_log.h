//
// i_log.h — gpu-scaler.log session log for the Vulkan shader path.
//
// Copyright (C) 2025 VTSTech (Nigel Todman) — veritas@vts-tech.org
// License: GNU General Public License, version 2 (see COPYING.md).
//
// Windows GUI-subsystem builds have no console: printf and stderr are
// invisible exactly where the Vulkan path most often needs to explain
// itself (fallback reasons, device-lost recovery, shader compile
// failures). This module gives those messages a durable home — one
// plain-text file, gpu-scaler.log, written next to the game's other
// per-run data (configdir: the executable's directory on Windows,
// SDL_GetPrefPath elsewhere — the same place as default.cfg and the
// SPIR-V shader cache).
//
// One file per session: I_LogInit rotates the previous run to
// gpu-scaler.log.old (the run before that is gone), so the last two
// sessions — including a crashed one — are always on disk.
//
// Everything is best-effort: a log that cannot be opened disables
// itself, and every entry point stays safe to call (I_LogPrintf /
// I_LogError still print to the console). Logging must never fail the
// game. The module compiles into every binary (i_system.c reports
// fatal errors through it) but only the Vulkan path opens the file,
// so builds with ENABLE_VK_SHADERS off never create one.
//

#ifndef __I_LOG__
#define __I_LOG__

// Open <configdir>/gpu-scaler.log and write the session header. Call
// once from I_VK_AttemptGraphics — before anything can fail, so
// fallback reasons are recorded too. Idempotent: later calls (a video
// re-init after a mode change) are no-ops and the session keeps
// running in the same file.
void I_LogInit(void);

// Flush and close (idempotent). Registered with atexit(), so a normal
// exit and a fatal I_Error both end with the file on disk; each write
// is flushed as it happens regardless.
void I_LogClose(void);

// 1 when the log file is open, 0 otherwise.
int I_LogEnabled(void);

// The full path of the open log, or "" when logging is disabled.
const char *I_LogPath(void);

// Write one entry to the log only (timestamped prefix, newline
// guaranteed). No console output — for callers that already print, or
// that must stay silent.
void I_LogMessage(const char *fmt, ...);

// printf + log: console stdout behaviour is unchanged, and the same
// line lands in gpu-scaler.log.
void I_LogPrintf(const char *fmt, ...);

// fprintf(stderr, ...) + log: console stderr behaviour is unchanged,
// and the same line lands in gpu-scaler.log.
void I_LogError(const char *fmt, ...);

#endif
