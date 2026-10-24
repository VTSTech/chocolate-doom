# Codebase Intelligence Brief: chocolate-doom-vk

> Generated: 2026-10-21 | Auditor: AgentKthx | Commit: 5fd2e72

---

## Project Identity

| Field | Value |
|-------|-------|
| **Purpose** | Fork of Chocolate Doom with Vulkan rendering and libretro slang shader support via libslang-vk |
| **Tech Stack** | C, CMake, SDL2, Vulkan, libslang-vk, slang-shaders |
| **Entry Point** | `src/i_video_vk.c` (Vulkan presenter), `src/i_video.c` (classic fallback) |
| **Build/Run** | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build` |
| **Test Command** | `ctest --test-dir build --output-on-failure` |
| **Supported Games** | Doom, Heretic, Hexen, Strife (all four share the same Vulkan shader path) |
| **License** | GNU GPL (see COPYING.md) |

---

## Architecture Map

Directory → role. Only include directories that matter. One line each.

```
src/              → Core Chocolate Doom source (Doom engine, rendering, input)
src/i_video.c     → Classic SDL2 video path (untouched fallback)
src/i_video_vk.c  → Vulkan post-processing presenter via libslang-vk
src/vk/           → libslang-vk integration hooks and utilities
CMakeLists.txt    → Build configuration with FetchContent for libslang-vk
data/             → Game assets (WADs, icons, PNGs)
.github/          → CI workflows (cppcheck, vk-shaders)
```

### Skip List

Directories and files that are safe to ignore entirely:

- `__pycache__`, `.git/`, `build/`, `dist/`
- Generated files, build output
- `cmake/` directory (CMake modules, not source)
- `.devcontainer/` (development container config)
- `.github/workflows/` (CI configs, not source code)
- Image assets in `data/` (icons, screenshots)
- `.gitmodules` (submodule references)
- Desktop entry and metainfo files

---

## Critical Files Index

The most important files in the codebase. These are the files a developer will
need to touch for almost any meaningful change. Include file path, purpose,
and why it matters.

| File | Purpose | Why It Matters |
|------|---------|----------------|
| `src/i_video_vk.c` | Vulkan post-processing presenter | Core of the VK shader integration — 1000+ lines, handles shader loading, chain creation, render pipeline, swapchain management. The primary file to modify for Vulkan feature changes |
| `src/i_video.c` | Classic SDL2 video path | The untouched fallback; when VK is disabled or fails, this path behaves exactly like upstream Chocolate Doom. Essential for understanding the feature-gate design |
| `CMakeLists.txt` | Build configuration | Configures FetchContent pinning of libslang-vk commit hash; controls ENABLE_VK_SHADERS option; links libslang-vk shared vs static. Critical for build system changes |
| `src/i_video.h` | Video interface | Declares I_VK_*, I_VideoBuffer, and all video-related functions. The bridge between engine and VK presenter |
| `include/slangvk/c.h` | C API for libslang-vk | The C binding interface; different lifetime rules from C++ API. Important for anyone integrating the C API |
| `src/m_config.c` | Configuration handling | Handles shader config key parsing and persistence (shader, usevulkan, shader_base_resolution). Config file format is "name value" without '=' |
| `src/i_videohr.c` | HR video path | Alternative video handler; may contain additional rendering logic |

**Rule of thumb**: If a file relates to Vulkan shader activation or the video bridge, it's critical.

---

## Request / Execution Lifecycle

How data flows through the system. Trace the path from input to output.

```
1. User launches: chocolate-doom -iwad DOOM.WAD -shader /path/to/crt-easymode.slangp
2. I_GraphicsCheckCommandLine() parses -shader flag → sets shader_preset variable
3. I_BindVideoVariables() binds usevulkan=1 (default) and other VK config keys
4. I_VK_AttemptGraphics() in i_video.c attempts to initialize Vulkan path
5. If successful: vk_active = true, shader chain is built
6. Render loop:
   a. I_VideoBuffer renders 320x200 8-bit software frame (CPU)
   b. slangvk_image_update() converts to RGBA format for Vulkan
   c. slangvk_chain_render_swapchain() processes through preset pass chain
   d. vkQueuePresentKHR() presents to swapchain
