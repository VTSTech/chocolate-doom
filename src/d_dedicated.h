//
// Copyright(C) 2026 VTSTech
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
//     Boot-stage log for the standalone dedicated server.
//

#ifndef D_DEDICATED_H
#define D_DEDICATED_H

// Append one line to chocolate-server.log (next to the executable, or
// in the working directory if that is not writable) and flush it. The
// console is the dedicated server's whole UI, so a silent instant death
// would otherwise leave no trace at all — the boot log is that trace:
// its last line names the last stage that completed. Lazily opens the
// file on first use; a no-op when no writable location exists (the
// console banner still carries the happy path).
void DED_BootLog(const char *fmt, ...);

#endif /* #ifndef D_DEDICATED_H */
