//
// i_vk_cache.h — SPIR-V disk cache for the Vulkan shader path.
//
// Copyright (C) 2025 VTSTech (Nigel Todman) — veritas@vts-tech.org
// License: GNU General Public License, version 2 (see COPYING.md).
//
// libslang-vk deliberately keeps no disk cache of its own (two caches
// would disagree); what it provides instead is a correct key — a digest
// of the exact GLSL handed to glslang plus the compiler's version and
// target, so the key changes precisely when the compiled output would.
// This module is the host side of that bargain: it stores each pass's
// compiled SPIR-V under that key and serves it back on later runs,
// turning warm starts from "recompile every pass" (seconds on
// crt-royale-class presets) into "memcpy the bytes" (milliseconds).
//
// Invalidation is entirely content-driven: editing the preset, its
// #include graph or the library itself produces new keys — there is no
// version number to bump and nothing can go stale. Deleting the cache
// directory is always safe (it just recompiles).
//
// Everything here is best-effort by design: a missing, read-only,
// corrupt or half-written cache degrades to a miss + recompile and never
// fails a preset load. The whole module compiles to nothing when
// ENABLE_VK_SHADERS is off (see i_vk_cache.c).
//

#ifndef __I_VK_CACHE__
#define __I_VK_CACHE__

#include <stddef.h>

// Prepare the cache directory (<configdir>/shader-cache). Call once from
// I_VK_AttemptGraphics, before slangvk_preset_load. Returns 1 when the
// cache is active, 0 when it is disabled (feature compiled out, or the
// config dir is unknown). A directory that cannot be written is NOT an
// error: reads will miss and writes will silently no-op.
int VK_CacheInit(void);

// The active cache directory ("" when disabled) — for the startup log.
const char *VK_CacheDir(void);

// Load one pass's cached SPIR-V by its content key into the module's
// internal arena. The returned pointers are read-only and stay valid
// until the next VK_CacheRead call — the library copies the bytes
// immediately after its load callback returns (c.h bridges them into its
// own PassSpirv), and the callbacks are strictly serial: one per pass,
// on the preset-loading thread.
// Returns 1 on a hit (all four out-parameters filled), 0 on a miss
// (out-parameters untouched). A malformed or truncated file is a miss.
int VK_CacheRead(const char *key,
                 const unsigned char **vert, size_t *vert_size,
                 const unsigned char **frag, size_t *frag_size);

// Store one pass's compiled SPIR-V under its content key. Best-effort:
// writes to a temporary file and renames over the destination (a torn
// store can never be observed as a valid entry — the size check in
// VK_CacheRead rejects it, and the next compile re-stores). Never fails
// a load; failures are silent by design.
void VK_CacheWrite(const char *key,
                   const unsigned char *vert, size_t vert_size,
                   const unsigned char *frag, size_t frag_size);

#endif
