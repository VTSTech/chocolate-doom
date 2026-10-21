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

# Persisted in the config file
# (~/.local/share/chocolate-doom/chocolate-doom.cfg — note the
# "name  value" format, no '=' sign, like the rest of Chocolate Doom):
#   shader                     "/path/to/crt-easymode.slangp"
#   usevulkan                  1
#   shader_base_resolution     640        # 2x base canvas (see below)
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
| `-vkbase <n>` | Shader base resolution: 2/3/4 = doubled/tripled/quadrupled chain input (or a pixel width, e.g. `640`); persists in `shader_base_resolution` |
| **F11** (in-game) | Toggle 4:3 ↔ raw aspect live; feed re-creates, chain re-derives |
| **Scroll Lock** (in-game) | Toggle the stats overlay (below) |
| **PrintScreen** (in-game) | Screenshot: read back the next presented frame to the first free `vkshotNNN.tga` (same channel-correct TGA writer as `-vkcapture`) |
| **F12** (in-game) | Cycle the shader base resolution 1x → 2x → 3x → 4x → 1x (persists in `shader_base_resolution`) |
| `-novkstats` | Start with the stats overlay off (`vkstats = 0` persists) |
| `-vkdebug` | Enable the Vulkan validation layer for this run |
| `-vkcapture <n>` | Dump the n-th presented frame to `vkframe.tga` |

## On-screen stats overlay

A compact overlay is drawn into the software frame just before it is
presented — on **both** backends — so it answers "what am I actually
looking at" at a glance:

```
Vulkan | libslang-vk
llvmpipe (LLVM 19.1.7, 256 bits)
crt/crt-easymode.slangp | 3 passes
314.2 fps | 3.18 ms | 320x240 4:3
```

- Row 1 is the presenter: `Vulkan | libslang-vk` or `SDL2 | classic`.
- Row 2 is the adapter (Vulkan device name) or, on the classic path,
  the SDL renderer, video driver and window size
  (e.g. `opengl | x11 | 640x480`).
- Row 3 is the active preset with its pass count; a configured preset
  that failed to start is shown as `(unused)`, `no shader` when none is
  set.
- Row 4 is the smoothed frame rate and frame time, plus the feed mode
  (`320x240 4:3` or `320x200 1:1`, the same state F11 flips; with a base
  multiplier active it shows the enlarged canvas, e.g. `640x480 4:3 (2x)`).
  Under normal play the engine caps rendering at its 35 Hz tic rate — that
  cap is what the counter honestly reports; `-timedemo` shows real
  throughput (several hundred fps on lavapipe, for example).

The text is an embedded public-domain 8×8 bitmap font (font8x8), drawn
in palette-index space with `I_GetPaletteIndex()` colors, so it follows
gamma and pain tints like the rest of the frame — and on the Vulkan
path it goes through the shader chain itself (`-vkcapture` frames show
it). **Scroll Lock** toggles it at runtime; `vkstats` in the config
file and `-novkstats` control the startup state (default: on). Note
that Scroll Lock is intercepted by the overlay toggle and therefore
not available for game bindings while this fork runs.

The hotkeys report back through the overlay itself: **PrintScreen** and
**F12** each add a short-lived amber feedback row to the box for two
seconds — `shot vkshot000.tga` / `base 640x480 (2x)` — so the action is
confirmed without leaving the game. While the Vulkan path is live,
F11 (vanilla: gamma) is the aspect toggle and F12 (vanilla: demo/net
spy camera) is the base-resolution cycle; PrintScreen is unused by the
vanilla game. All three only intercept while the Vulkan path is active
— with `-novulkan` the keys fall through to the game unchanged.

## The frame feed and aspect handling

DOOM's software renderer produces 320×200 indexed 8-bit frames — VGA mode
13h with non-square pixels: 4:3 on the CRT, 16:10 in square-pixel terms.
The presenter LUT-expands those indices to RGBA8 through the current
PLAYPAL (gamma and the 6-bit VGA quantization included — the exact same
values the classic path uses) and, when `aspect_ratio_correct` is on, does
a 6:5 nearest row stretch to 320×240 so CRT geometry and bezel presets see
the authentic shape (this is what libslang-vk's aspect-corrected viewport
derives its letterbox from — SPEC F6). F11 flips the two modes at runtime.

