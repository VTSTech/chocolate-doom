//
// i_vk_cache.c — SPIR-V disk cache for the Vulkan shader path.
//
// Copyright (C) 2025 VTSTech (Nigel Todman) — veritas@vts-tech.org
// License: GNU General Public License, version 2 (see COPYING.md).
//
// Host side of libslang-vk's M4.07 "cache enabler": the library computes
// a per-pass content key (a hex digest of the exact GLSL handed to
// glslang plus the compiler's version and target) and calls back for the
// bytes; this module owns the bytes on disk. See i_vk_cache.h for the
// design contract.
//
// File format (one file per pass, named <key>.spv):
//   offset  0: magic "SLVKSPC1"            (8 bytes)
//   offset  8: vertex stage size, uint32 LE
//   offset 12: fragment stage size, uint32 LE
//   offset 16: vertex blob || fragment blob
// Everything is validated on read (magic, size sanity, exact file
// length, full fread) — a torn or corrupt file is always a miss, never
// an error, and heals itself on the next compile+store.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "i_vk_cache.h"

#include "m_config.h"
#include "m_misc.h"

#ifdef ENABLE_VK_SHADERS

#define VK_CACHE_MAGIC     "SLVKSPC1"
#define VK_CACHE_MAGIC_LEN 8
#define VK_CACHE_HEADER    16
// Paranoia cap per stage blob: real pass SPIR-V is tens of KB; anything
// past this is a corrupt size field, not a shader.
#define VK_CACHE_MAX_BLOB  (32u << 20)
// "<configdir>/shader-cache/" + 64-char key + ".spv" + slack
// (1024 leaves room for any real configdir; a longer one would only
// truncate into a harmless miss).
#define VK_CACHE_PATH_MAX  1024

// Active cache directory ("" when the cache is off). Always ends in a
// path separator so entry paths join with a plain concat.
static char cache_dir[VK_CACHE_PATH_MAX];

// Read arena: one buffer reused by every VK_CacheRead hit. The library
// copies the blobs right after its load callback returns and the
// callbacks are strictly serial (one per pass, loading thread only), so
// a single arena is sufficient and nothing leaks per pass.
static unsigned char *cache_arena = NULL;
static size_t cache_arena_bytes = 0;

// Content keys are 64-char lowercase hex digests. Anything else is not
// ours — refuse it before it can reach a path join.
static int ValidKey(const char *key)
{
    size_t i;
    size_t len;

    if (key == NULL)
    {
        return 0;
    }
    len = strlen(key);
    if (len == 0 || len > 64)
    {
        return 0;
    }
    for (i = 0; i < len; ++i)
    {
        const char c = key[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        {
            return 0;
        }
    }
    return 1;
}

static uint32_t GetLE32(const unsigned char *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8)
         | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static void PutLE32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char) (v & 0xFF);
    p[1] = (unsigned char) ((v >> 8) & 0xFF);
    p[2] = (unsigned char) ((v >> 16) & 0xFF);
    p[3] = (unsigned char) ((v >> 24) & 0xFF);
}

static void EntryPath(char *out, size_t out_size, const char *key,
                      const char *suffix)
{
    snprintf(out, out_size, "%s%s%s", cache_dir, key, suffix);
}

static int ArenaEnsure(size_t bytes)
{
    unsigned char *grown;

    if (cache_arena_bytes >= bytes)
    {
        return 1;
    }
    grown = realloc(cache_arena, bytes);
    if (grown == NULL)
    {
        return 0;       // old block stays valid; a miss is harmless
    }
    cache_arena = grown;
    cache_arena_bytes = bytes;
    return 1;
}

int VK_CacheInit(void)
{
    const char *sep;
    size_t len;
    int n;
    char base[VK_CACHE_PATH_MAX];

    if (configdir == NULL || configdir[0] == '\0')
    {
        return 0;
    }

    // configdir conventionally ends in a separator (M_SetConfigDir);
    // add one when it doesn't so the joins below are always clean.
    len = strlen(configdir);
    sep = (len > 0 && configdir[len - 1] != '/'
        && configdir[len - 1] != '\\') ? "/" : "";

    n = snprintf(base, sizeof(base), "%s%s", configdir, sep);
    if (n < 0 || (size_t) n >= sizeof(base))
    {
        return 0;
    }

    n = snprintf(cache_dir, sizeof(cache_dir), "%sshader-cache", base);
    if (n < 0 || (size_t) n >= sizeof(cache_dir) - 1)
    {
        cache_dir[0] = '\0';
        return 0;
    }

    M_MakeDirectory(base);         // in case configdir itself is fresh
    M_MakeDirectory(cache_dir);    // best-effort: a miss is harmless

    // Keep the trailing separator for the entry-path joins.
    cache_dir[n] = '/';
    cache_dir[n + 1] = '\0';
    return 1;
}

const char *VK_CacheDir(void)
{
    return cache_dir;
}

