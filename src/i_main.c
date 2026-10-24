//
// Copyright(C) 1993-1996 Id Software, Inc.
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
// DESCRIPTION:
//	Main program, simply calls D_DoomMain high level loop.
//

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "SDL.h"

#include "doomtype.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_misc.h"

#ifdef SDL_MAIN_HANDLED
#include "d_dedicated.h"
#endif


//
// D_DoomMain()
// Not a globally visible function, just included for source reference,
// calls all startup code, parses command line options.
//

void D_DoomMain (void);

int main(int argc, char **argv)
{
    // save arguments

    myargc = argc;
    myargv = malloc(argc * sizeof(char *));
    assert(myargv != NULL);

    for (int i = 0; i < argc; i++)
    {
        myargv[i] = M_StringDuplicate(argv[i]);
    }

    //!
    // Print the program version and exit.
    //
#if defined(_WIN32) && !defined(SDL_MAIN_HANDLED)
    // The four games are GUI-subsystem executables: their stdout belongs
    // to no console, so "chocolate-doom.exe --version" from PowerShell
    // printed absolutely nothing. Attach the launching terminal's console
    // so version/help output lands where the user can read it. A
    // double-clicked launch has no parent console, AttachConsole fails,
    // and the exe stays quiet like a proper GUI app.
    if (AttachConsole(ATTACH_PARENT_PROCESS))
    {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif

    if (M_ParmExists("-version") || M_ParmExists("--version")) {
        puts(PACKAGE_STRING);
        exit(0);
    }

#if defined(_WIN32)
    // compose a proper command line from loose file paths passed as arguments
    // to allow for loading WADs and DEHACKED patches by drag-and-drop
    M_AddLooseFiles();
#endif

    M_FindResponseFile();
    M_SetExeDir();

#ifdef SDL_MAIN_HANDLED
    // Dedicated server: leave a stage trail in chocolate-server.log as
    // each boot step completes. A server that dies instantly and
    // silently is diagnosed by the LAST line this log managed to
    // flush; the games keep a zero-footprint startup.
    DED_BootLog("boot: main entered (%s)", PACKAGE_STRING);
#endif

    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

#ifdef SDL_MAIN_HANDLED
    // Targets compiled with SDL_MAIN_HANDLED (the dedicated server: a
    // console app whose "main" stays a real "main" instead of being
    // renamed to SDL_main) own their entry point — tell SDL the main
    // machinery was handled manually. A no-op for the GUI game binaries,
    // which enter through SDL2main's WinMain instead.
    SDL_SetMainReady();
#endif

#ifdef SDL_MAIN_HANDLED
    DED_BootLog("boot: entering D_DoomMain");
#endif

    // start doom

    D_DoomMain ();

    return 0;
}

