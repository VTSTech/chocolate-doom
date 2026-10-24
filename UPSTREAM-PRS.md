# Upstream PR plan

Working notes for patches worth carrying back to
chocolate-doom/chocolate-doom, distilled from the 3.0.1-3.0.3 work on
this fork. Every entry below was found the hard way here first: each
one says what upstream's bug is, the change we would propose, and
where it is already built, CI-proven and in daily use in this fork.

Ground rules:

- **Port, don't paste.** This fork has diverged (Vulkan renderer,
  `d_dedicated.c` rewrite of upstream's `d_server.c`, the shader
  browser), so each PR is re-derived against upstream master with the
  smallest possible diff — and re-checked against current master
  first; upstream may have moved since this fork's base.
- **One concern per PR.** The five below are independently reviewable
  and independently revertable. No bundle PRs.
- **Windows changes stay gated** so Linux/macOS builds are untouched,
  and must be proven on both toolchains this fork's CI covers — MSVC
  and MSYS2/ucrt64. The two disagree about SDL2main link shape, and
  that disagreement is itself one of the findings (PR 5).
- **Issue first** for anything behavioral (PR 5). Small mechanical
  fixes (PR 1, 2, 4) can go straight to PR with the diagnosis in the
  description.

## PR 1 — net_sdl.c: check SDLNet_Init() and the receive-packet allocation

- **Problem.** `NET_SDL_InitClient()` and `NET_SDL_InitServer()` ignore
  `SDLNet_Init()`'s return value, and the packet returned by
  `SDLNet_AllocPacket()` is used without a NULL check. On Windows, a
  refused WSAStartup (security policy, a broken `SDL2_net.dll`) or a
  failed allocation leaves the net layer dead while boot continues;
  the failure surfaces later and far from the cause, with nothing
  pointing at init.
- **Change.** Check both calls in both functions — about ten lines:
  `I_Error("NET_SDL_InitServer: SDLNet_Init: %s", SDLNet_GetError())`,
  the same fatal-init style upstream uses elsewhere. The message names
  the stage.
- **Proven here.** 3.0.3. The happy path is exercised by every
  server/client run since; the checks themselves only add a failure
  path that did not exist before.
- **Reception.** Pure bugfix, failure path only. Near-zero risk of
  objection.

## PR 2 — doomtype.h + textscreen/txt_main.h: gnu_printf under MinGW

- **Problem.** GCC's MinGW targets use the `ms_printf` format
  archetype, whose conversion set predates C99. Every `%zu` in an
  `I_Error`/`M_snprintf` message produces `warning: unknown conversion
  type character 'z'` on MSYS2 builds even though the UCRT runtime
  formats it correctly — noise that buries real warnings, and fatal
  under `-Werror`.
- **Change.** Under `__MINGW32__`/`__MINGW64__`, define `PRINTF_ATTR`
  as `format(gnu_printf, ...)`. Two lines in two files.
