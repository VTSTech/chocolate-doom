//
// i_overlay.c — on-screen stats overlay (Chocolate Doom VK).
//
// Copyright (C) 2025 VTSTech (Nigel Todman).
// License: GNU General Public License, version 2 (see COPYING.md).
//
// Draws a small text box into the 320x200 paletted frame buffer right
// before presentation (see I_FinishUpdate in i_video.c), reporting:
//
//   row 1: graphics backend   — "Vulkan | libslang-vk" or "SDL2 | ..."
//   row 2: adapter / renderer — Vulkan device name, or the SDL2
//                               renderer + video driver + window size
//   row 3: active shader      — preset path + pass count, or why not
//   row 4: rate               — smoothed FPS + frame time + feed mode
//                              (native, or the enlarged shader base canvas)
//
// The text is rendered with an embedded 8x8 bitmap font (font8x8 by
// Daniel Hepper / Marcel Sondaar / IBM, public domain) directly in
// palette-index space, using I_GetPaletteIndex() to pick readable
// colors from whatever palette (including gamma/pain tints) is active.
// Because it draws into the software frame, the overlay itself goes
// through the shader chain on the Vulkan path — a capture made with
// -vkcapture shows it.
//
// Scroll Lock toggles it at runtime; "vkstats" in the config file and
// -novkstats on the command line control the startup state.
//

#include <stdio.h>
#include <string.h>

#include "SDL.h"

#include "doomtype.h"
#include "i_video.h"
#include "i_video_vk.h"
#include "i_overlay.h"

// ------------------------------------------------------------------
// Config-exposed option
// ------------------------------------------------------------------

int vkstats = 1;

// ------------------------------------------------------------------
// font8x8 — public domain 8x8 bitmap font (ASCII 32..127).
// Row bytes, MSB = leftmost pixel. Retrieved from
// https://github.com/dhepper/font8x8 (see file header comment below).
// ------------------------------------------------------------------

//
// font8x8: 8x8 monochrome bitmap fonts for rendering
// Author: Daniel Hepper <daniel@hepper.net>
// License: Public Domain
// Based on: font8x8.h by Marcel Sondaar and the public domain IBM VGA
// 8x8 font collection.
//