## Shader base resolution (double / triple / quadruple)

DevilutionX's libslang-vk integration exposes two independent knobs: the
**Game Resolution** (the window/swapchain the chain presents to) and the
**Shader Base Resolution** (the canvas the chain consumes). This fork has
the same split — the window side is Chocolate Doom's regular `-2`/`-3`/
`-width`/`-height`/fullscreen handling, and the base side is new:

```sh
# 2x  base canvas (feed 320x240 -> chain input 640x480):
build/src/chocolate-doom -iwad DOOM.WAD -shader crt/crtglow_lanczos.slangp -vkbase 2

# 3x / 4x (960x720 / 1280x960 chain input), or a pixel width — same thing:
... -vkbase 3
... -vkbase 1280

# Config-file form (persisted like every other VK option):
#   shader_base_resolution     960
```

**F12 cycles the multiplier live** while the game runs — 1x → 2x → 3x
→ 4x → back to 1x — riding the same re-create-and-re-derive path as the
F11 aspect toggle: the input image is rebuilt at the new size, the chain
re-derives every pass FBO from the per-frame input, the overlay's mode
row updates immediately, and the choice persists through
`shader_base_resolution` (multiplier form) on exit.

What it does: after the palette LUT expansion (and the 4:3 stretch when
enabled), the frame is duplicated k-fold — each game pixel becomes an exact
k×k block, pure nearest-neighbor, no new detail — and *that* image is what
the preset chain sees. The chain re-derives every pass FBO from the input
size, so a `2xScaleHQ` fed a 640×480 base really does its 2x work at 2x
game scale, and the final pass maps the result onto the window as before.

Why you'd want it:

- **Upscaler headroom on small windows.** Presets designed around
  input < output (ScaleFX, HQx, xBR variants) have more room to work
  with a bigger base — the same reason DevilutionX ships a base-size
  setting. On a huge window it matters less (the ratio is already large).
- **Anti-aliasing / sharpening presets** (FXAA, advanced AA) behave
  better with more pixels to smooth: at native 320×240 they can blur the
  whole frame, at 2–3x they work at a finer granularity.
- **CRT masks / scanline presets** that compute in input space get a
  finer pixel grid; output-space presets are unaffected.

Trade-offs: it is a real upscale — heavier input-space presets cost more
GPU time per frame (input-space work scales with k²; on the lavapipe
software rasterizer a 1-pass preset went ~294 → ~211 fps at 2x, ~157 at
3x — real GPUs have far more headroom), and the CPU staging copy grows
(k×k duplication, one `memcpy` per row block — measured in microseconds).
The multiplier is capped at 4 (1280×960) to bound staging memory and
shader cost. `0` (the default) and `320` mean native 1x — the exact
pre-v0.2 look. Vulkan path only; `-novulkan` runs ignore it. The startup
log prints the effective choice:

```
I_VK: shader base resolution 2x — feed 320x240 -> base 640x480 (DevilutionX-style pre-scale before the preset chain)
I_VK: feed 320x240 (4:3 corrected), base 640x480, swapchain 1920x1080, present FIFO
```

The F11 aspect toggle composes with it (base follows the feed: 640×480 ↔
640×400), and the overlay's mode row always shows the live canvas.

## Debug aids

`-vkcapture <n>` reads back the n-th presented frame straight off the
swapchain image (created with `TRANSFER_SRC` when the surface allows it)
and writes `vkframe.tga` — a deterministic, tooling-free proof of the full
present pipeline. **PrintScreen** does the same readback on demand at
any moment, writing the first free `vkshot000.tga`, `vkshot001.tga`, …
(probed with `fopen`, so repeated presses never overwrite).

The readback is a raw `vkCmdCopyImageToBuffer` — the bytes arrive in the
surface format's native packing, and the writer maps that to TGA's
B,G,R,A order per the actual `VkFormat` of the swapchain (`B8G8R8A8*`
is already TGA order and goes out untouched; `R8G8B8A8*` gets R and B
swapped on the way out). Versions before v0.2.1 swapped unconditionally,
which red/blue-flipped every capture made on a B8G8R8A8 surface — i.e.
virtually every real platform — producing the "blue shifted"
screenshots. `-vkdebug` enables the Khronos validation layer; a clean
run produces no output.

## Fallback behavior

