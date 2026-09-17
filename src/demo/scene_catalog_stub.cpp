/*
 * The empty scene catalog, linked when the image is built without the editor.
 *
 * An image built straight from tools/build.ps1 has no cooked scenes, because
 * nothing cooked any. It still has to link, and the boot host still has to be
 * able to ask what is in the catalog, so this answers honestly: no entries and
 * no startup scene. The host reads that as "fall back to the scene built in
 * code" rather than as a failure.
 *
 * tools/build.ps1 compiles this only when no generated catalog is supplied;
 * compiling both would be a duplicate definition at link time, which is the
 * intended way for that mistake to surface.
 */
#include "scene_catalog.hpp"

namespace {
const char kNoStartupScene[] = "";
}

extern "C" const char *he_x360_startup_scene_id() { return kNoStartupScene; }

extern "C" const HEX360SceneCatalogEntry *
he_x360_scene_catalog_entries(std::size_t *count) {
    if (count) {
        *count = 0;
    }
    return nullptr;
}