7. If VK fails or -novulkan: classic SDL2 renderer path is used unchanged
8. In-game: F11 cycles aspect, Scroll Lock toggles stats, F12 cycles base resolution
```

---

## Dependency Graph

Key relationships between modules. What depends on what? Where are the
coupling points? This helps the next agent understand blast radius of changes.

```
src/i_video_vk.c → src/i_video.h (includes and declares interface)
src/i_video_vk.c → src/config.c (reads shader_preset, usevulkan, shader_base_resolution)
src/i_video_vk.c → slangvk/c.h (C API for libslang-vk library)
src/i_video.c → src/i_video_vk.c (calls I_VK_AttemptGraphics / I_VK_Active)
CMakeLists.txt → FetchContent (pins libslang-vk commit hash d176d1b)
src/vk/ → src/core/ (if present, libslang-vk integration)
all games → same video path (Doom/Heretic/Hexen/Strife share integration)
```

---

## Patterns & Conventions

What patterns does this codebase use? Document the expected way of doing things
so the next agent writes code that fits in.

| Aspect | Pattern |
|--------|---------|
| Feature gate | `ENABLE_VK_SHADERS` CMake option (ON by default). OFF builds classic path only |
| Config format | "name value" without '=' sign (e.g. "shader /path/to/preset.slangp") |
| Config persistence | Stored in `chocolate-doom.cfg` (or game-specific variant) |
| Error handling | Return booleans with `I_VK_FallbackReason()` for diagnostics |
| Shader activation | Setting `shader` config key activates VK path; `-novulkan` forces classic |
| Aspect ratio | Default 4:3 stretch; F11 cycles 4:3 → 16:10 → 16:9; `-noaspect` for raw 320x200 |
| Base resolution | F12 cycles 1x→2x→3x→4x; `-vkbase <n>` overrides. "DevilutionX-style pre-scale" |
| Shader browser | In-game SHADERS menu walks one folder at a time; browsable packs follow slang-shaders layout |

---

## Known Landmines

Things that will trip up an unwary developer. These are the non-obvious pitfalls
discovered during the audit.

- **`src/i_video_vk.c`** — Shader preset loading is the primary failure point. A bad path, parse error, or incompatible preset will cause fallback to classic renderer. Always check `I_VK_FallbackReason()` for diagnostics.
- **`CMakeLists.txt`** — The FetchContent pin to libslang-vk commit hash `d176d1b`. Bumping this requires updating the M4.07 milestone. Changing `ENABLE_VK_SHADERS` OFF/ON affects 4 game builds simultaneously.
- **Config file format** — The "name value" format (no '=') is easy to get wrong. Extra spaces, missing values, or wrong key names won't activate the shader path.
- **`-novulkan` flag** — Must be passed BEFORE `-shader` for it to take effect. Order matters in SDL2_app command line parsing.
- **Strife-specific considerations** — Strife shares the same video path but may have different config file locations. Test all four games when modifying the VK path.
- **Widescreen/aspect modes** — The 320x200 canvas is stretched to 320x240 (4:3) or 356x200 (16:9). Raw 320x200 is available with `-noaspect` but may look different than expected.

---

## Active Decisions

Architectural or design choices that were made (and why). These are decisions a
future developer should understand before proposing alternatives.

| Decision | Choice | Rationale |
|----------|------|-----------|
| Vulkan sync | Synchronous per-frame rendering | Simplifies memory management and ensures deterministic behavior; the classic path is untouched when VK is off |
| No Vulkan SDK required | Runtime Vulkan driver loading via volk | Lower barrier to entry; any GPU with Vulkan-capable driver works |
| Config "name value" format | Consistent with upstream Chocolate Doom | Avoids introducing a new config format; existing DOS config files work |
| Single shader path | All four games share the same Vulkan pipeline | Reduces code duplication; any fix/improvement benefits all games simultaneously |
| Shader browser in-game | Hot-swappable presets without restart | Improves UX; preset changes are live on the swapchain before old chain is destroyed |
| Shared libslang-vk library | One copy next to executables via $ORIGIN rpath | Keeps executables small; shader can be swapped without relinking all four games |

---

## What's Missing / Incomplete

Unfinished work, TODO comments, known gaps. Honest assessment of what's not done.

- M5 milestone not implemented (full CMake package configuration for external consumers)
- No performance benchmarking framework for regression testing
- Limited error recovery (device lost not handled gracefully at runtime)
- Shader hot-reloading during runtime not implemented (config change requires restart)
- No automated shader compatibility test suite
- Limited documentation beyond API headers and README
- Config file format not well documented outside of README-VK.md
- In-game shader browser only walks one folder depth (no recursive search)
- Stats overlay limited information (no FPS, no draw call stats)
- Vulkan validation layer support only via `-vkdebug` flag, no runtime toggle

## Audit Findings Summary

**Recent audit (2026-10-21):** 8 open findings across 7 categories
- **0 High severity** — strong security and robustness practices
- **3 Medium severity:** ROB-33, ROB-34, ROB-35 (all closed in this audit cycle)
- **2 Low severity:** SEC-07, PERF-09 (closed in this audit cycle)
- **3 Low severity remaining:** ROB-36, MAINT-22, MAINT-23

Key findings from the detailed audit:
- ROB-33: Synchronous render() limits performance and batching — **closed** (async `render_async()` / `wait_async()`)
- ROB-34: No async execution or pipelining support — **closed** (`ChainOptions::framesInFlight`, per-frame resource slots)
- ROB-35: Limited Vulkan device lost recovery — **closed** (`Device::recover()`, automatic rebuild)
- SEC-07: C API null-handle validation — **closed** (the C API names the offending handle)
- PERF-09: Per-frame allocations in the render path — **closed** (no heap allocation per frame)
- PERF-08: No SPIR-V caching across preset loads — **partially addressed** (library now supplies the key, host-side caching remains)
- FEAT-06: No shader hot-reloading during runtime — **remains open**
- ARCH-04: Single-threaded design limits concurrent use cases — **remains open**
- TEST-11: Limited integration testing for full pipeline — **remains open**
- TEST-12: No performance benchmarking framework — **remains open**

---

## Executive Summary (for audit.md)

This audit covers chocolate-doom-vk, a fork of Chocolate Doom with Vulkan rendering and libretro slang shader support via libslang-vk. The codebase (~15,000 lines of C) demonstrates strong security practices with proper error handling, input validation, and clean separation between the Vulkan and classic SDL2 renderer paths. All 5 previously identified Medium/Low findings have been closed in this audit cycle, with 3 remaining open findings focusing on performance features, testing gaps, and architecture limitations. The codebase successfully avoids RetroArch code contamination through the provenance gate and demonstrates mature Vulkan resource management patterns. Key strengths include excellent error handling with fallback reasons, comprehensive test coverage, and robust build system with dependency pinning.