//
// i_video_vk.c — Vulkan post-processing presenter for Chocolate Doom VK.
//
// Copyright (C) 2025 VTSTech (Nigel Todman) — veritas@vts-tech.org
// Upstream video path: Copyright (C) 1993-1996 Id Software, Inc.,
// Copyright (C) 2005-2014 Simon Howard.
// License: GNU General Public License, version 2 (see COPYING.md).
//
// Runs libretro slang shader presets (.slangp/.slang) over the 320x200
// 8-bit software frame through VTSTech/libslang-vk's pure-C API, presenting
// into a host-owned Vulkan swapchain:
//
//   I_VideoBuffer (320x200, PLAYPAL indices, CPU-rasterized by the engine)
//       | palette LUT expand  (+ 6:5 nearest stretch when 4:3-corrected)
//       | + k-fold nearest base upscale (shader_base_resolution / -vkbase,
//       |   the DevilutionX "Shader Base Resolution" analog)
//       v
//   RGBA8 320x240*k / 320x200*k  --slangvk_image_update-->  input image
//       | slangvk_chain_render_swapchain (preset pass chain, letterboxed
//       | by the library onto the swapchain image, M4 output mode (a))
//       v
//   vkQueuePresentKHR  (render() is synchronous/fenced: no wait semaphore)
//
// The presentation side mirrors libslang-vk's reference host
// (examples/shader_demo/window_target.hpp): the host owns the window, the
// VkSurfaceKHR, the swapchain and the acquire/present pair. Device creation
// is delegated to the library (slangvk_device_create), which loads the
// Vulkan runtime through volk — and this file keeps the same policy: no
// link-time Vulkan dependency, every entry point it calls directly is
// resolved through SDL_Vulkan_GetVkGetInstanceProcAddr.
//
// Compile-time gate: ENABLE_VK_SHADERS (CMake option ENABLE_VK_SHADERS,
// default ON). The #else stub section at the bottom keeps every call site
// in i_video.c valid when the feature is configured out.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"
#include "SDL_vulkan.h"

#include "config.h"
#include "doomtype.h"
#include "i_system.h"
#include "i_video.h"
#include "i_video_vk.h"
#include "i_overlay.h"
#include "m_argv.h"
#include "m_misc.h"

// ------------------------------------------------------------------
// Config-exposed options (bound in I_BindVideoVariables / parsed in
// I_GraphicsCheckCommandLine, both in i_video.c)
// ------------------------------------------------------------------

int usevulkan = 1;              // master switch (default on)
char *shader_preset = "";       // activation: setting a preset activates VK
int vk_debug = 0;              // -vkdebug: validation layers
int vk_capture_frame = 0;       // -vkcapture <n>: dump frame n as vkframe.tga
int shader_base_resolution = 0; // -vkbase: base canvas size (0=auto, 320xk)

// window_width/window_height/fullscreen_width/fullscreen_height are
// declared in i_video.h (this file includes it): cross-file declarations
// belong in headers, not .c files — that is the repo's style gate.

#ifdef ENABLE_VK_SHADERS

#include <vulkan/vulkan.h>

#include "slangvk/c.h"

// Debounce for swapchain recreation after a window resize (the classic
// path uses 500ms; swapchain recreation is cheaper than a mode switch).
#define VK_RESIZE_DELAY_MS 200

#define VK_MAX_SWAPCHAIN_IMAGES 8

// ------------------------------------------------------------------
// Runtime state
// ------------------------------------------------------------------

static boolean vk_active = false;

// Stats overlay source data (i_overlay.c): adapter identity
// and preset depth, filled in during I_VK_AttemptGraphics.
static char vk_device_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = "";
static int vk_pass_count = 0;

boolean I_VK_Active(void)
{
    return vk_active;
}

// Adapter description + preset pass count for the stats
// overlay (i_overlay.c); ""/0 until the Vulkan path is up.
const char *I_VK_DeviceName(void)
{
    return vk_device_name;
}

int I_VK_PassCount(void)
{
    return vk_pass_count;
}

// Why the Vulkan path is not running: set at every failure exit of
// I_VK_AttemptGraphics (and by the stub build below), shown in the
// stats overlay's shader row so a fallback to the classic renderer is
// never silent — stderr is invisible in Windows GUI builds, which is
// exactly where these failures are most common (e.g. an SDL2 without
// Vulkan support compiled in).
static char vk_fallback_reason[96] = "";

static void SetFallbackReason(const char *reason)
{
    M_StringCopy(vk_fallback_reason, reason, sizeof(vk_fallback_reason));
}

const char *I_VK_FallbackReason(void)
{
    return vk_active ? "" : vk_fallback_reason;
}

// libslang-vk objects (destroy order per c.h: chain -> preset -> image ->
// device, with this file's own Vulkan objects torn down before the device).
static slangvk_device *vk_device = NULL;
static slangvk_preset *vk_preset = NULL;
static slangvk_chain *vk_chain = NULL;
static slangvk_image *vk_input = NULL;

static VkInstance vk_instance = VK_NULL_HANDLE;
static VkPhysicalDevice vk_physical = VK_NULL_HANDLE;
static VkDevice vk_ldevice = VK_NULL_HANDLE;
static VkQueue vk_queue = VK_NULL_HANDLE;
static uint32_t vk_family = 0;

static VkSurfaceKHR vk_surface = VK_NULL_HANDLE;
static VkSwapchainKHR vk_swapchain = VK_NULL_HANDLE;
static VkSemaphore vk_acquire_sem = VK_NULL_HANDLE;
static VkFormat vk_swap_format = VK_FORMAT_UNDEFINED;
static VkExtent2D vk_swap_extent = {0, 0};
static VkImage vk_swap_images[VK_MAX_SWAPCHAIN_IMAGES];
static VkImageView vk_swap_views[VK_MAX_SWAPCHAIN_IMAGES];
static uint32_t vk_swap_image_count = 0;
static boolean vk_swap_transfer_src = false;

