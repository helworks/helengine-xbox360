#include "x360_shader_runtime.h"
#include "xenia_guest.h"
#include <string.h>

int hel_shader_program_open(const char *path, const char *asset,
    const char *vertex, const char *pixel, const char *variant,
    uint32_t profile, HelShaderProgram *out) {
    const uint8_t *bytes = 0;
    uint32_t size = 0;
    if (!out) return HEL_X3SP_ERROR_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!path) return HEL_X3SP_ERROR_ARGUMENT;
    if (hel_content_open(path, &bytes, &size) != HEL_OK) {
        /* Editor packages prefix cooker-relative paths with cooked/. Direct
           cooker archives keep their original paths; support both layouts. */
        char packaged_path[512];
        size_t length = strlen(path);
        if (length > sizeof(packaged_path) - sizeof("cooked/")) return HEL_X3SP_ERROR_ARGUMENT;
        memcpy(packaged_path, "cooked/", sizeof("cooked/") - 1);
        memcpy(packaged_path + sizeof("cooked/") - 1, path, length + 1);
        if (hel_content_open(packaged_path, &bytes, &size) != HEL_OK) return HEL_X3SP_ERROR_ARGUMENT;
    }
    return hel_shader_program_init(bytes, size, asset, vertex, pixel, variant, profile, out);
}
