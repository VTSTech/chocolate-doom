//
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// Code specific to the standalone dedicated server.
//

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "config.h"

#include "m_argv.h"
#include "m_misc.h"
#include "net_defs.h"

#include "d_dedicated.h"
#include "net_dedicated.h"
#include "net_server.h"
#include "z_zone.h"

#if defined(_WIN32)
// A Windows-native crash (access violation, heap corruption abort, ...) kills
// the process with no output whatsoever — for a double-clicked console app
// that is a window flash, indistinguishable from any other instant death.
// This trap turns that flash into a report: the exception code, what it
// means, and the faulting address (which tells a debugger-less user whether
// the fault is in chocolate-server.exe itself or in a DLL like SDL2_net).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <signal.h>

// Translate the handful of exception codes that actually show up in the
// field into plain English. Everything else prints as a raw hex code.
static const char *DED_ExceptionText(DWORD code)
{
    switch (code)
    {
        case EXCEPTION_ACCESS_VIOLATION:      return "access violation";
        case EXCEPTION_ILLEGAL_INSTRUCTION:   return "illegal instruction";
        case EXCEPTION_STACK_OVERFLOW:        return "stack overflow";
        case 0xC0000374:                      return "heap corruption";
        case 0xC0000409:                      return "stack buffer overrun / fastfail";
        case 0xC0000135:                      return "DLL not found (loader)";
        default:                              return "unknown exception";
    }
}

static LONG WINAPI DED_CrashFilter(EXCEPTION_POINTERS *info)
{
    char msgbuf[512];
    DWORD code = info->ExceptionRecord->ExceptionCode;
    void *addr = info->ExceptionRecord->ExceptionAddress;

    fprintf(stderr, "chocolate-server: fatal exception 0x%08lX (%s) at %p\n",
                    (unsigned long) code, DED_ExceptionText(code), addr);
    fflush(stderr);

    // The boot log is the copy that survives even if the console output
    // was lost — write it before anything else can go wrong.
    DED_BootLog("CRASH: fatal exception 0x%08lX (%s) at %p",
                (unsigned long) code, DED_ExceptionText(code), addr);

    M_snprintf(msgbuf, sizeof(msgbuf),
               "Fatal exception 0x%08lX (%s)\n"
               "at address %p\n\n"
               "If the address above is inside a DLL (e.g. SDL2_net.dll) "
               "rather than chocolate-server.exe, that module is the "
               "culprit — check that the DLLs sitting next to "
               "chocolate-server.exe match the ones the game uses.",
               (unsigned long) code, DED_ExceptionText(code), addr);

    // The console window is about to vanish (or already flashed shut),
    // so also put the report in a message box that stays put until
    // acknowledged.
    MessageBoxA(NULL, msgbuf, "chocolate-server crashed",
                MB_OK | MB_ICONERROR);

    return EXCEPTION_EXECUTE_HANDLER;
}

static void DED_AbortHandler(int sig)
{
    fprintf(stderr, "chocolate-server: abort() (signal %d) — usually heap "
                    "corruption or a failed CRT check\n", sig);
    fflush(stderr);
    DED_BootLog("CRASH: abort() (signal %d) — usually heap corruption "
                "or a failed CRT check", sig);
    MessageBoxA(NULL, "abort() called — usually heap corruption or a "
                      "failed CRT check. See console output.",
                      "chocolate-server crashed", MB_OK | MB_ICONERROR);
    _exit(3);
}
#endif

// The default listen port lives in net_sdl.c (NET_SDL_InitServer parses
// "-port" the same way); duplicated here so the startup banner can show
// what will actually be opened. Keep the two in sync.
#define DED_DEFAULT_PORT 2342

// The boot log file, opened lazily by DED_BootLog on first use.
static FILE *bootlog;