- **Open review point** (state it in the PR, don't hide it): on old
  msvcrt-based MinGW runtimes without `__USE_MINGW_ANSI_STDIO`, `%zu`
  genuinely does not format at runtime — gnu_printf would silence a
  real limitation there. Either gate on `_UCRT ||
  __USE_MINGW_ANSI_STDIO`, or accept it since upstream's supported
  MinGW path (MSYS2 ucrt64) is UCRT. Ask; we run ucrt64, where it is
  purely correct.
- **Proven here.** 3.0.3; MSYS2 CI builds warning-free.
- **Reception.** CI-noise fix, trivial to review.

## PR 3 — Windows: make --version/--help output visible from a terminal

- **Problem.** Upstream's Windows executables are GUI-subsystem by
  design (no console flash on double-click) — so their stdout belongs
  to no console, and `chocolate-doom.exe --version` from PowerShell
  prints absolutely nothing. The version check itself sits right there
  in `main()` (`i_main.c`, "Print the program version and exit") — it
  just writes into the void.
- **Change.** About twelve lines at the top of `main()` under
  `#if defined(_WIN32)`: `AttachConsole(ATTACH_PARENT_PROCESS)`, then
  `freopen("CONOUT$", "w", stdout/stderr)`, placed before the version
  check. Launched from a terminal, output lands in that terminal;
  double-clicked, AttachConsole fails and the exe stays quiet like a
  proper GUI app. No change on other platforms, none interactively.
- **Proven here.** 3.0.3; `--version` prints in PowerShell on the
  user's machine, double-click stays silent.
- **Reception.** Papercut fix, self-gating, low controversy.

## PR 4 — CMake: build the four per-game chocolate-*-setup binaries

- **Problem.** Autotools builds `chocolate-{doom,heretic,hexen,strife}-setup`
  (`SETUP_BINARIES` in `src/Makefile.am`); the setup tool dispatches on
  its executable basename, so a CMake-built `chocolate-setup` cannot
  open Strife-specific settings at all. CMake is upstream's documented
  Windows build path — Windows users get no `chocolate-strife-setup`.
- **Change.** A `POST_BUILD` copy loop on the setup target, about six
  lines (this fork: `add_custom_command(TARGET ...setup POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE> .../chocolate-<game>-setup)`),
  mirroring what autotools already does. No extra compile cost.
- **Proven here.** Since 3.0.2, all CI, both toolchains.
- **Reception.** Parity with autotools; hard to argue against. Check
  upstream's install rules want the copies too before submitting.

## PR 5 — Windows: the dedicated server must be a console application

The highest-value one, and the one worth an issue before the PR.

- **Problem.** Upstream's "apply WIN32 to executables" change made
  every Windows binary GUI-subsystem — including `chocolate-server`. A
  dedicated server is a console program: upstream's
  `chocolate-server.exe` on Windows has no console, prints its banner
  into the void, and a double-clicked server runs — or dies —
  invisibly. Every diagnostic it owns (SDL_net errors, `-netlog`
  messages, `I_Error` text) is lost. Repro is thirty seconds: build
  upstream, run the server, watch nothing appear.
- **Change** (exactly what this fork ships):
  1. `add_executable(...server ...)` without `WIN32`.
  2. Server target compiles with `SDL_MAIN_HANDLED`; `i_main.c` calls
     `SDL_SetMainReady()` for it — the console binary enters through
     its own real `main`. Games keep SDL2main/WinMain untouched.
  3. CMake: the server links `SDL2::SDL2` only, not `SDL2::SDL2main`,
     on Windows. With `CMAKE_FIND_PACKAGE_PREFER_CONFIG` (default On),
     SDL2's own package config carries `-Wl,--undefined=WinMain` on
     MinGW, which force-loads SDL2main's `WinMain` object → calls
     `SDL_main` → undefined reference once `main` is no longer
     renamed. That exact failure is what breaks MSYS2 server links;
     MSVC console exes never reference `WinMain`, so only MinGW shows
     it. Both toolchains proven in this fork's CI.
  4. Unbuffered stdout (`setvbuf(stdout, NULL, _IONBF, 0)`) so banner
     and diagnostics survive redirection and kills; banner names the
     UDP port. (A caution learned the hard way while debugging this
     fork: `setvbuf` with a buffered mode and size 0 — which glibc
     accepts — is documented-invalid on the UCRT and release builds
     fastfail on it. Use `_IONBF` or a real size.)
- **Open questions for upstream** (go in the issue): the original
  intent of the blanket WIN32 commit (games' console-flash avoidance —
  the server needs carving out, not reverting); autotools parity on
  Windows (whether the autotools server links SDL2main and needs the
  same treatment); and whether maintainers prefer `SDL_MAIN_HANDLED`
  or a WinMain shim in `d_server.c`.
- **Proven here.** 3.0.3 on MSVC (local builds) and MSYS2/ucrt64 (CI);
  server boots, prints, serves.
- **Reception.** Most discussion of the batch, best payoff. The PR
  lands on CMake + `i_main.c` + `d_server.c` and should keep each
  file's diff minimal.

## Considered, not proposed

- **FOV slider, shader browser, the Vulkan path** — outside upstream's
  vanilla-accuracy design goals; not worth their review time or ours.
- **Crash trap + boot log for the server** (3.0.3's diagnostics
  infrastructure) — genuinely useful on Windows, but it is
  error-reporting architecture upstream did not ask for. Hold until
  PR 5 lands, then offer a minimal "print the exception code to
  stderr" version if there is appetite.
- **Strife-only gamepad inventory keys** (3.0.2) — plausible upstream
  material (it completes `joyb_*` coverage), but it is a feature with
  defaults to debate; separate discussion if wanted.

## Sequencing

1. Rebase-check all five against current upstream master.
2. Search upstream issues and PRs for prior art — the invisible server
   in particular may already have an issue to attach to.
3. Submit 1 and 2 together (trivial, build trust), then 3, then 4,
   then the issue for 5 followed by its PR.
4. Keep fork provenance out of the PR descriptions — the repros stand
   on their own against upstream.

All five are already written and CI-proven in this fork's tree; porting
each is at most a half-day.