int VK_CacheRead(const char *key,
                 const unsigned char **vert, size_t *vert_size,
                 const unsigned char **frag, size_t *frag_size)
{
    unsigned char header[VK_CACHE_HEADER];
    char path[VK_CACHE_PATH_MAX];
    FILE *f;
    long total;
    uint32_t vs, fs;

    if (cache_dir[0] == '\0' || !ValidKey(key) || vert == NULL
        || vert_size == NULL || frag == NULL || frag_size == NULL)
    {
        return 0;
    }

    EntryPath(path, sizeof(path), key, ".spv");
    f = M_fopen(path, "rb");
    if (f == NULL)
    {
        return 0;
    }

    do
    {
        if (fread(header, 1, sizeof(header), f) != sizeof(header))
        {
            break;
        }
        if (memcmp(header, VK_CACHE_MAGIC, VK_CACHE_MAGIC_LEN) != 0)
        {
            break;
        }
        vs = GetLE32(header + VK_CACHE_MAGIC_LEN);
        fs = GetLE32(header + VK_CACHE_MAGIC_LEN + 4);
        if (vs == 0 || fs == 0
            || vs > VK_CACHE_MAX_BLOB || fs > VK_CACHE_MAX_BLOB)
        {
            break;
        }

        // Exact length: the trailing-garbage and truncated-file cases
        // both land here (a torn concurrent store can never pass).
        if (fseek(f, 0, SEEK_END) != 0)
        {
            break;
        }
        total = ftell(f);
        if (total < 0
            || (unsigned long) total != (unsigned long) VK_CACHE_HEADER + vs + fs)
        {
            break;
        }
        if (fseek(f, VK_CACHE_HEADER, SEEK_SET) != 0)
        {
            break;
        }

        if (!ArenaEnsure((size_t) vs + (size_t) fs))
        {
            break;
        }
        if (fread(cache_arena, 1, vs, f) != vs)
        {
            break;
        }
        if (fread(cache_arena + vs, 1, fs, f) != fs)
        {
            break;
        }

        fclose(f);
        *vert = cache_arena;
        *vert_size = vs;
        *frag = cache_arena + vs;
        *frag_size = fs;
        return 1;
    } while (0);

    fclose(f);
    return 0;
}

void VK_CacheWrite(const char *key,
                   const unsigned char *vert, size_t vert_size,
                   const unsigned char *frag, size_t frag_size)
{
    unsigned char header[VK_CACHE_HEADER];
    char path[VK_CACHE_PATH_MAX];
    char tmp[VK_CACHE_PATH_MAX];
    FILE *f;
    int ok;

    if (cache_dir[0] == '\0' || !ValidKey(key))
    {
        return;
    }
    if (vert == NULL || frag == NULL)
    {
        return;
    }
    if (vert_size == 0 || frag_size == 0
        || vert_size > VK_CACHE_MAX_BLOB || frag_size > VK_CACHE_MAX_BLOB)
    {
        return;
    }

    EntryPath(path, sizeof(path), key, ".spv");
    EntryPath(tmp, sizeof(tmp), key, ".tmp");

    memcpy(header, VK_CACHE_MAGIC, VK_CACHE_MAGIC_LEN);
    PutLE32(header + VK_CACHE_MAGIC_LEN, (uint32_t) vert_size);
    PutLE32(header + VK_CACHE_MAGIC_LEN + 4, (uint32_t) frag_size);

    f = M_fopen(tmp, "wb");
    if (f == NULL)
    {
        return;         // read-only location, locked file, ... — miss
    }
    ok = fwrite(header, 1, sizeof(header), f) == sizeof(header)
      && fwrite(vert, 1, vert_size, f) == vert_size
      && fwrite(frag, 1, frag_size, f) == frag_size;
    if (fclose(f) != 0)
    {
        ok = 0;
    }
    if (!ok)
    {
        M_remove(tmp);
        return;
    }

    // rename() cannot replace an existing destination on Windows, so
    // remove first; on POSIX the remove is a harmless no-op before an
    // otherwise-atomic replace. A miss inside the tiny window simply
    // recompiles and re-stores. M_remove/M_rename are the UTF-8-safe
    // wrappers, matching M_fopen above.
    M_remove(path);
    if (M_rename(tmp, path) != 0)
    {
        M_remove(tmp);
    }
}

#else   // !ENABLE_VK_SHADERS — the whole feature is compiled out.

int VK_CacheInit(void)
{
    return 0;
}

const char *VK_CacheDir(void)
{
    return "";
}

int VK_CacheRead(const char *key,
                 const unsigned char **vert, size_t *vert_size,
                 const unsigned char **frag, size_t *frag_size)
{
    (void) key; (void) vert; (void) vert_size; (void) frag; (void) frag_size;
    return 0;
}

void VK_CacheWrite(const char *key,
                   const unsigned char *vert, size_t vert_size,
                   const unsigned char *frag, size_t frag_size)
{
    (void) key; (void) vert; (void) vert_size; (void) frag; (void) frag_size;
}

#endif  // ENABLE_VK_SHADERS