void DED_BootLog(const char *fmt, ...)
{
    va_list argptr;

    if (bootlog == NULL)
    {
        // exedir is set by M_SetExeDir() in main() before D_DoomMain()
        // runs, so the log lands next to the exe the user actually
        // launched; fall back to the working directory when that is
        // not writable. "w" truncates per run — this is a boot report,
        // not an ongoing server log (that is what -netlog is for).
        if (exedir != NULL)
        {
            char *path = M_StringJoin(exedir, "chocolate-server.log", NULL);
            bootlog = M_fopen(path, "w");
            free(path);
        }
        if (bootlog == NULL)
        {
            bootlog = M_fopen("chocolate-server.log", "w");
        }
    }

    if (bootlog == NULL)
    {
        // Nowhere writable to put it; the console still carries the
        // banner, so the happy path does not depend on this file.
        return;
    }

    va_start(argptr, fmt);
    vfprintf(bootlog, fmt, argptr);
    va_end(argptr);
    fputc('\n', bootlog);

    // Every line must survive whatever kills us next.
    fflush(bootlog);
}

static void DED_PrintUsage(void)
{
    // Upstream's d_server.c printed a usage block for --help; this
    // fork's stripped-down entry point lost it somewhere along the
    // way, leaving --help to fall through into normal startup.
    printf("Usage: %s [options]\n\n"
           "Chocolate Doom VK standalone dedicated server. Game options\n"
           "(IWAD, mods, skill, ...) belong to the first player who joins\n"
           "the server, not to the server itself.\n\n"
           "Options:\n"
           "  -port <n>        UDP port to listen on (default %i)\n"
           "  -privateserver   don't register with the global master server\n"
           "  -netlog <file>   write a packet-level debug log to <file>\n"
           "  -nogui           suppress GUI popups for fatal errors\n"
           "  --version        print version and exit\n"
           "  --help           print this message and exit\n",
           M_GetExecutableName(), DED_DEFAULT_PORT);
}

void NET_CL_Run(void)
{
    // No client present :-)
    //
    // This is here because the server code sometimes runs this 
    // to let the client do some processing if it needs to.
    // In a standalone dedicated server, we don't have a client.
}

void D_DoomMain(void)
{
    int p, port = DED_DEFAULT_PORT;

#if defined(_WIN32)
    // Install the crash trap before anything that could trip over it.
    // SIGABRT covers the CRT's own abort() path (heap corruption checks);
    // the SEH filter covers native faults. Note MSVC's release-mode
    // abort() uses __fastfail (0xC0000409), which is deliberately
    // uncatchable — that one stays a plain instant death by design.
    SetUnhandledExceptionFilter(DED_CrashFilter);
    signal(SIGABRT, DED_AbortHandler);
    DED_BootLog("boot: crash trap installed");
#endif

    // --help needs handling BEFORE anything else: with no handler it
    // used to fall through into normal startup, so asking for help
    // looked exactly like the mystery instant crash.
    if (M_ParmExists("--help") || M_ParmExists("-help"))
    {
        DED_PrintUsage();
        exit(0);
    }

    // The console is the dedicated server's whole UI, so make sure it
    // actually says something. Line-buffered stdout keeps the banner
    // (and anything after it) visible when the output is redirected to
    // a file or a pipe — without this, a killed or crashed server can
    // take its whole buffered log with it (the per-line-flush rule the
    // gpu-scaler.log batch introduced for the same reason).
    setvbuf(stdout, NULL, _IOLBF, 0);
    DED_BootLog("boot: console ready");

    // PACKAGE_STRING ("Chocolate Doom VK 3.0.3"), not the bare package
    // name: the banner is the fastest possible answer to "which build
    // is this exe actually from" — the question the 3.0.1/3.0.2
    // version-string confusion made painfully relevant.
    printf(PACKAGE_STRING " standalone dedicated server\n");

    // Mirror NET_SDL_InitServer's -port parsing so the banner names
    // the port that is about to be bound (or was refused).
    p = M_CheckParmWithArgs("-port", 1);
    if (p > 0)
    {
        port = atoi(myargv[p + 1]);
    }
    printf("Listening on UDP port %i (change with -port;"
           " -privateserver skips the master server)\n", port);
    DED_BootLog("boot: banner printed (UDP port %i)", port);

    Z_Init();
    DED_BootLog("boot: zone allocator initialized");

    NET_DedicatedServer();
}
