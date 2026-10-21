//
// i_video_vk.h — Vulkan post-FX presenter interface (Chocolate Doom VK).
//
// Copyright (C) 2025 VTSTech (Nigel Todman).
// License: GNU General Public License, version 2 (see COPYING.md).
//
// Integrates VTSTech/libslang-vk: libretro slang shader presets (.slangp)
// executed over the 320x200 software frame through a Vulkan swapchain.
//
// Call sites live in i_video.c; every entry point is a no-op (and
// I_VK_AttemptGraphics() returns false) when ENABLE_VK_SHADERS is not
// defined, so the classic SDL2 renderer path is completely unaffected.
//

#ifndef __I_VIDEO_VK__
#define __I_VIDEO_VK__

#include "SDL.h"

#include "doomtype.h"
#include "i_video.h"

// --- config-exposed options (bound in I_BindVideoVariables, i_video.c) ---

// Master switch (config + -novulkan). Only honored when a preset is set.
extern int usevulkan;

// Path to a .slangp preset (config "shader" + -shader <path>). Setting it
// is what activates the Vulkan path at startup; init falls back to the
// classic renderer on any failure.
extern char *shader_preset;

// -vkdebug: enable the Vulkan validation layer for this run.
extern int vk_debug;

// -vkcapture <frame>: dump that rendered frame to vkframe.tga (debug aid;
// validates the full present pipeline without screen-grab tooling).
extern int vk_capture_frame;

// --- shared state owned by i_video.c ---
// The VK presenter creates the window straight into `screen` (so the
// classic event pump / title / shutdown paths keep working unmodified)
// and debounces swapchain recreation off the same resize flags that
// HandleWindowEvent maintains for the classic path.
extern SDL_Window *screen;
extern boolean need_resize;
extern unsigned int last_resize_time;

// --- runtime state (i_video.c consults these) ---

// True once the Vulkan path owns the window and presents every frame.
boolean I_VK_Active(void);

// Adapter description (deviceName from the library device info)
// and preset pass count, used by the stats overlay (i_overlay.c).
// Return ""/0 when the Vulkan path is not running (or compiled
// out).
const char *I_VK_DeviceName(void);
int I_VK_PassCount(void);

// --- hooks called from i_video.c ---

// From I_InitGraphics: try to take over (window + device + swapchain +
// preset + chain + I_VideoBuffer). On failure cleans up after itself and
// returns false; the caller falls back to the classic SetVideoMode().
boolean I_VK_AttemptGraphics(void);

// From I_SetPalette: copies the 256-entry palette into the expansion LUT.
void I_VK_SetPalette(const SDL_Color *palette);

// From HandleWindowEvent(SDL_WINDOWEVENT_RESIZED).
void I_VK_FlagResize(void);

// Runtime aspect toggle (F11): flips the feed between 320x240 (4:3, the
// authentic VGA mode 13h stretch) and raw 320x200 square pixels.
void I_VK_ToggleAspect(void);

// From I_FinishUpdate: feed + render + present one frame.
void I_VK_FinishUpdate(void);

// From I_ShutdownGraphics.
void I_VK_ShutdownGraphics(void);

#endif
