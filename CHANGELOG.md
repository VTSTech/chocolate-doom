# Changelog

Notable changes to **Chocolate Doom VK** — the Vulkan slang-shader path
this fork adds on top of upstream Chocolate Doom. Upstream changes keep
living in upstream's own history and are not tracked here.

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions: this repository's `v<N>` tags.

## [Unreleased]

### Added
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
- This `CHANGELOG.md`.

### Changed
- `libslang-vk` bumped to **M4.06** (release commit `09068b25`): the
  M4.05-era library had bugs that made the Vulkan path fail to start
  on real drivers — the game silently fell back to the classic SDL2
  renderer while the overlay still showed the configured shader.
  M4.06 fixes them; the Vulkan path now starts as configured.
- The dependency pin is now an **immutable commit hash, not a mutable
  release tag** (`GIT_TAG 09068b25… # M4.06 Release` in
  `CMakeLists.txt`). A tag can be re-pointed upstream; a commit hash
  cannot. Bumping is always a deliberate hash swap.

### Fixed
- A silent-fallback trap: if an earlier `-novulkan` run persisted
  `usevulkan = 0`, later runs with `-shader <path>` stayed on the
  classic renderer with no indication why. Passing `-shader` on the
  command line is explicit intent and now re-enables the Vulkan path
  for that run (and persists `usevulkan = 1`); `-novulkan` still wins
  when both are given.
- Vulkan fallbacks are no longer invisible: every failure exit of
  `I_VK_AttemptGraphics` records a human-readable reason, shown in
  the stats overlay's shader row (`shader.slangp (unused: preset
  failed to load)`) and echoed to stdout (`Vulkan path inactive
  (…) — classic renderer`). stderr is invisible in Windows GUI
  builds — exactly where fallbacks bite hardest.
- MSVC/vcpkg builds linked an SDL2 **without** Vulkan support (the
  `vulkan` feature of vcpkg's `sdl2` port is not in its defaults):
  `vcpkg.json` now requests `sdl2[vulkan]`, so the CI artifact is
  actually capable of the Vulkan path.
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

[Unreleased]: https://github.com/VTSTech/chocolate-doom/compare/v2...HEAD
[v2]: https://github.com/VTSTech/chocolate-doom/compare/v1...v2
[v1]: https://github.com/VTSTech/chocolate-doom/releases/tag/v1
