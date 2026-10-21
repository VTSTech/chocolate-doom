# Chocolate Doom VK — libretro slang shaders in DOOM, via Vulkan

This fork of [Chocolate Doom](https://www.chocolate-doom.org) runs every
frame of the game through a **libretro slang shader preset** (`.slangp` /
`.slang` — the shader format used by RetroArch and the
[slang-shaders](https://github.com/libretro/slang-shaders) corpus) using
[VTSTech/libslang-vk](https://github.com/VTSTech/libslang-vk), an embeddable
C++23 library with a pure-C binding. CRT scanlines, LCD grids, phosphor
decay, bezels — the same post-processing ecosystem emulators use, applied
to the original 1993 engine.

```
I_VideoBuffer (320x200, PLAYPAL indices, CPU-rasterized by the engine)
    |  palette LUT expand (+ 6:5 nearest stretch when 4:3-corrected)
    v
RGBA8 320x240 / 320x200  --slangvk_image_update-->  Vulkan input image
    |  slangvk_chain_render_swapchain  (preset pass chain; the library
    |  handles history rings, feedback, per-pass FBOs, letterboxing)
    v
vkQueuePresentKHR  (swapchain; render() is synchronous/fenced, so the
                     present needs no wait semaphore)
```

The integration lives in `src/i_video_vk.c` (~1,000 lines of C99) plus a
set of small, reviewable hooks in `src/i_video.c`, `src/m_config.c`, and
the build files. The classic SDL2 renderer is untouched: with the feature
off (or on any failure), Chocolate Doom behaves exactly like upstream.

## Requirements

- A CMake ≥ 3.7 build (as upstream) and a C++23-capable compiler for the
  embedded library (GCC ≥ 12, Clang ≥ 15, MSVC 2022 17.13+).
- **No Vulkan SDK** anywhere. libslang-vk loads the Vulkan runtime at
  runtime through volk, and the presenter in this fork resolves its own
  direct entry points through `SDL_Vulkan_GetVkGetInstanceProcAddr`. You
  just need a Vulkan-capable driver (any modern GPU, or lavapipe).
- SDL2 ≥ 2.0.14 with the `SDL_WINDOW_VULKAN` path (any 2.24+ is fine;
  2.32's window-aware Vulkan API is used when present).

At build time CMake fetches a pinned copy of libslang-vk (tag `M4.05`)
via FetchContent; that in turn fetches glslang 16.6, Vulkan-Headers and
volk. `ENABLE_VK_SHADERS=OFF` builds the stub path instead — no fetches,
no C++, upstream-identical behavior.

## Building

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build          # chocolate-doom, -heretic, -hexen, -strife
```

All four games share the same video path, so the feature covers all of
them (plus Strife). On Windows, MSYS2/MinGW and MSVC (vcpkg manifest) are
wired up in `.github/workflows/vk-shaders.yml`.

## Running

```sh
# One-off:
build/src/chocolate-doom -iwad DOOM.WAD -shader /path/to/crt-easymode.slangp

# Persisted in the config file (~/.local/share/chocolate-doom/default.cfg):
#   shader = /path/to/crt-easymode.slangp
#   usevulkan = 1
```

Setting `shader` (config or `-shader <path>`) is what activates the
Vulkan path. This fork ships **no presets by design** — clone
[libretro/slang-shaders](https://github.com/libretro/slang-shaders) and
point `shader` at any `.slangp` in it; `-shader` accepts any path.

| Flag | Effect |
|------|--------|
| `-shader <path>` | Run this `.slangp` through the chain (persist in `shader` config key) |
| `-novulkan` | Force the classic SDL2 renderer for this run (`usevulkan = 0` persists) |
| `-noaspect` | Feed raw 320x200 (16:10) instead of the authentic 4:3 stretch |
| **F11** (in-game) | Toggle 4:3 ↔ raw aspect live; feed re-creates, chain re-derives |
| `-vkdebug` | Enable the Vulkan validation layer for this run |
| `-vkcapture <n>` | Dump the n-th presented frame to `vkframe.tga` |

## The frame feed and aspect handling

DOOM's software renderer produces 320×200 indexed 8-bit frames — VGA mode
13h with non-square pixels: 4:3 on the CRT, 16:10 in square-pixel terms.
The presenter LUT-expands those indices to RGBA8 through the current
PLAYPAL (gamma and the 6-bit VGA quantization included — the exact same
values the classic path uses) and, when `aspect_ratio_correct` is on, does
a 6:5 nearest row stretch to 320×240 so CRT geometry and bezel presets see
the authentic shape (this is what libslang-vk's aspect-corrected viewport
derives its letterbox from — SPEC F6). F11 flips the two modes at runtime.

## Debug aids

`-vkcapture <n>` reads back the n-th presented frame straight off the
swapchain image (created with `TRANSFER_SRC` when the surface allows it)
and writes `vkframe.tga` — a deterministic, tooling-free proof of the full
present pipeline. `-vkdebug` enables the Khronos validation layer; a clean
run produces no output.

## Fallback behavior

Every failure path — no Vulkan loader, device creation refused, the preset
file missing or failing to parse — prints one explanatory line and drops
back to the classic SDL2 renderer for that run. A failure *after* the
chain is live (device lost, render error) is fatal with the library's
message, via `I_Error`. The games' own sources stay C99 throughout.

## Validation performed (2026-10-21, this fork @ v0.1.0-vk)

- libslang-vk M4.05 standalone: 18/18 ctest on lavapipe (incl. the M4
  swapchain-presentation test and the C API test).
- Full game smoke on lavapipe (llvmpipe 19.1.7) under Xvfb: freedoom1.wad,
  `demo1` timedemo through the chain — 1-pass preset at ~306 fps, 2-pass
  at ~284 fps, frame 240 captured via `-vkcapture`, differential check
  across two different presets (mean abs diff 22/255, 98% of pixels
  changed — the chain demonstrably processes the frame).
- Zero Vulkan validation messages across a full `-vkdebug` session.
- F11 aspect toggle exercised live mid-demo: feed re-created 320x240 →
  320x200, chain re-derived, and the presentation letterbox switched
  exactly as specified (edge rows content 57.6/74.1 → pure black 0.0/0.0,
  non-black coverage 99.6% → 83.0%).
- Not yet automated: interactive window resize / fullscreen-toggle
  swapchain recreation (exercised by design; mirrors the reference host's
  recreate logic — please report anything odd on real hardware).

## Known limitations

- The Vulkan device is created by the library (create mode); on
  multi-GPU systems it picks the first graphics-capable device and checks
  that it can present to the window's surface. Hosts wanting full control
  can adopt their own device instead (`slangvk_device_adopt` — not wired
  in this fork yet).
- Emscripten is rejected at configure time when `ENABLE_VK_SHADERS=ON`.
- `-timedemo` plus upstream's ENDOOM screen waits for a keypress on exit —
  that is unchanged upstream behavior (it only matters in headless runs).

## License & attribution

Upstream [Chocolate Doom](https://www.chocolate-doom.org): GPL-2.0,
Copyright (C) 1993-1996 Id Software, Inc., 2005-2014 Simon Howard et al.
[libslang-vk](https://github.com/VTSTech/libslang-vk) (MIT, Copyright (C)
VTSTech) is fetched at configure time and statically linked; the combined
work is distributed under the GPL-2.0 (see `COPYING.md`). Freedoom
(BSD-3) is used for automated testing only and never distributed with
this fork. This fork adds no shader presets of its own; anything you
point `-shader` at is governed by its own license (the slang-shaders
corpus is largely MIT — check each preset's header).
