/*
 * The contract between the platform builder and the console for authored
 * scenes.
 *
 * The builder knows which cooked file holds which scene; the console has no
 * file system and no way to discover that for itself, so the mapping is
 * compiled into the image as a table. This header is the single declaration
 * of that table. The builder's generated source includes this header rather
 * than emitting its own copy, so the two cannot drift apart: a change here is
 * a compile error in the generated file rather than a mismatch that only
 * shows up as a scene failing to load on the target.
 *
 * Exactly one translation unit defines these functions. Either the builder's
 * generated xbox360_runtime_scene_catalog.cpp, or scene_catalog_stub.cpp for
 * an image built without the editor, which reports an empty catalog.
 */
#pragma once

#include <cstddef>

/// <summary>One authored scene and the cooked file the content archive holds it in.</summary>
struct HEX360SceneCatalogEntry {
    /// <summary>Stable scene id the engine loads by.</summary>
    const char *SceneId;

    /// <summary>Path of the cooked scene inside the content archive.</summary>
    const char *CookedRelativePath;
};

extern "C" {

/// <summary>
/// The scene to load at boot, or an empty string when the image carries no
/// authored scenes.
/// </summary>
/// <remarks>Never null, so a caller can test it without a null check first.</remarks>
const char *he_x360_startup_scene_id();

/// <summary>Every authored scene in the image.</summary>
/// <param name="count">Receives the number of entries; may be null.</param>
/// <returns>The entry table, which is null only when the count is zero.</returns>
const HEX360SceneCatalogEntry *he_x360_scene_catalog_entries(std::size_t *count);
}