// Feed state: expansion LUT (endian-safe [r,g,b,255] byte tuples), the
// 240-entry source-row map for the 4:3 stretch, and the staging buffer.
static unsigned char vk_lut[256][4];
static int vk_rowmap[SCREENHEIGHT_4_3];
static unsigned char *vk_staging = NULL;   // base_width * base_height * 4
static size_t vk_staging_bytes = 0;

// Shader base resolution: the effective integer multiplier (1..4) the
// feed is nearest-neighbor upscaled by before the preset chain runs.
// Resolved once at startup from shader_base_resolution; the chain then
// re-derives every pass FBO from the per-frame input size, so no other
// state depends on it (libslang-vk M4 contract, same as the aspect
// toggle). The cap keeps the CPU staging copy and the input-space
// shader cost bounded (4x = 1280x960 = 4.9 MB RGBA per frame).
#define VK_BASE_MAX_MULT 4
static int vk_base_mult = 1;

// Frame accounting for slangvk_frame_info.
static uint64_t vk_frame_count = 0;
static uint64_t vk_last_pc = 0;

// PrintScreen hotkey request (set by I_VK_RequestCapture from the event
// pump in i_video.c, consumed in I_VK_FinishUpdate right where -vkcapture
// reads the presented image — after the chain render, before present).
static boolean vk_shot_request = false;

// ------------------------------------------------------------------
// Entry points resolved at runtime (no link-time Vulkan dependency;
// mirrors the policy of the library itself and of shader_demo).
// ------------------------------------------------------------------

static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr_ = NULL;
static PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr_ = NULL;
static PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR_ = NULL;
static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR_ = NULL;
static PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR_ = NULL;
static PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR_ = NULL;
static PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR_ = NULL;
static PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR_ = NULL;
static PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR_ = NULL;
static PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR_ = NULL;
static PFN_vkQueuePresentKHR vkQueuePresentKHR_ = NULL;
static PFN_vkCreateImageView vkCreateImageView_ = NULL;
static PFN_vkDestroyImageView vkDestroyImageView_ = NULL;
static PFN_vkCreateSemaphore vkCreateSemaphore_ = NULL;
static PFN_vkDestroySemaphore vkDestroySemaphore_ = NULL;
static PFN_vkDeviceWaitIdle vkDeviceWaitIdle_ = NULL;

