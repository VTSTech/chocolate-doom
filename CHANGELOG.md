# Changelog

Notable changes to **Chocolate Doom VK** — the Vulkan slang-shader path
this fork adds on top of upstream Chocolate Doom. Upstream changes keep
living in upstream's own history and are not tracked here.

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions: this repository's `v<N>` tags.

## [Unreleased]

## [v3] - 2026-10-21

The GPU-scaler maturity release: true 16:9, warm-start shader caching,
survivable GPU driver resets, a session log you can actually read on
Windows, and `libslang-vk` as a shared DLL.

### Added
- **16:9 aspect** — a third stop on the F11 cycle
  (4:3 → 16:10 → 16:9 → 4:3; `aspect_ratio_correct` 0/1/2). The feed is
  column-stretched to 356x200 *before* the shader chain, so presets see
  a genuine 16:9 source and CRT masks stay square on a 16:9 display.
  The classic renderer treats any non-zero value as 4:3. (Stretched,
  not true widescreen FOV — that needs engine-side column rendering;
  see README-VK.md, "Known limitations".)
- **SPIR-V disk cache** — compiled passes persist in `shader-cache/`
  beside the executable and reload in milliseconds on warm starts
  instead of full slang recompiles. Every entry is validated on read
  (magic, size caps, exact file length) and cache keys are whitelisted
  to `[0-9a-f]{1,64}`, so a corrupt or hostile file is a silent cache
  miss, never a failed startup. Writes are atomic (temp file + rename).
  Built on libslang-vk M4.07's new `slangvk_spirv_cache` API.
- **Device-lost recovery** — `VK_ERROR_DEVICE_LOST` (driver crash /
  Windows TDR) no longer kills the game with `I_Error`. All three loss
  surfaces (render, acquire, frame upload) re-acquire host objects,
  drop the dead swapchain and rebuild it lazily on the next frame,
  then confirm recovery with an overlay flash and a log line. Only a
  failed *recovery* is fatal.
- **`gpu-scaler.log`** — a timestamped session log written beside the
  executable (previous run rotated to `.old`). Windows GUI builds have
  no console, so the path's ~36 printf diagnostics were invisible
  exactly where they matter most; startup, preset state, SPIR-V cache
  stats, hotkey actions, fallback reasons and even the final `I_Error`
  message now land in the log, with a per-line flush so a crash never
  truncates the tail. Best-effort by design: an unwritable location
  degrades to console-only and never blocks the game.
- **PrintScreen** hotkey — screenshot at any moment: reads back the next
  presented frame to the first free `vkshotNNN.tga` (names probed with
  `fopen`, so nothing is ever overwritten). Same channel-correct TGA
  writer as `-vkcapture`.
- **F12** hotkey — cycle the shader base resolution 1x → 2x → 3x → 4x →
  1x, riding the same re-create-and-re-derive path as the F11 aspect
  toggle; the overlay mode row follows immediately and the choice
  persists in `shader_base_resolution` on exit.
- Transient amber feedback row (2 s) in the stats overlay for either
  hotkey (`shot vkshot000.tga` / `base 640x480 (2x)`).
- `README-VK.md` grows with the code: 16:9 aspect table, Shader cache
  and Session log sections, shared-library build instructions, and a
  Validation section recording the per-batch test harness runs.
- This `CHANGELOG.md`.

### Changed
- `libslang-vk` pinned to **M4.07** (commit `d176d1b…`, immutable full
  hash; v2 used the mutable `M4.05` tag). The M4.05-era library had
  bugs that made the Vulkan path fail to start on real drivers — the
  game silently fell back to the classic renderer — so v3 rides the
  M4.06 fixes and settles on M4.07 for its additions: the SPIR-V cache
  API the disk cache above is built on, C-API null-handle validation
  (SEC-07) and a zero-allocation render path (PERF-09). Bumping the pin
  is always a deliberate hash swap in `CMakeLists.txt`.
- **`libslang-vk` now builds as a shared library** — `libslang-vk.dll`
  (identically named on MSVC and MinGW — an empty target prefix keeps
  MinGW from doubling it to `liblibslang-vk.dll`) is deployed beside
  the game executables at build time by a single `slangvk-runtime`
  copy step (`libslang-vk.so` on Linux), exporting its 27 C-API
  symbols via `cmake/libslang-vk-c-api.def` (MSVC and MinGW need the
  explicit `.def`; ELF exports the symbols by default anyway).
  Relinking a game no longer relinks the shader compiler. Prefer static
  again? One CMake toggle — see README-VK.md, "Building".
- F11 is now a three-way aspect cycle (was 4:3 ↔ 16:10).
- Stats overlay: the shader row shows the preset's basename
  (`crt-easymode | 3 passes`) instead of the full path, and the old
  "1:1" aspect label is now correctly labeled "16:10".

### Removed
- `cpp-linter` GitHub Actions workflow.

### Fixed
- A silent-fallback trap: if an earlier `-novulkan` run persisted
  `usevulkan = 0`, later runs with `-shader <path>` stayed on the
  classic renderer with no indication why. Passing `-shader` on the
  command line is explicit intent and now re-enables the Vulkan path
  for that run (and persists `usevulkan = 1`); `-novulkan` still wins
  when both are given.