Every failure path — no Vulkan loader, device creation refused, the preset
file missing or failing to parse — prints one explanatory line and drops
back to the classic SDL2 renderer for that run. A failure *after* the
chain is live (device lost, render error) is fatal with the library's
message, via `I_Error`. The games' own sources stay C99 throughout.

## Validation performed (2026-10-21; v0.1.1-vk, base-resolution at v0.2.0, hotkeys + capture fix at v0.2.1)

- libslang-vk M4.05 standalone: 18/18 ctest on lavapipe (incl. the M4
  swapchain-presentation test and the C API test).
- Full game smoke on lavapipe (llvmpipe 19.1.7) under Xvfb: freedoom1.wad,
  `demo1` timedemo through the chain — 1-pass preset at ~306 fps, 2-pass
  at ~284 fps, frame 240 captured via `-vkcapture`, differential check
  across two different presets (mean abs diff 22/255, 98% of pixels
  changed — the chain demonstrably processes the frame).
- Shader base resolution (v0.2.0): six-run matrix on lavapipe — 1x
  baseline, `-vkbase 2` / `-vkbase 640` (multiplier and width forms,
  byte-identical output: mean diff 0.28/255), `-window -3 -vkbase 2`
  (960x720 window, real upscaler headroom), `-vkbase 4 -vkdebug` (zero
  validation messages), and a config-seeded `shader_base_resolution 960`
  (3x). Overlay decoded glyph-by-glyph from every capture: the mode row
  reads `320x240 4:3`, `640x480 4:3 (2x)`, `960x720 4:3 (3x)`,
  `1280x960 4:3 (4x)` exactly as configured. Content equivalence at 2x
  and 4x: mean abs diff vs the 1x capture 9.9 / 9.2 out of 255 (final-pass
  sampling phase only); at 3x (a 3:2 output ratio, the worst case for the
  test preset's synthetic scanlines) the scan-factor-corrected per-channel
  ratio median is exactly 1.000 — identical scene structure. Timedemo
  throughput on lavapipe: ~294 / ~211 / ~157 / ~117 fps at 1x / 2x / 3x /
  4x (input-space cost scales with k², as expected).
- Zero Vulkan validation messages across a full `-vkdebug` session.
- Hotkeys + capture channel fix (v0.2.1): 25/25 automated checks on
  lavapipe — `-vkcapture` frame 240 and PrintScreen shots at 1x/2x/3x all
  verified channel-correct by pixel analysis (status-bar digits pure red,
  e.g. (196,0,0); thousands of warm overlay glyphs; zero blue-swapped
  pixels) — the pre-v0.2.1 writer unconditionally swapped R/B and produced
  blue-shifted TGAs on every B8G8R8A8 surface. F12 cycled 1x→2x→3x live in
  a real game session (log lines, overlay mode row change measured across
  50% of the box, flash row visible in the shots), and a graceful quit
  wrote `shader_base_resolution 3` back to the config file — the choice
  persists. Hotkeys also exercised under `-vkdebug` with zero validation
  messages.
- F11 aspect toggle exercised live mid-demo: feed re-created 320x240 →
  320x200, chain re-derived, and the presentation letterbox switched
  exactly as specified (edge rows content 57.6/74.1 → pure black 0.0/0.0,
  non-black coverage 99.6% → 83.0%).
- Stats overlay (v0.1.1): 24/24 automated checks on lavapipe — default-on
  box present, `-novkstats` and Scroll Lock each remove it on both
  backends, and the overlay text was decoded glyph-by-glyph out of the
  captured frames: `Vulkan | libslang-vk` / `llvmpipe (LLVM 19.1.7, 256
  bits)` / `demo-bright.slangp | 1 pass` / `275.9 fps | 3.62 ms |
  320x240 4:3` on the Vulkan path, and `SDL2 | classic` / `opengl | x11
  | 640x480` / `no shader (classic renderer)` / `35.1 fps | 28.52 ms |
  320x240 4:3` on the classic path (the engine's 35 Hz tic cap, as
  expected). The overlay's fps reading agrees with `-timedemo`'s own
  summary (275.9 vs 304 average).
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
corpus is largely MIT — check each preset's header). The stats overlay
embeds [font8x8](https://github.com/dhepper/font8x8) (public domain,
Daniel Hepper / Marcel Sondaar / IBM).