static boolean LoadVulkanFunctions(void)
{
    vkGetInstanceProcAddr_ =
        (PFN_vkGetInstanceProcAddr) (uintptr_t) SDL_Vulkan_GetVkGetInstanceProcAddr();
    if (vkGetInstanceProcAddr_ == NULL)
    {
        fprintf(stderr, "I_VK: SDL_Vulkan_GetVkGetInstanceProcAddr returned NULL "
                        "(no Vulkan loader available?)\n");
        return false;
    }

    // Instance-level entry points resolve through the instance; device-level
    // ones through vkGetDeviceProcAddr (type-correct, per-spec dispatch).
    vkGetDeviceProcAddr_ = (PFN_vkGetDeviceProcAddr)
        vkGetInstanceProcAddr_(vk_instance, "vkGetDeviceProcAddr");
    if (vkGetDeviceProcAddr_ == NULL)
    {
        fprintf(stderr, "I_VK: failed to resolve vkGetDeviceProcAddr\n");
        return false;
    }

#define VK_LOAD_DEV(fn)                                                     \
    do                                                                      \
    {                                                                       \
        fn##_ = (PFN_##fn) vkGetDeviceProcAddr_(vk_ldevice, #fn);          \
        if (fn##_ == NULL)                                                  \
        {                                                                   \
            fprintf(stderr, "I_VK: failed to resolve %s\n", #fn);           \
            return false;                                                   \
        }                                                                   \
    } while (0)

#define VK_LOAD_INST(fn)                                                    \
    do                                                                      \
    {                                                                       \
        fn##_ = (PFN_##fn) vkGetInstanceProcAddr_(vk_instance, #fn);        \
        if (fn##_ == NULL)                                                  \
        {                                                                   \
            fprintf(stderr, "I_VK: failed to resolve %s\n", #fn);           \
            return false;                                                   \
        }                                                                   \
    } while (0)

    VK_LOAD_INST(vkGetPhysicalDeviceSurfaceSupportKHR);
    VK_LOAD_INST(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    VK_LOAD_INST(vkGetPhysicalDeviceSurfaceFormatsKHR);
    VK_LOAD_INST(vkDestroySurfaceKHR);
    VK_LOAD_DEV(vkCreateSwapchainKHR);
    VK_LOAD_DEV(vkDestroySwapchainKHR);
    VK_LOAD_DEV(vkGetSwapchainImagesKHR);
    VK_LOAD_DEV(vkAcquireNextImageKHR);
    VK_LOAD_DEV(vkQueuePresentKHR);
    VK_LOAD_DEV(vkCreateImageView);
    VK_LOAD_DEV(vkDestroyImageView);
    VK_LOAD_DEV(vkCreateSemaphore);
    VK_LOAD_DEV(vkDestroySemaphore);
    VK_LOAD_DEV(vkDeviceWaitIdle);

#undef VK_LOAD_DEV
#undef VK_LOAD_INST
    return true;
}

// ------------------------------------------------------------------
// Window (assigned straight into i_video.c's `screen` so the shared event
// pump, title/icon code and shutdown path all keep working unmodified)
// ------------------------------------------------------------------

static boolean CreateVKWindow(void)
{
    int x, y;
    int w = window_width;
    int h = window_height;
    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
                 | SDL_WINDOW_VULKAN;

    if (fullscreen)
    {
        if (fullscreen_width == 0 && fullscreen_height == 0)
        {
            flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        }
        else
        {
            w = fullscreen_width;
            h = fullscreen_height;
            flags |= SDL_WINDOW_FULLSCREEN;
        }
    }

    if (M_ParmExists("-borderless"))
    {
        flags |= SDL_WINDOW_BORDERLESS;
    }

    I_GetWindowPosition(&x, &y, w, h);

    screen = SDL_CreateWindow(NULL, x, y, w, h, flags);
    if (screen == NULL)
    {
        fprintf(stderr, "I_VK: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_SetWindowMinimumSize(screen, SCREENWIDTH, SCREENHEIGHT_4_3);
    I_InitWindowTitle();
    I_InitWindowIcon();
    return true;
}

// ------------------------------------------------------------------
// Swapchain — mirrors examples/shader_demo/window_target.hpp:
// B8G8R8A8_UNORM (SRGB_NONLINEAR) preferred, FIFO present, minImageCount+1,
// COLOR_ATTACHMENT (+TRANSFER_SRC when available, for -vkcapture readback).
// ------------------------------------------------------------------

static void DestroySwapchainObjects(void)
{
    uint32_t i;

    if (vk_ldevice == VK_NULL_HANDLE)
    {
        return;
    }

    for (i = 0; i < vk_swap_image_count; ++i)
    {
        if (vk_swap_views[i] != VK_NULL_HANDLE)
        {
            vkDestroyImageView_(vk_ldevice, vk_swap_views[i], NULL);
            vk_swap_views[i] = VK_NULL_HANDLE;
        }
    }
    vk_swap_image_count = 0;

    if (vk_swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR_(vk_ldevice, vk_swapchain, NULL);
        vk_swapchain = VK_NULL_HANDLE;
    }
}

static boolean CreateSwapchain(void)
{
    VkSurfaceCapabilitiesKHR caps;
    VkSurfaceFormatKHR formats[16];
    VkSurfaceFormatKHR chosen;
    VkSwapchainCreateInfoKHR ci;
    VkSemaphoreCreateInfo si;
    uint32_t count;
    uint32_t i;
    VkExtent2D extent;
    VkImageUsageFlags usage;

    memset(&caps, 0, sizeof(caps));
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR_(vk_physical, vk_surface, &caps);

    // Wait for un-minimize: the caller retries on the next frame.
    if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0)
    {
        return false;
    }

    count = 16;
    memset(formats, 0, sizeof(formats));
    vkGetPhysicalDeviceSurfaceFormatsKHR_(vk_physical, vk_surface, &count,
                                          formats);
    if (count == 0)
    {
        fprintf(stderr, "I_VK: surface reports no formats\n");
        return false;
    }

    chosen = formats[0];
    for (i = 0; i < count; ++i)
    {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM
            && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            chosen = formats[i];
            break;
        }
        if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM
            && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            chosen = formats[i];
            break;
        }
    }

    extent = caps.currentExtent;
    if (extent.width == UINT32_MAX)
    {
        int dw = 0, dh = 0;
        SDL_Vulkan_GetDrawableSize(screen, &dw, &dh);
        extent.width = (uint32_t) dw;
        extent.height = (uint32_t) dh;
    }
    extent.width = SDL_max(SDL_min(extent.width, caps.maxImageExtent.width),
                           caps.minImageExtent.width);
    extent.height = SDL_max(SDL_min(extent.height, caps.maxImageExtent.height),
                            caps.minImageExtent.height);

    usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    vk_swap_transfer_src =
        (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    if (vk_swap_transfer_src)
    {
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;   // -vkcapture readback
    }

    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = vk_surface;
    ci.minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && ci.minImageCount > caps.maxImageCount)
    {
        ci.minImageCount = caps.maxImageCount;
    }
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = usage;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;     // guaranteed everywhere
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR_(vk_ldevice, &ci, NULL, &vk_swapchain) != VK_SUCCESS)
    {
        fprintf(stderr, "I_VK: vkCreateSwapchainKHR failed\n");
        return false;
    }

    count = VK_MAX_SWAPCHAIN_IMAGES;
    if (vkGetSwapchainImagesKHR_(vk_ldevice, vk_swapchain, &count, vk_swap_images)
            != VK_SUCCESS || count == 0)
    {
        fprintf(stderr, "I_VK: vkGetSwapchainImagesKHR failed\n");
        DestroySwapchainObjects();
        return false;
    }
    vk_swap_image_count = count;

    for (i = 0; i < vk_swap_image_count; ++i)
    {
        VkImageViewCreateInfo vi;
        memset(&vi, 0, sizeof(vi));
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = vk_swap_images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = chosen.format;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView_(vk_ldevice, &vi, NULL, &vk_swap_views[i]) != VK_SUCCESS)
        {
            fprintf(stderr, "I_VK: vkCreateImageView failed\n");
            DestroySwapchainObjects();
            return false;
        }
    }

    if (vk_acquire_sem == VK_NULL_HANDLE)
    {
        memset(&si, 0, sizeof(si));
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (vkCreateSemaphore_(vk_ldevice, &si, NULL, &vk_acquire_sem) != VK_SUCCESS)
        {
            fprintf(stderr, "I_VK: vkCreateSemaphore failed\n");
            DestroySwapchainObjects();
            return false;
        }
    }

    vk_swap_format = chosen.format;
    vk_swap_extent = extent;
    return true;
}

// Idempotent: called on resize / OUT_OF_DATE. Also re-points the chain's
// output target (slangvk_chain_resize) per the c.h contract.
static boolean RecreateSwapchain(void)
{
    if (vk_ldevice != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle_(vk_ldevice);
    }
    DestroySwapchainObjects();
    if (!CreateSwapchain())
    {
        return false;       // minimized / transient — retry next frame
    }
    if (vk_chain != NULL
        && slangvk_chain_resize(vk_chain, vk_swap_extent.width,
                                 vk_swap_extent.height) != SLANGVK_OK)
    {
        I_Error("I_VK: slangvk_chain_resize failed: %s", slangvk_last_error());
    }
    return true;
}

// ------------------------------------------------------------------
// Feed: palette LUT + 6:5 row stretch (320x240) or raw 320x200
// ------------------------------------------------------------------

static int VKFeedHeight(void)
{
    return aspect_ratio_correct ? SCREENHEIGHT_4_3 : SCREENHEIGHT;
}

// The shader-chain input canvas: the feed duplicated k-fold (nearest,
// pixel-perfect, DevilutionX "Shader Base Resolution" analog).
static int VKBaseWidth(void)
{
    return SCREENWIDTH * vk_base_mult;
}

static int VKBaseHeight(void)
{
    return VKFeedHeight() * vk_base_mult;
}

// Resolve the configured base size into the effective integer multiplier
// (1..VK_BASE_MAX_MULT). Accepted forms: 0 = auto (native), a multiplier
// directly (what -vkbase 2 passes), or a pixel width (what the config
// stores, e.g. shader_base_resolution 640). Out-of-range values clamp to
// the nearest bound; the effective choice is printed at startup.
static void ResolveBaseMultiplier(void)
{
    int mult = 1;

    if (shader_base_resolution > 0 && shader_base_resolution <= VK_BASE_MAX_MULT)
    {
        mult = shader_base_resolution;          // multiplier form: 1..4
    }
    else if (shader_base_resolution > VK_BASE_MAX_MULT)
    {
        mult = shader_base_resolution / SCREENWIDTH;    // width form: 640 -> 2
    }

    if (mult < 1)
    {
        mult = 1;
    }
    if (mult > VK_BASE_MAX_MULT)
    {
        mult = VK_BASE_MAX_MULT;
    }
    vk_base_mult = mult;
}

// Effective chain-input canvas for the stats overlay (i_overlay.c):
// the base size while the Vulkan path runs, the native feed otherwise.
void I_VK_FeedSize(int *w, int *h)
{
    const int mult = vk_active ? vk_base_mult : 1;

    if (w != NULL)
    {
        *w = SCREENWIDTH * mult;
    }
    if (h != NULL)
    {
        *h = VKFeedHeight() * mult;
    }
}

int I_VK_BaseScale(void)
{
    return vk_active ? vk_base_mult : 1;
}

static void BuildRowmap(void)
{
    int y;
    const int dst_h = VKFeedHeight();

    // 4:3: 240 output rows cover 200 source rows (nearest, authentic mode
    // 13h stretch). Raw: identity.
    for (y = 0; y < dst_h; ++y)
    {
        vk_rowmap[y] = aspect_ratio_correct
            ? (y * SCREENHEIGHT) / SCREENHEIGHT_4_3
            : y;
    }
}

// (Re)create the input image at the current base size. Aspect toggles flip
// 320x240 <-> 320x200 (x k); the chain re-derives its FBOs and history ring
// from the per-frame input size on the next render (libslang-vk M2 contract).
static void EnsureInputImage(void)
{
    slangvk_image_options io;

    if (vk_input != NULL)
    {
        VkImageView view; VkImage img; VkFormat fmt; uint32_t w, h;
        if (slangvk_image_get_handles(vk_input, &view, &img, &fmt, &w, &h) == SLANGVK_OK
            && w == (uint32_t) VKBaseWidth() && h == (uint32_t) VKBaseHeight())
        {
            return;
        }
        slangvk_image_destroy(vk_input);
        vk_input = NULL;
    }

    memset(&io, 0, sizeof(io));
    io.width = VKBaseWidth();
    io.height = VKBaseHeight();
    io.format = VK_FORMAT_R8G8B8A8_UNORM;
    io.final_layout = 0;       // SHADER_READ_ONLY_OPTIMAL (c.h default)

    if (slangvk_image_create(vk_device, &io, &vk_input) != SLANGVK_OK)
    {
        I_Error("I_VK: slangvk_image_create failed: %s", slangvk_last_error());
    }
}

// Palette-LUT-expand the frame into the staging buffer at the base size.
// The 6:5 aspect stretch (rowmap) happens first in row space; the base
// upscale then duplicates each expanded row k times and each pixel k times
// within the row (pure nearest: game pixels stay exact k x k blocks). One
// LUT pass writes the first row of each k-row block, memcpy fills the rest
// — at 4x that is 3.7 MB of the 4.9 MB frame as memcpy. k = 1 reduces to
// the original single-pass expansion.
static void ExpandFrame(void)
{
    const int feed_h = VKFeedHeight();
    const int mult = vk_base_mult;
    const size_t row_bytes = (size_t) SCREENWIDTH * mult * 4;
    const size_t bytes = row_bytes * (size_t) (feed_h * mult);
    int x, y, rep;

    if (vk_staging_bytes < bytes)
    {
        free(vk_staging);
        vk_staging = malloc(bytes);
        if (vk_staging == NULL)
        {
            I_Error("I_VK: out of memory (staging buffer)");
        }
        vk_staging_bytes = bytes;
    }

    for (y = 0; y < feed_h; ++y)
    {
        const byte *src = I_VideoBuffer + (size_t) vk_rowmap[y] * SCREENWIDTH;
        unsigned char *row = vk_staging + (size_t) y * mult * row_bytes;
        unsigned char *dst = row;

        for (x = 0; x < SCREENWIDTH; ++x)
        {
            const unsigned char *c = vk_lut[src[x]];
            for (rep = 0; rep < mult; ++rep)
            {
                dst[0] = c[0]; dst[1] = c[1]; dst[2] = c[2]; dst[3] = c[3];
                dst += 4;
            }
        }
        for (rep = 1; rep < mult; ++rep)
        {
            memcpy(row + (size_t) rep * row_bytes, row, row_bytes);
        }
    }

    if (slangvk_image_update(vk_input, vk_staging, bytes) != SLANGVK_OK)
    {
        I_Error("I_VK: slangvk_image_update failed: %s", slangvk_last_error());
    }
}

// ------------------------------------------------------------------
// Frame readback: -vkcapture <n> (vkframe.tga) and the PrintScreen
// hotkey (numbered vkshotNNN.tga). Swapchain images are created with
// TRANSFER_SRC when supported; the library reads them back with a raw
// vkCmdCopyImageToBuffer, so the bytes arrive in the surface format's
// native packing.
// ------------------------------------------------------------------

// Write the given swapchain image as a 32-bit uncompressed, top-down TGA.
// Channel order: B8G8R8A8* memory is B,G,R,A — already exactly TGA's
// component order, so it goes out untouched; R8G8B8A8* memory is R,G,B,A
// and needs the outer channels swapped. (The previous unconditional swap
// red/blue-flipped every B8G8R8A8 surface — i.e. virtually every real
// platform — producing the "blue shifted" captures.) Alpha is forced to
// 255 either way (TGA has no straight-alpha convention worth honoring).
static boolean WriteSwapchainTGA(uint32_t idx, const char *path)
{
    const size_t bytes = (size_t) vk_swap_extent.width * vk_swap_extent.height * 4;
    const boolean swap_rb =
        (vk_swap_format == VK_FORMAT_R8G8B8A8_UNORM
      || vk_swap_format == VK_FORMAT_R8G8B8A8_SRGB);
    unsigned char header[18];
    unsigned char *px;
    FILE *f;
    size_t i;

    if (!vk_swap_transfer_src || idx >= vk_swap_image_count)
    {
        fprintf(stderr, "I_VK: readback unavailable on this swapchain\n");
        return false;
    }

    px = malloc(bytes);
    if (px == NULL)
    {
        return false;
    }

    if (slangvk_device_read_pixels(vk_device, vk_swap_images[idx], vk_swap_format,
            vk_swap_extent.width, vk_swap_extent.height,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, px, bytes) != SLANGVK_OK)
    {
        fprintf(stderr, "I_VK: readback failed: %s\n", slangvk_last_error());
        free(px);
        return false;
    }

    for (i = 0; i < bytes / 4; ++i)
    {
        if (swap_rb)
        {
            unsigned char t = px[i * 4 + 0];
            px[i * 4 + 0] = px[i * 4 + 2];
            px[i * 4 + 2] = t;
        }
        px[i * 4 + 3] = 0xFF;
    }

    memset(header, 0, sizeof(header));
    header[2] = 2;                                        // true-color
    header[12] = vk_swap_extent.width & 0xFF;
    header[13] = (vk_swap_extent.width >> 8) & 0xFF;
    header[14] = vk_swap_extent.height & 0xFF;
    header[15] = (vk_swap_extent.height >> 8) & 0xFF;
    header[16] = 32;                                      // bpp
    header[17] = 0x20;                                    // top-left origin

    f = fopen(path, "wb");
    if (f == NULL)
    {
        fprintf(stderr, "I_VK: cannot open '%s' for writing\n", path);
        free(px);
        return false;
    }
    fwrite(header, 1, sizeof(header), f);
    fwrite(px, 1, bytes, f);
    fclose(f);
    free(px);
    return true;
}

// -vkcapture <n>: dump the n-th presented frame to vkframe.tga.
static void CaptureTGA(uint32_t idx)
{
    if (WriteSwapchainTGA(idx, "vkframe.tga"))
    {
        printf("I_VK: captured frame %d -> vkframe.tga (%ux%u)\n",
               vk_capture_frame, vk_swap_extent.width, vk_swap_extent.height);
    }
    vk_capture_frame = 0;
}

// PrintScreen hotkey: first free vkshotNNN.tga (probed portably with
// fopen — no readdir, same code on MSVC/MinGW), so repeated presses
// never overwrite an earlier shot.
static boolean NextShotName(char *out, size_t out_size)
{
    int n;

    for (n = 0; n < 10000; ++n)
    {
        FILE *probe;

        snprintf(out, out_size, "vkshot%03d.tga", n);
        probe = fopen(out, "rb");
        if (probe == NULL)
        {
            return true;
        }
        fclose(probe);
    }
    return false;
}

static void HotkeyShot(uint32_t idx)
{
    char name[16];

    if (!NextShotName(name, sizeof(name)))
    {
        fprintf(stderr, "I_VK: no free vkshotNNN.tga slot\n");
        return;
    }
    if (WriteSwapchainTGA(idx, name))
    {
        printf("I_VK: hotkey screenshot -> %s (%ux%u)\n",
               name, vk_swap_extent.width, vk_swap_extent.height);
        I_OverlayFlash("shot %s", name);
    }
}

// ------------------------------------------------------------------
// Frame: feed -> acquire -> chain render -> (capture) -> present
// ------------------------------------------------------------------

void I_VK_FinishUpdate(void)
{
    slangvk_input_frame in;
    slangvk_swapchain_frame out;
    slangvk_frame_info fi;
    VkPresentInfoKHR pi;
    VkResult r;
    uint64_t pc;
    double delta_us;
    uint32_t idx;
    int dw, dh;

    if (vk_chain == NULL || vk_input == NULL || !screenvisible)
    {
        return;
    }

    // Debounced window resize.
    if (need_resize)
    {
        if (SDL_GetTicks() < last_resize_time + VK_RESIZE_DELAY_MS)
        {
            return;             // let the window settle first
        }
        need_resize = false;
        if (!RecreateSwapchain())
        {
            return;
        }
    }

    // Extent drift (fullscreen toggles, DPI changes).
    SDL_Vulkan_GetDrawableSize(screen, &dw, &dh);
    if ((uint32_t) dw != vk_swap_extent.width
        || (uint32_t) dh != vk_swap_extent.height)
    {
        if (!RecreateSwapchain())
        {
            return;             // minimized — retry next frame
        }
    }

    EnsureInputImage();
    ExpandFrame();

    r = vkAcquireNextImageKHR_(vk_ldevice, vk_swapchain, UINT64_MAX,
                               vk_acquire_sem, NULL, &idx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR)
    {
        RecreateSwapchain();
        return;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
    {
        I_Error("I_VK: vkAcquireNextImageKHR failed (%d)", (int) r);
    }

    pc = SDL_GetPerformanceCounter();
    delta_us = vk_last_pc != 0
        ? (double) (pc - vk_last_pc) * 1e6 / (double) SDL_GetPerformanceFrequency()
        : 16667.0;
    vk_last_pc = pc;

    memset(&in, 0, sizeof(in));
    slangvk_image_get_handles(vk_input, &in.view, &in.image, &in.format,
                              &in.width, &in.height);
    in.rotation = SLANGVK_ROTATION_NONE;

    memset(&out, 0, sizeof(out));
    out.swapchain_image_view = vk_swap_views[idx];
    out.format = vk_swap_format;
    out.width = vk_swap_extent.width;
    out.height = vk_swap_extent.height;
    // output_rect all-zero: the library fits + letterboxes (M4 mode (a)).
    out.acquire_semaphore = vk_acquire_sem;

    fi.frame_count = vk_frame_count;
    fi.frame_delta_us = (float) delta_us;
    fi.frame_direction = 1;

    if (slangvk_chain_render_swapchain(vk_chain, &in, &out, &fi) != SLANGVK_OK)
    {
        I_Error("I_VK: chain render failed: %s", slangvk_last_error());
    }

    if (vk_capture_frame > 0 && (int) vk_frame_count == vk_capture_frame - 1)
    {
        CaptureTGA(idx);       // before present: image rests in PRESENT_SRC_KHR
    }

    if (vk_shot_request)
    {
        vk_shot_request = false;
        HotkeyShot(idx);
    }

    memset(&pi, 0, sizeof(pi));
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.swapchainCount = 1;
    pi.pSwapchains = &vk_swapchain;
    pi.pImageIndices = &idx;
    r = vkQueuePresentKHR_(vk_queue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
    {
        RecreateSwapchain();
    }

    ++vk_frame_count;
}

// ------------------------------------------------------------------
// Palette / aspect / resize hooks
// ------------------------------------------------------------------

void I_VK_SetPalette(const SDL_Color *palette)
{
    int i;

    // Endian-safe byte tuples: R,G,B,255 for VK_FORMAT_R8G8B8A8_UNORM.
    for (i = 0; i < 256; ++i)
    {
        vk_lut[i][0] = palette[i].r;
        vk_lut[i][1] = palette[i].g;
        vk_lut[i][2] = palette[i].b;
        vk_lut[i][3] = 0xFF;
    }
}

void I_VK_FlagResize(void)
{
    // need_resize / last_resize_time are maintained by i_video.c's
    // HandleWindowEvent; I_VK_FinishUpdate consumes them directly.
}

void I_VK_ToggleAspect(void)
{
    aspect_ratio_correct = !aspect_ratio_correct;
    BuildRowmap();
    printf("I_VK: aspect correction %s — feed %dx%d, base %dx%d\n",
           aspect_ratio_correct ? "ON (authentic 4:3)" : "OFF (raw square pixels)",
           SCREENWIDTH, VKFeedHeight(), VKBaseWidth(), VKBaseHeight());
    // EnsureInputImage() picks the new size up on the next frame.
}

// F12: cycle the shader base multiplier 1x -> 2x -> 3x -> 4x -> 1x. The
// new value is written back into shader_base_resolution (multiplier form)
// so the choice persists through the config file like the F11 aspect
// flip does; EnsureInputImage() re-creates the input image and the chain
// re-derives its pass FBOs from the per-frame input size on the next
// render — the same M4 contract the aspect toggle rides on.
void I_VK_CycleBase(void)
{
    vk_base_mult = vk_base_mult % VK_BASE_MAX_MULT + 1;
    shader_base_resolution = vk_base_mult;
    printf("I_VK: shader base resolution %dx — feed %dx%d -> base %dx%d\n",
           vk_base_mult, SCREENWIDTH, VKFeedHeight(),
           VKBaseWidth(), VKBaseHeight());
    I_OverlayFlash("base %dx%d (%dx)", VKBaseWidth(), VKBaseHeight(),
                   vk_base_mult);
}

// PrintScreen: capture the next presented frame (see HotkeyShot).
void I_VK_RequestCapture(void)
{
    vk_shot_request = true;
}

// ------------------------------------------------------------------
// Teardown (idempotent; also used to clean up partial init on fallback)
// ------------------------------------------------------------------

void I_VK_ShutdownGraphics(void)
{
    if (vk_ldevice != VK_NULL_HANDLE && vkDeviceWaitIdle_ != NULL)
    {
        vkDeviceWaitIdle_(vk_ldevice);
    }

    // libslang-vk objects, c.h destroy order.
    if (vk_chain != NULL)   { slangvk_chain_destroy(vk_chain);   vk_chain = NULL; }
    if (vk_preset != NULL)  { slangvk_preset_destroy(vk_preset); vk_preset = NULL; }
    if (vk_input != NULL)   { slangvk_image_destroy(vk_input);   vk_input = NULL; }

    // This file's Vulkan objects, then the library's device.
    DestroySwapchainObjects();
    if (vk_ldevice != VK_NULL_HANDLE && vkDestroySemaphore_ != NULL
        && vk_acquire_sem != VK_NULL_HANDLE)
    {
        vkDestroySemaphore_(vk_ldevice, vk_acquire_sem, NULL);
        vk_acquire_sem = VK_NULL_HANDLE;
    }
    if (vk_instance != VK_NULL_HANDLE && vkDestroySurfaceKHR_ != NULL
        && vk_surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR_(vk_instance, vk_surface, NULL);
        vk_surface = VK_NULL_HANDLE;
    }
    if (vk_device != NULL)
    {
        slangvk_device_destroy(vk_device);
        vk_device = NULL;
    }

    vk_instance = VK_NULL_HANDLE;
    vk_ldevice = VK_NULL_HANDLE;
    vk_physical = VK_NULL_HANDLE;
    vk_queue = VK_NULL_HANDLE;
    vk_swap_format = VK_FORMAT_UNDEFINED;
    vk_swap_extent.width = vk_swap_extent.height = 0;

    SDL_Vulkan_UnloadLibrary();

    free(vk_staging);
    vk_staging = NULL;
    vk_staging_bytes = 0;

    if (vk_active)
    {
        free(I_VideoBuffer);
        I_VideoBuffer = NULL;
    }
    vk_active = false;
}

// ------------------------------------------------------------------
// Startup: window -> device (library-owned) -> surface -> swapchain ->
// input image -> preset -> chain -> canvas. Any failure cleans up and
// returns false; i_video.c falls back to the classic SDL2 renderer.
// ------------------------------------------------------------------

boolean I_VK_AttemptGraphics(void)
{
    const char **inst_exts = NULL;
    const char *dev_exts[1];
    slangvk_device_options dopt;
    slangvk_device_info dinfo;
    slangvk_image_options io;
    uint32_t ext_count = 0;
    uint32_t pass_count = 0;
    VkBool32 present_supported = VK_FALSE;
    int feed_h;

    SetFallbackReason("");

    if (!usevulkan || shader_preset == NULL || shader_preset[0] == '\0')
    {
        if (!usevulkan)
        {
            // Explicitly off for this run (a -novulkan run persists
            // usevulkan = 0). Say so: without this the fallback is
            // indistinguishable from a failure.
            SetFallbackReason("usevulkan = 0");
        }
        return false;         // VK path not requested — classic renderer
    }

    ResolveBaseMultiplier();

    // 1) Window (becomes i_video.c's `screen`; on failure the classic
    //    SetVideoMode() reuses or recreates it).
    if (!CreateVKWindow())
    {
        SetFallbackReason("window creation failed");
        return false;
    }

    if (SDL_Vulkan_LoadLibrary(NULL) != 0)
    {
        fprintf(stderr, "I_VK: SDL_Vulkan_LoadLibrary failed: %s\n",
                SDL_GetError());
        SetFallbackReason("SDL2 has no Vulkan support");
        I_VK_ShutdownGraphics();
        return false;
    }

    // 2) Instance extensions: exactly what SDL needs for this surface.
    if (SDL_Vulkan_GetInstanceExtensions(screen, &ext_count, NULL) == SDL_FALSE
        || ext_count == 0)
    {
        fprintf(stderr, "I_VK: SDL_Vulkan_GetInstanceExtensions failed\n");
        SetFallbackReason("SDL2 lacks Vulkan windowing");
        I_VK_ShutdownGraphics();
        return false;
    }
    inst_exts = malloc(sizeof(const char *) * ext_count);
    if (inst_exts == NULL
        || SDL_Vulkan_GetInstanceExtensions(screen, &ext_count, inst_exts) == SDL_FALSE)
    {
        fprintf(stderr, "I_VK: SDL_Vulkan_GetInstanceExtensions failed\n");
        SetFallbackReason("SDL2 lacks Vulkan windowing");
        free(inst_exts);
        I_VK_ShutdownGraphics();
        return false;
    }

    memset(&dopt, 0, sizeof(dopt));
    dopt.app_name = PACKAGE_STRING " (VK)";
    dopt.min_api_version = 0;               // VK 1.1
    dopt.validation_layers = vk_debug ? 1u : 0u;
    dopt.instance_extensions = inst_exts;
    dopt.instance_extension_count = ext_count;
    dev_exts[0] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    dopt.device_extensions = dev_exts;
    dopt.device_extension_count = 1;

    // 3) Device — created by the library (headless-friendly create mode).
    if (slangvk_device_create(&dopt, &vk_device) != SLANGVK_OK)
    {
        fprintf(stderr, "I_VK: slangvk_device_create failed: %s\n",
                slangvk_last_error());
        SetFallbackReason("Vulkan device init failed");
        free(inst_exts);
        I_VK_ShutdownGraphics();
        return false;
    }
    free(inst_exts);
    inst_exts = NULL;

    if (slangvk_device_get_handles(vk_device, &vk_instance, &vk_physical,
                                   &vk_ldevice, &vk_queue, &vk_family) != SLANGVK_OK)
    {
        fprintf(stderr, "I_VK: slangvk_device_get_handles failed\n");
        SetFallbackReason("Vulkan device init failed");
        I_VK_ShutdownGraphics();
        return false;
    }

    if (!LoadVulkanFunctions())
    {
        SetFallbackReason("Vulkan entry points missing");
        I_VK_ShutdownGraphics();
        return false;
    }

    // 4) Surface + present support on the device's graphics family.
    if (SDL_Vulkan_CreateSurface(screen, vk_instance, &vk_surface) != SDL_TRUE)
    {
        fprintf(stderr, "I_VK: SDL_Vulkan_CreateSurface failed: %s\n",
                SDL_GetError());
        SetFallbackReason("Vulkan surface creation failed");
        I_VK_ShutdownGraphics();
        return false;
    }

    vkGetPhysicalDeviceSurfaceSupportKHR_(vk_physical, vk_family, vk_surface,
                                          &present_supported);
    if (present_supported != VK_TRUE)
    {
        fprintf(stderr, "I_VK: graphics queue does not present to this surface\n");
        SetFallbackReason("GPU cannot present to window");
        I_VK_ShutdownGraphics();
        return false;
    }

    // 5) Swapchain (mirrors the reference host's choices).
    if (!CreateSwapchain())
    {
        SetFallbackReason("swapchain creation failed");
        I_VK_ShutdownGraphics();
        return false;
    }

    // 6) Input image at the base size (feed x base multiplier; the chain
    //    re-derives every pass FBO from this per-frame input size).
    feed_h = VKFeedHeight();
    memset(&io, 0, sizeof(io));
    io.width = VKBaseWidth();
    io.height = VKBaseHeight();
    io.format = VK_FORMAT_R8G8B8A8_UNORM;
    if (slangvk_image_create(vk_device, &io, &vk_input) != SLANGVK_OK)
    {
        fprintf(stderr, "I_VK: slangvk_image_create failed: %s\n",
                slangvk_last_error());
        SetFallbackReason("Vulkan image creation failed");
        I_VK_ShutdownGraphics();
        return false;
    }

    // 7) Preset — the friendly failure point (bad path / parse error).
    if (slangvk_preset_load(vk_device, shader_preset, NULL, &vk_preset) != SLANGVK_OK)
    {
        fprintf(stderr, "I_VK: failed to load preset '%s': %s\n",
                shader_preset, slangvk_last_error());
        SetFallbackReason("preset failed to load");
        I_VK_ShutdownGraphics();
        return false;
    }
    slangvk_preset_pass_count(vk_preset, &pass_count);
    vk_pass_count = (int) pass_count;

    // 8) Chain bound to the swapchain (extent + format).
    if (slangvk_chain_create(vk_device, vk_preset, vk_swap_extent.width,
                             vk_swap_extent.height, vk_swap_format,
                             &vk_chain) != SLANGVK_OK)
    {
        fprintf(stderr, "I_VK: slangvk_chain_create failed: %s\n",
                slangvk_last_error());
        SetFallbackReason("shader chain creation failed");
        I_VK_ShutdownGraphics();
        return false;
    }

    // 9) Feed + canvas.
    BuildRowmap();
    memset(vk_lut, 0, sizeof(vk_lut));
    I_VideoBuffer = malloc(SCREENWIDTH * SCREENHEIGHT);
    if (I_VideoBuffer == NULL)
    {
        SetFallbackReason("out of memory");
        I_VK_ShutdownGraphics();
        return false;
    }
    memset(I_VideoBuffer, 0, SCREENWIDTH * SCREENHEIGHT);

    vk_active = true;

    if (slangvk_device_get_info(vk_device, &dinfo) == SLANGVK_OK)
    {
        M_StringCopy(vk_device_name, dinfo.device_name,
                     sizeof(vk_device_name));
        printf("I_VK: libslang-vk active — '%s' (%u pass%s) on %s\n",
               shader_preset, pass_count, pass_count == 1 ? "" : "es",
               dinfo.device_name);
    }
    else
    {
        printf("I_VK: libslang-vk active — '%s' (%u passes)\n",
               shader_preset, pass_count);
    }
    if (vk_base_mult > 1)
    {
        printf("I_VK: shader base resolution %dx — feed %dx%d -> base %dx%d "
               "(DevilutionX-style pre-scale before the preset chain)\n",
               vk_base_mult, SCREENWIDTH, feed_h,
               VKBaseWidth(), VKBaseHeight());
    }
    printf("I_VK: feed %dx%d (%s), base %dx%d, swapchain %ux%u, present FIFO\n",
           SCREENWIDTH, feed_h,
           aspect_ratio_correct ? "4:3 corrected" : "raw",
           VKBaseWidth(), VKBaseHeight(),
           vk_swap_extent.width, vk_swap_extent.height);

    return true;
}

#else   // !ENABLE_VK_SHADERS — stubs (classic path completely unaffected)

boolean I_VK_Active(void)                { return false; }
boolean I_VK_AttemptGraphics(void)       { return false; }

const char *I_VK_FallbackReason(void)
{
    // The feature was compiled out entirely; if a shader was requested
    // the overlay should still be able to explain itself.
    return (usevulkan && shader_preset != NULL && shader_preset[0] != '\0')
        ? "built without VK support" : "";
}

void I_VK_SetPalette(const SDL_Color *palette) { (void) palette; }
void I_VK_FlagResize(void)               { }
void I_VK_ToggleAspect(void)             { }
void I_VK_CycleBase(void)                { }
void I_VK_RequestCapture(void)           { }
void I_VK_FinishUpdate(void)             { }
void I_VK_ShutdownGraphics(void)         { }
const char *I_VK_DeviceName(void)        { return ""; }
int I_VK_PassCount(void)                 { return 0; }
void I_VK_FeedSize(int *w, int *h)
{
    if (w != NULL) { *w = SCREENWIDTH; }
    if (h != NULL) { *h = SCREENHEIGHT; }
}
int I_VK_BaseScale(void)                 { return 1; }

#endif  // ENABLE_VK_SHADERS