- Vulkan fallbacks are no longer invisible: every failure exit of
  `I_VK_AttemptGraphics` records a human-readable reason, shown in the
  stats overlay's shader row (`shader.slangp (unused: preset failed to
  load)`) and written to stdout *and* `gpu-scaler.log` — stderr is
  invisible in Windows GUI builds, exactly where fallbacks bite
  hardest.
- MSVC/vcpkg builds linked an SDL2 **without** Vulkan support (the
  `vulkan` feature of vcpkg's `sdl2` port is not in its defaults), and
  the manifest's CLI-style `"sdl2[vulkan]"` string is invalid in
  manifest mode and broke `vcpkg install` outright. `vcpkg.json` now
  uses the proper object form —
  `{"name": "sdl2", "features": ["vulkan"]}` — and additionally
  requests the `vulkan` port (headers + ICD loader) for the renderer's
  own `find_package(Vulkan)`.
- The Windows CI artifacts ship `libslang-vk.dll` — and the jobs fail
  loudly when it is absent. Two holes closed: the MSYS2 zip's ldd-based
  runtime walk only collected `/ucrt64` DLLs and silently omitted it
  (the zip launched straight into a "not found" dialog), and the MSVC
  package step never looked at the build tree at all. Deployment itself
  no longer races either: the initial per-game POST_BUILD copies could
  collide on the same destination under parallel links (a Windows
  sharing violation — intermittent `permission denied` on the MSVC
  runner); one copy step that every game depends on replaces them.
- `-vkcapture` screenshots came out blue-shifted. The readback
  (`slangvk_device_read_pixels`) is a raw `vkCmdCopyImageToBuffer`, so
  bytes arrive in the surface format's native packing: B8G8R8A8 memory
  is B,G,R,A — already exactly TGA's component order — but the writer
  swapped channels 0 and 2 unconditionally, red/blue-flipping every
  capture on every B8G8R8A8 swapchain (virtually every real platform).
  The writer now swaps only for R8G8B8A8* surfaces, per the actually
  chosen `VkFormat`.

## [v2] - 2026-10-21

### Added
- On-screen stats overlay, drawn into the software frame before
  presentation on **both** backends: FPS, presenter (`Vulkan |
  libslang-vk` / `SDL2 | classic`), active shader preset and pass
  count, adapter / renderer string. **Scroll Lock** toggles it live;
  `-novkstats` starts without it (`vkstats` config key persists).
- Shader base resolution, mirroring DevilutionX's libslang-vk knobs:
  `-vkbase <n>` / `shader_base_resolution` accepts a 2/3/4 multiplier
  (or a pixel width, e.g. `640`) and rebuilds the input image at that
  size before the chain, so shaders designed for higher-resolution
  sources light up correctly.
- **F11** hotkey — live 4:3 ↔ raw 320x200 (16:10) aspect toggle; the
  feed re-creates and the chain re-derives on the next frame.
- `./patches/` — the per-feature patch archive this fork keeps in-tree
  (0001–0006 at this point).

## [v1] - 2026-10-21

### Added
- Vulkan presentation path via
  [VTSTech/libslang-vk](https://github.com/VTSTech/libslang-vk) (M4.05,
  FetchContent): the 8-bit software frame is expanded through the
  palette LUT into an input image and run through any libretro slang
  preset chain (`.slangp`), presented via a Vulkan swapchain sharing
  the SDL window. The games' own sources stay C99; the classic SDL2
  renderer is untouched and takes over on any failure or `-novulkan`.
- CLI: `-shader <path>` (activates the path, persists in `shader`),
  `-novulkan`, `-vkdebug` (validation layer), `-noaspect` (raw 16:10
  feed), `-vkcapture <n>` (dump the n-th presented frame to
  `vkframe.tga`).
- Build: `ENABLE_VK_SHADERS` option in both CMake and autotools; no
  Vulkan SDK needed anywhere (volk at runtime, entry points via
  `SDL_Vulkan_GetVkInstanceProcAddr`).
- CI: `.github/workflows/vk-shaders.yml` — Linux lavapipe smoke test
  plus MSYS2 UCRT64 and MSVC (vcpkg) Windows builds.
- `README-VK.md` — full feature documentation.

### Fixed
- Windows CI: MSYS2 packaging zipped nothing (bare `.` with no `-r`;
  now globs the files and walks `ldd` over every game binary for the
  runtime DLLs). MSVC job: `lukka/run-vcpkg` refused to run because
  upstream's `vcpkg.json` ships no builtin-baseline — pinned one
  (vcpkg release `2026.07.29`, commit
  `9e593bb18ea69cc5095e012465dcd675a822ed0d`) so the manifest resolves
  deterministically.

[Unreleased]: https://github.com/VTSTech/chocolate-doom-vk/compare/v3...HEAD
[v3]: https://github.com/VTSTech/chocolate-doom-vk/compare/v2...v3
[v2]: https://github.com/VTSTech/chocolate-doom-vk/compare/v1...v2
[v1]: https://github.com/VTSTech/chocolate-doom-vk/releases/tag/v1