static const unsigned char overlay_font[96][8] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // U+0020 ( )
    { 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00 },   // U+0021 (!)
    { 0x6C, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // U+0022 (")
    { 0x6C, 0x6C, 0xFE, 0x6C, 0xFE, 0x6C, 0x6C, 0x00 },   // U+0023 (#)
    { 0x30, 0x7C, 0xC0, 0x78, 0x0C, 0xF8, 0x30, 0x00 },   // U+0024 ($)
    { 0x00, 0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0x00 },   // U+0025 (%)
    { 0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00 },   // U+0026 (&)
    { 0x60, 0x60, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00 },   // U+0027 (')
    { 0x18, 0x30, 0x60, 0x60, 0x60, 0x30, 0x18, 0x00 },   // U+0028 (()
    { 0x60, 0x30, 0x18, 0x18, 0x18, 0x30, 0x60, 0x00 },   // U+0029 ())
    { 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00 },   // U+002A (*)
    { 0x00, 0x30, 0x30, 0xFC, 0x30, 0x30, 0x00, 0x00 },   // U+002B (+)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x60 },   // U+002C (,)
    { 0x00, 0x00, 0x00, 0xFC, 0x00, 0x00, 0x00, 0x00 },   // U+002D (-)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x00 },   // U+002E (.)
    { 0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00 },   // U+002F (/)
    { 0x7C, 0xC6, 0xCE, 0xDE, 0xF6, 0xE6, 0x7C, 0x00 },   // U+0030 (0)
    { 0x30, 0x70, 0x30, 0x30, 0x30, 0x30, 0xFC, 0x00 },   // U+0031 (1)
    { 0x78, 0xCC, 0x0C, 0x38, 0x60, 0xCC, 0xFC, 0x00 },   // U+0032 (2)
    { 0x78, 0xCC, 0x0C, 0x38, 0x0C, 0xCC, 0x78, 0x00 },   // U+0033 (3)
    { 0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x1E, 0x00 },   // U+0034 (4)
    { 0xFC, 0xC0, 0xF8, 0x0C, 0x0C, 0xCC, 0x78, 0x00 },   // U+0035 (5)
    { 0x38, 0x60, 0xC0, 0xF8, 0xCC, 0xCC, 0x78, 0x00 },   // U+0036 (6)
    { 0xFC, 0xCC, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00 },   // U+0037 (7)
    { 0x78, 0xCC, 0xCC, 0x78, 0xCC, 0xCC, 0x78, 0x00 },   // U+0038 (8)
    { 0x78, 0xCC, 0xCC, 0x7C, 0x0C, 0x18, 0x70, 0x00 },   // U+0039 (9)
    { 0x00, 0x30, 0x30, 0x00, 0x00, 0x30, 0x30, 0x00 },   // U+003A (:)
    { 0x00, 0x30, 0x30, 0x00, 0x00, 0x30, 0x30, 0x60 },   // U+003B (;)
    { 0x18, 0x30, 0x60, 0xC0, 0x60, 0x30, 0x18, 0x00 },   // U+003C (<)
    { 0x00, 0x00, 0xFC, 0x00, 0x00, 0xFC, 0x00, 0x00 },   // U+003D (=)
    { 0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60, 0x00 },   // U+003E (>)
    { 0x78, 0xCC, 0x0C, 0x18, 0x30, 0x00, 0x30, 0x00 },   // U+003F (?)
    { 0x7C, 0xC6, 0xDE, 0xDE, 0xDE, 0xC0, 0x78, 0x00 },   // U+0040 (@)
    { 0x30, 0x78, 0xCC, 0xCC, 0xFC, 0xCC, 0xCC, 0x00 },   // U+0041 (A)
    { 0xFC, 0x66, 0x66, 0x7C, 0x66, 0x66, 0xFC, 0x00 },   // U+0042 (B)
    { 0x3C, 0x66, 0xC0, 0xC0, 0xC0, 0x66, 0x3C, 0x00 },   // U+0043 (C)
    { 0xF8, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0xF8, 0x00 },   // U+0044 (D)
    { 0xFE, 0x62, 0x68, 0x78, 0x68, 0x62, 0xFE, 0x00 },   // U+0045 (E)
    { 0xFE, 0x62, 0x68, 0x78, 0x68, 0x60, 0xF0, 0x00 },   // U+0046 (F)
    { 0x3C, 0x66, 0xC0, 0xC0, 0xCE, 0x66, 0x3E, 0x00 },   // U+0047 (G)
    { 0xCC, 0xCC, 0xCC, 0xFC, 0xCC, 0xCC, 0xCC, 0x00 },   // U+0048 (H)
    { 0x78, 0x30, 0x30, 0x30, 0x30, 0x30, 0x78, 0x00 },   // U+0049 (I)
    { 0x1E, 0x0C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78, 0x00 },   // U+004A (J)
    { 0xE6, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0xE6, 0x00 },   // U+004B (K)
    { 0xF0, 0x60, 0x60, 0x60, 0x62, 0x66, 0xFE, 0x00 },   // U+004C (L)
    { 0xC6, 0xEE, 0xFE, 0xFE, 0xD6, 0xC6, 0xC6, 0x00 },   // U+004D (M)
    { 0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0x00 },   // U+004E (N)
    { 0x38, 0x6C, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00 },   // U+004F (O)
    { 0xFC, 0x66, 0x66, 0x7C, 0x60, 0x60, 0xF0, 0x00 },   // U+0050 (P)
    { 0x78, 0xCC, 0xCC, 0xCC, 0xDC, 0x78, 0x1C, 0x00 },   // U+0051 (Q)
    { 0xFC, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0xE6, 0x00 },   // U+0052 (R)
    { 0x78, 0xCC, 0xE0, 0x70, 0x1C, 0xCC, 0x78, 0x00 },   // U+0053 (S)
    { 0xFC, 0xB4, 0x30, 0x30, 0x30, 0x30, 0x78, 0x00 },   // U+0054 (T)
    { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xFC, 0x00 },   // U+0055 (U)
    { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0x78, 0x30, 0x00 },   // U+0056 (V)
    { 0xC6, 0xC6, 0xC6, 0xD6, 0xFE, 0xEE, 0xC6, 0x00 },   // U+0057 (W)
    { 0xC6, 0xC6, 0x6C, 0x38, 0x38, 0x6C, 0xC6, 0x00 },   // U+0058 (X)
    { 0xCC, 0xCC, 0xCC, 0x78, 0x30, 0x30, 0x78, 0x00 },   // U+0059 (Y)
    { 0xFE, 0xC6, 0x8C, 0x18, 0x32, 0x66, 0xFE, 0x00 },   // U+005A (Z)
    { 0x78, 0x60, 0x60, 0x60, 0x60, 0x60, 0x78, 0x00 },   // U+005B ([)
    { 0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0x00 },   // U+005C (\)
    { 0x78, 0x18, 0x18, 0x18, 0x18, 0x18, 0x78, 0x00 },   // U+005D (])
    { 0x10, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00 },   // U+005E (^)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF },   // U+005F (_)
    { 0x30, 0x30, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00 },   // U+0060 (`)
    { 0x00, 0x00, 0x78, 0x0C, 0x7C, 0xCC, 0x76, 0x00 },   // U+0061 (a)
    { 0xE0, 0x60, 0x60, 0x7C, 0x66, 0x66, 0xDC, 0x00 },   // U+0062 (b)
    { 0x00, 0x00, 0x78, 0xCC, 0xC0, 0xCC, 0x78, 0x00 },   // U+0063 (c)
    { 0x1C, 0x0C, 0x0C, 0x7C, 0xCC, 0xCC, 0x76, 0x00 },   // U+0064 (d)
    { 0x00, 0x00, 0x78, 0xCC, 0xFC, 0xC0, 0x78, 0x00 },   // U+0065 (e)
    { 0x38, 0x6C, 0x60, 0xF0, 0x60, 0x60, 0xF0, 0x00 },   // U+0066 (f)
    { 0x00, 0x00, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0xF8 },   // U+0067 (g)
    { 0xE0, 0x60, 0x6C, 0x76, 0x66, 0x66, 0xE6, 0x00 },   // U+0068 (h)
    { 0x30, 0x00, 0x70, 0x30, 0x30, 0x30, 0x78, 0x00 },   // U+0069 (i)
    { 0x0C, 0x00, 0x0C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78 },   // U+006A (j)
    { 0xE0, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0xE6, 0x00 },   // U+006B (k)
    { 0x70, 0x30, 0x30, 0x30, 0x30, 0x30, 0x78, 0x00 },   // U+006C (l)
    { 0x00, 0x00, 0xCC, 0xFE, 0xFE, 0xD6, 0xC6, 0x00 },   // U+006D (m)
    { 0x00, 0x00, 0xF8, 0xCC, 0xCC, 0xCC, 0xCC, 0x00 },   // U+006E (n)
    { 0x00, 0x00, 0x78, 0xCC, 0xCC, 0xCC, 0x78, 0x00 },   // U+006F (o)
    { 0x00, 0x00, 0xDC, 0x66, 0x66, 0x7C, 0x60, 0xF0 },   // U+0070 (p)
    { 0x00, 0x00, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0x1E },   // U+0071 (q)
    { 0x00, 0x00, 0xDC, 0x76, 0x66, 0x60, 0xF0, 0x00 },   // U+0072 (r)
    { 0x00, 0x00, 0x7C, 0xC0, 0x78, 0x0C, 0xF8, 0x00 },   // U+0073 (s)
    { 0x10, 0x30, 0x7C, 0x30, 0x30, 0x34, 0x18, 0x00 },   // U+0074 (t)
    { 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0x76, 0x00 },   // U+0075 (u)
    { 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0x78, 0x30, 0x00 },   // U+0076 (v)
    { 0x00, 0x00, 0xC6, 0xD6, 0xFE, 0xFE, 0x6C, 0x00 },   // U+0077 (w)
    { 0x00, 0x00, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0x00 },   // U+0078 (x)
    { 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0x7C, 0x0C, 0xF8 },   // U+0079 (y)
    { 0x00, 0x00, 0xFC, 0x98, 0x30, 0x64, 0xFC, 0x00 },   // U+007A (z)
    { 0x1C, 0x30, 0x30, 0xE0, 0x30, 0x30, 0x1C, 0x00 },   // U+007B ({)
    { 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00 },   // U+007C (|)
    { 0xE0, 0x30, 0x30, 0x1C, 0x30, 0x30, 0xE0, 0x00 },   // U+007D (})
    { 0x76, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // U+007E (~)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // U+007F ()
};

// ------------------------------------------------------------------
// FPS bookkeeping (EMA over I_FinishUpdate-to-I_FinishUpdate deltas)
// ------------------------------------------------------------------

static Uint64 overlay_last_pc = 0;
static double overlay_ema_ms = 0.0;

// ------------------------------------------------------------------
// Text rendering
// ------------------------------------------------------------------

#define OVERLAY_PAD       2
#define OVERLAY_GLYPH_W   8
#define OVERLAY_GLYPH_H   8
// 320 - 2*2 padding = 316 usable pixels = 39 glyphs per row.
#define OVERLAY_MAX_CHARS 39

// Clip an over-long string to max_chars glyphs, keeping its tail
// (the file name is the interesting end of a shader path).
static void OverlayClipN(char *dest, size_t dest_size, const char *src,
                         int max_chars)
{
    size_t len;

    if (src == NULL)
    {
        src = "";
    }
    len = strlen(src);

    if (max_chars < 4)
    {
        max_chars = 4;
    }
    if (len > (size_t) max_chars)
    {
        snprintf(dest, dest_size, "...%s",
                 src + len - (max_chars - 3));
    }
    else
    {
        snprintf(dest, dest_size, "%s", src);
    }
}

// Clip to a full overlay row.
static void OverlayClip(char *dest, size_t dest_size, const char *src)
{
    OverlayClipN(dest, dest_size, src, OVERLAY_MAX_CHARS);
}

static void OverlayDrawText(pixel_t *buf, const char *s,
                            int x, int y, byte color)
{
    const unsigned char *glyph;
    int row;
    int col;
    int i;

    for (i = 0; s[i] != '\0'; ++i)
    {
        unsigned char ch = (unsigned char) s[i];

        if (ch < 32 || ch > 127)
        {
            ch = '?';
        }
        glyph = overlay_font[ch - 32];

        for (row = 0; row < OVERLAY_GLYPH_H; ++row)
        {
            byte bits = glyph[row];
            pixel_t *dst;

            if (y + row < 0 || y + row >= SCREENHEIGHT)
            {
                continue;
            }
            dst = buf + (y + row) * SCREENWIDTH + x + i * OVERLAY_GLYPH_W;

            for (col = 0; col < OVERLAY_GLYPH_W; ++col)
            {
                if (bits & (0x80 >> col))
                {
                    int px = x + i * OVERLAY_GLYPH_W + col;

                    if (px >= 0 && px < SCREENWIDTH)
                    {
                        dst[col] = color;
                    }
                }
            }
        }
    }
}

// ------------------------------------------------------------------
// Backend / shader description lines
// ------------------------------------------------------------------

static void OverlaySDLLine(char *dest, size_t dest_size)
{
    SDL_RendererInfo info;
    const char *renderer = "unknown renderer";
    const char *driver;
    int w = 0;
    int h = 0;
    char raw[64];

    if (SDL_GetRenderer(screen) != NULL
        && SDL_GetRendererInfo(SDL_GetRenderer(screen), &info) == 0
        && info.name != NULL)
    {
        renderer = info.name;
    }
    driver = SDL_GetCurrentVideoDriver();
    if (driver == NULL)
    {
        driver = "?";
    }
    SDL_GetWindowSize(screen, &w, &h);

    snprintf(raw, sizeof(raw), "%.24s | %.12s | %dx%d",
             renderer, driver, w, h);
    OverlayClip(dest, dest_size, raw);
}

static void OverlayShaderLine(char *dest, size_t dest_size)
{
    char clipped[40];
    char raw[64];
    int passes;

    if (shader_preset == NULL || shader_preset[0] == '\0')
    {
        snprintf(dest, dest_size, "%s",
                 I_VK_Active() ? "no shader"
                               : "no shader (classic renderer)");
        return;
    }

    OverlayClip(clipped, sizeof(clipped), shader_preset);
    passes = I_VK_PassCount();
    if (!I_VK_Active())
    {
        // A preset is configured but the Vulkan path did not start;
        // say so instead of silently implying it is in use.
        snprintf(raw, sizeof(raw), "%s (unused)", clipped);
    }
    else if (passes > 0)
    {
        snprintf(raw, sizeof(raw), "%s | %d pass%s", clipped, passes,
                 passes == 1 ? "" : "es");
    }
    else
    {
        snprintf(raw, sizeof(raw), "%s", clipped);
    }
    OverlayClip(dest, dest_size, raw);
}

// Feed mode tag: the canvas the shader chain sees — native 320x240/320x200,
// or the enlarged base (shader_base_resolution / -vkbase) while the Vulkan
// path runs, e.g. "640x480 4:3 (2x)".
static void OverlayModeLine(char *dest, size_t dest_size)
{
    int w = SCREENWIDTH;
    int h = aspect_ratio_correct ? SCREENHEIGHT_4_3 : SCREENHEIGHT;
    int scale = 1;

    if (I_VK_Active())
    {
        I_VK_FeedSize(&w, &h);
        scale = I_VK_BaseScale();
    }

    if (scale > 1)
    {
        snprintf(dest, dest_size, "%dx%d %s (%dx)",
                 w, h, aspect_ratio_correct ? "4:3" : "1:1", scale);
    }
    else
    {
        snprintf(dest, dest_size, "%dx%d %s",
                 w, h, aspect_ratio_correct ? "4:3" : "1:1");
    }
}

// ------------------------------------------------------------------
// Public entry points
// ------------------------------------------------------------------

void I_OverlayToggle(void)
{
    vkstats = !vkstats;
}

void I_OverlayDraw(void)
{
    char lines[4][64];
    const char *device;
    Uint64 pc;
    double dt_ms;
    int col_value;
    int col_plain;
    int col_box;
    int width;
    int height;
    int nlines;
    int i;
    int x;
    int y;
    pixel_t *buf = I_VideoBuffer;

    if (!vkstats || buf == NULL || !screenvisible)
    {
        return;
    }

    // --- frame rate (smoothed over presentation cadence) ---

    pc = SDL_GetPerformanceCounter();
    if (overlay_last_pc != 0)
    {
        dt_ms = (double) (pc - overlay_last_pc) * 1000.0
              / (double) SDL_GetPerformanceFrequency();
        if (dt_ms > 0.0 && dt_ms < 1000.0)
        {
            overlay_ema_ms = overlay_ema_ms > 0.0
                ? overlay_ema_ms * 0.9 + dt_ms * 0.1
                : dt_ms;
        }
    }
    overlay_last_pc = pc;

    // --- compose the rows ---

    nlines = 0;

    if (I_VK_Active())
    {
        snprintf(lines[nlines++], sizeof(lines[0]), "Vulkan | libslang-vk");

        device = I_VK_DeviceName();
        if (device == NULL || device[0] == '\0')
        {
            device = "unknown adapter";
        }
        OverlayClip(lines[nlines++], sizeof(lines[0]), device);
    }
    else
    {
        snprintf(lines[nlines++], sizeof(lines[0]), "SDL2 | classic");
        OverlaySDLLine(lines[nlines++], sizeof(lines[0]));
    }

    OverlayShaderLine(lines[nlines++], sizeof(lines[0]));

    if (overlay_ema_ms > 0.0)
    {
        char mode[32];

        OverlayModeLine(mode, sizeof(mode));
        snprintf(lines[nlines++], sizeof(lines[0]), "%.1f fps | %.2f ms | %s",
                 1000.0 / overlay_ema_ms, overlay_ema_ms, mode);
    }
    else
    {
        char mode[32];

        OverlayModeLine(mode, sizeof(mode));
        snprintf(lines[nlines++], sizeof(lines[0]), "-- fps | %s", mode);
    }

    // --- palette-matched colors (survive gamma and pain tints) ---

    col_value = I_GetPaletteIndex(255, 216, 80);    // values: amber
    col_plain = I_GetPaletteIndex(232, 232, 232);   // plain text: near-white
    col_box   = I_GetPaletteIndex(0, 0, 0);         // backdrop: black

    // --- box + text ---

    width = OVERLAY_PAD * 2;
    for (i = 0; i < nlines; ++i)
    {
        int w = (int) strlen(lines[i]) * OVERLAY_GLYPH_W;
        if (w + OVERLAY_PAD * 2 > width)
        {
            width = w + OVERLAY_PAD * 2;
        }
    }
    height = nlines * OVERLAY_GLYPH_H + OVERLAY_PAD * 2;

    for (y = 0; y < height && y < SCREENHEIGHT; ++y)
    {
        for (x = 0; x < width && x < SCREENWIDTH; ++x)
        {
            buf[y * SCREENWIDTH + x] = col_box;
        }
    }

    for (i = 0; i < nlines; ++i)
    {
        OverlayDrawText(buf, lines[i], OVERLAY_PAD,
                        OVERLAY_PAD + i * OVERLAY_GLYPH_H,
                        (i == 0 || i == nlines - 1) ? col_value : col_plain);
    }
}
