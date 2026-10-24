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
#include "net_defs.h"

#include "net_dedicated.h"
#include "net_server.h"
#include "z_zone.h"

// The default listen port lives in net_sdl.c (NET_SDL_InitServer parses
// "-port" the same way); duplicated here so the startup banner can show
// what will actually be opened. Keep the two in sync.
#define DED_DEFAULT_PORT 2342

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

    // The console is the dedicated server's whole UI, so make sure it
    // actually says something. Line-buffered stdout keeps the banner
    // (and anything after it) visible when the output is redirected to
    // a file or a pipe — without this, a killed or crashed server can
    // take its whole buffered log with it (the per-line-flush rule the
    // gpu-scaler.log batch introduced for the same reason).
    setvbuf(stdout, NULL, _IOLBF, 0);

    printf(PACKAGE_NAME " standalone dedicated server\n");

    // Mirror NET_SDL_InitServer's -port parsing so the banner names
    // the port that is about to be bound (or was refused).
    p = M_CheckParmWithArgs("-port", 1);
    if (p > 0)
    {
        port = atoi(myargv[p + 1]);
    }
    printf("Listening on UDP port %i (change with -port;"
           " -privateserver skips the master server)\n", port);

    Z_Init();

    NET_DedicatedServer();
}
