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
//      System specific interface stuff.
//


#ifndef __I_VIDEO__
#define __I_VIDEO__

#include "doomtype.h"

// Screen width and height.

#define SCREENWIDTH  320
#define SCREENHEIGHT 200

// Screen height used when aspect_ratio_correct=true.

#define SCREENHEIGHT_4_3 240

// Feed width used when aspect_ratio_correct=2 (VK presenter only): the
// 320x200 frame nearest-stretched to a 16:9 canvas (356/200 = 1.78).

#define SCREENWIDTH_16_9 356

typedef boolean (*grabmouse_callback_t)(void);

// Called by D_DoomMain,
// determines the hardware configuration
// and sets up the video mode
void I_InitGraphics (void);

void I_GraphicsCheckCommandLine(void);

void I_ShutdownGraphics(void);

// Takes full 8 bit values.
void I_SetPalette (byte* palette);
int I_GetPaletteIndex(int r, int g, int b);

void I_UpdateNoBlit (void);
void I_FinishUpdate (void);

void I_ReadScreen (pixel_t* scr);

void I_BeginRead (void);

void I_SetWindowTitle(const char *title);

void I_CheckIsScreensaver(void);
void I_SetGrabMouseCallback(grabmouse_callback_t func);

void I_DisplayFPSDots(boolean dots_on);
void I_BindVideoVariables(void);

void I_InitWindowTitle(void);
void I_RegisterWindowIcon(const unsigned int *icon, int width, int height);
void I_InitWindowIcon(void);

// Called before processing any tics in a frame (just after displaying a frame).
// Time consuming syncronous operations are performed here (joystick reading).

void I_StartFrame (void);

// Called before processing each tic in a frame.
// Quick syncronous operations are performed here.

void I_StartTic (void);

// Enable the loading disk image displayed when reading from disk.

void I_EnableLoadingDisk(int xoffs, int yoffs);

extern char *video_driver;
extern boolean screenvisible;

extern int vanilla_keyboard_mapping;
extern boolean screensaver_mode;
extern int usegamma;
extern pixel_t *I_VideoBuffer;

extern int screen_width;
extern int screen_height;
extern int fullscreen;
extern int aspect_ratio_correct;
extern int integer_scaling;
extern int smooth_pixel_scaling;
extern int vga_porch_flash;
extern int force_software_renderer;

// Window sizing, owned by i_video.c (config-bound there); the Vulkan
// presenter (i_video_vk.c) mirrors these for its swapchain bookkeeping.
extern int window_width;
extern int window_height;
extern int fullscreen_width;
extern int fullscreen_height;

extern int png_screenshots;

extern char *window_position;
void I_GetWindowPosition(int *x, int *y, int w, int h);

// [CDVK] SHADERS browser support (menus in all four games):
//
// The browser walks one folder at a time. I_ShaderBrowse returns a
// shared array of row entries for the current folder with *count set
// to its length (0 when the folder could not be read or is empty) and
// is re-scanned on every call, so presets dropped into the folder
// mid-session appear on the next browser open. Row layout: a literal
// ".." first whenever the folder has a parent to climb to, then the
// folder's own subdirectories (sorted, stored with a trailing
// DIR_SEPARATOR so I_ShaderEntryIsDir can recognize them), then the
// folder's *.slangp presets (sorted, full paths).
//
// I_ShaderBrowseReset clears the current folder so the next
// I_ShaderBrowse re-selects the default: the active preset's own
// folder (its siblings are the most likely next pick), else
// <exedir>/shaders, else <configdir>/shaders, else <exedir>, else
// <configdir>. The browser open path calls this; navigation does not.
//
// I_ShaderBrowseGoto moves the browser: entry is either the literal
// ".." row (climb to the parent) or a folder row from a previous
// I_ShaderBrowse (descend into it). Returns false for a ".." with no
// parent or a non-folder entry; the menus follow a true return with
// I_ShaderBrowse to re-populate the list.
//
// I_ShaderEntryIsDir recognizes folder rows (trailing separator).
//
// I_ShaderRowName renders a row's display label into buf: ".." as-is,
// folders as NAME/ (trailing slash marks them), presets as NAME with
// the extension stripped. The menus apply their own width trimming
// and " *" active marker afterwards.
//
// I_CurrentShaderPreset returns the active preset path ("" when none)
// so the browser can mark the current row.
//
// I_SelectShaderPreset applies a pick: a live hot-swap (and config
// persist) when the Vulkan path is up, a persist-only "next run"
// selection otherwise. Returns true only when the swap went live.
//
// I_VulkanActive mirrors I_VK_Active() (i_video_vk.h) for menu code:
// hexen's h2def.h declares `byte *screen`, so the game menus must not
// include i_video_vk.h (which declares the SDL_Window * form).
void I_ShaderBrowseReset(void);
const char **I_ShaderBrowse(int *count);
boolean I_ShaderBrowseGoto(const char *entry);
boolean I_ShaderEntryIsDir(const char *entry);
void I_ShaderRowName(const char *entry, char *buf, size_t buf_size);
const char *I_CurrentShaderPreset(void);
boolean I_SelectShaderPreset(const char *path);
boolean I_VulkanActive(void);

// Joystic/gamepad hysteresis
extern unsigned int joywait;

extern int usemouse;


#endif