/*
 * Reads the cooked content archive linked into the image.
 *
 * The console has no file system, so cooked assets travel inside the image
 * as one archive built by tools/pack-content.py and placed in read-only
 * data by the generated stub. This walks its table; the archive is written
 * big-endian, which is this machine's own byte order, so its fields are
 * read directly.
 *
 * An empty archive is valid and is what an image with no staged content
 * links, so the lookup path is the same either way and reports "not
 * present" rather than failing differently.
 */
#include <stdint.h>

#include "../../engine/frame.h"
#include "xenia_guest.h"

/* Provided by the generated stub (build/content/content.S). */
extern const uint8_t hel_content_archive_start[];
extern const uint8_t hel_content_archive_end[];

#define HEL_CONTENT_MAGIC 0x48454C43u /* 'HELC' */
#define HEL_CONTENT_VERSION 1u
#define HEL_CONTENT_HEADER_BYTES 16u
#define HEL_CONTENT_ENTRY_BYTES 16u

typedef struct {
    uint32_t path_offset;
    uint32_t path_length;
    uint32_t data_offset;
    uint32_t data_length;
} HelContentEntry;

static const uint8_t *g_archive;
static uint32_t g_archive_bytes;
static uint32_t g_count;
static uint32_t g_lookups;
static uint32_t g_misses;

static uint32_t read_u32(uint32_t offset) {
    const uint8_t *at = g_archive + offset;
    return ((uint32_t)at[0] << 24) | ((uint32_t)at[1] << 16) |
           ((uint32_t)at[2] << 8) | (uint32_t)at[3];
}

int hel_content_init(void) {
    g_archive = hel_content_archive_start;
    const uint32_t linked =
        (uint32_t)(hel_content_archive_end - hel_content_archive_start);
    if (linked < HEL_CONTENT_HEADER_BYTES) {
        hel_log("HEL content: archive is too small to hold a header");
        g_archive = 0;
        return HEL_ERR_MESH_INVALID;
    }
    if (read_u32(0) != HEL_CONTENT_MAGIC) {
        hel_log("HEL content: archive magic does not match");
        g_archive = 0;
        return HEL_ERR_MESH_INVALID;
    }
    if (read_u32(4) != HEL_CONTENT_VERSION) {
        hel_log_u32("HEL content: unsupported archive version", read_u32(4));
        g_archive = 0;
        return HEL_ERR_MESH_INVALID;
    }
    g_count = read_u32(8);
    g_archive_bytes = read_u32(12);
    if (g_archive_bytes > linked ||
        g_count > (linked - HEL_CONTENT_HEADER_BYTES) / HEL_CONTENT_ENTRY_BYTES) {
        hel_log_u32("HEL content: archive claims more bytes than are linked",
                    g_archive_bytes);
        g_archive = 0;
        return HEL_ERR_MESH_INVALID;
    }
    hel_log_u32("HEL content: entries", g_count);
    hel_log_u32("HEL content: bytes", g_archive_bytes);
    return HEL_OK;
}

static int entry_at(uint32_t index, HelContentEntry *out) {
    const uint32_t base = HEL_CONTENT_HEADER_BYTES + index * HEL_CONTENT_ENTRY_BYTES;
    out->path_offset = read_u32(base);
    out->path_length = read_u32(base + 4);
    out->data_offset = read_u32(base + 8);
    out->data_length = read_u32(base + 12);
    /* Every span must lie inside the archive: the table is data like any
       other and a corrupt image should be reported, not followed. */
    if (out->path_offset + out->path_length + 1u > g_archive_bytes ||
        out->data_offset + out->data_length > g_archive_bytes) {
        hel_log_u32("HEL content: entry points outside the archive", index);
        return 0;
    }
    return 1;
}

static int paths_match(const char *wanted, const HelContentEntry *entry) {
    const char *stored = (const char *)(g_archive + entry->path_offset);
    for (uint32_t i = 0; i < entry->path_length; ++i) {
        char left = wanted[i];
        char right = stored[i];
        /* Cooked paths are written with forward slashes; accept either
           separator from the caller so a path built on the host still
           matches. */
        if (left == '\\') {
            left = '/';
        }
        if (left != right) {
            return 0;
        }
    }
    return wanted[entry->path_length] == 0;
}

int hel_content_open(const char *path, const uint8_t **data, uint32_t *size) {
    if (data) {
        *data = 0;
    }
    if (size) {
        *size = 0;
    }
    if (!g_archive || !path || !data || !size) {
        return HEL_ERR_MESH_INVALID;
    }
    g_lookups++;
    /* Skip a leading separator so "/scene.hasset" and "scene.hasset" are the
       same asset. */
    if (path[0] == '/' || path[0] == '\\') {
        path++;
    }
    for (uint32_t index = 0; index < g_count; ++index) {
        HelContentEntry entry;
        if (!entry_at(index, &entry)) {
            return HEL_ERR_MESH_INVALID;
        }
        if (paths_match(path, &entry)) {
            *data = g_archive + entry.data_offset;
            *size = entry.data_length;
            return HEL_OK;
        }
    }
    g_misses++;
    return HEL_ERR_MESH_INVALID;
}

uint32_t hel_content_count(void) { return g_count; }
uint32_t hel_content_lookups(void) { return g_lookups; }
uint32_t hel_content_misses(void) { return g_misses; }

const char *hel_content_path_at(uint32_t index) {
    HelContentEntry entry;
    if (!g_archive || index >= g_count || !entry_at(index, &entry)) {
        return 0;
    }
    return (const char *)(g_archive + entry.path_offset);
}
