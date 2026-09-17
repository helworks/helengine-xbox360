/*
 * Boots the transpiled engine core inside the guest.
 *
 * This is the Xbox 360 counterpart of the PS1 port's core-boot host: it
 * constructs the generated Core with an empty scene catalog and no render,
 * input or audio backends, then runs its frame calls. Nothing the core draws
 * reaches the screen yet, because the render managers are still null; the
 * point of this stage is that the engine's own object graph, allocator use
 * and frame loop run on the console target at all.
 *
 * Every step logs a marker so a failure can be placed exactly:
 *   HEL core: constructing -> constructed -> initializing -> ready
 * and then per-frame counters from the first update and draw.
 *
 * This translation unit is compiled with the core's flags (hosted headers,
 * C++20), not the guest's freestanding flags; see tools/build.ps1.
 */
#include "Core.hpp"
#include "CoreInitializationOptions.hpp"
#include "IContentStreamSource.hpp"
#include "system/io/file-stream.hpp"
#include "PlatformInfo.hpp"
#include "RenderManager2D.hpp"
#include "RenderManager3D.hpp"

#include "../platform/xenia_guest/X360RenderManager3D.hpp"
#include "core_host.hpp"
#include "scene_catalog.hpp"
#include "RuntimeSceneCatalog.hpp"
#include "RuntimeSceneCatalogEntry.hpp"
#include "SceneLoadMode.hpp"
#include "SceneManager.hpp"
#include "runtime/array.hpp"

extern "C" {
void hel_log(const char *message);
void hel_log_u32(const char *label, unsigned int value);
unsigned int hel_heap_live_allocations(void);
unsigned int hel_heap_used_bytes(void);
unsigned int hel_video_width(void);
unsigned int hel_video_height(void);
}

namespace {

/* Serves the core from the archive linked into the image. The console has
   no file system, so a cooked asset is a span of read-only data rather than
   a file, and the generated FileStream has a constructor for exactly that.
   A request for something not staged is reported and answered with null,
   which is what the engine expects for a missing asset. */
class X360ContentStreamSource : public ::IContentStreamSource {
public:
    ::Stream *OpenRead(HeCppString assetPath) override {
        const uint8_t *data = nullptr;
        uint32_t size = 0;
        if (hel_content_open(assetPath.c_str(), &data, &size) != HEL_OK) {
            hel_log("HEL core: content not present in the image");
            hel_log(assetPath.c_str());
            misses++;
            return nullptr;
        }
        opened++;
        return new ::FileStream(data, (size_t)size);
    }

    unsigned int opened = 0;
    unsigned int misses = 0;
};

/* The 2D manager is still a stub: nothing draws through it yet. The 3D
   manager is the real one (X360RenderManager3D), which walks the engine's
   cameras and render queues and draws through the Xenos backend. */
class X360RenderManager2D : public ::RenderManager2D {
public:
    ::RuntimeTexture *BuildTextureFromRaw(::TextureAsset *data) override {
        (void)data;
        hel_log("HEL core: texture requested, no texture backend yet");
        textures++;
        return nullptr;
    }

    void DrawRoundedRect(::IRoundedRectDrawable2D *shape) override {
        (void)shape;
        draws2d++;
    }

    void DrawSprite(::ISpriteDrawable2D *sprite) override {
        (void)sprite;
        draws2d++;
    }

    void DrawText(::ITextDrawable2D *text) override {
        (void)text;
        draws2d++;
    }

    void UpdateTextureRegionCore(::RuntimeTexture *texture, int32_t x,
                                 int32_t y, int32_t width, int32_t height,
                                 Array<uint8_t> *rgba8,
                                 int32_t sourceRowPitch) override {
        (void)texture;
        (void)x;
        (void)y;
        (void)width;
        (void)height;
        (void)rgba8;
        (void)sourceRowPitch;
    }

    unsigned int textures = 0;
    unsigned int draws2d = 0;
};

/* Turns the table the builder compiled into the image into the catalog the
   core resolves scene ids through. An image built without the editor links
   the stub instead, which reports no entries; that is not a failure, it is
   the case where the scene is built in code. */
::RuntimeSceneCatalog *BuildSceneCatalog() {
    std::size_t count = 0;
    const HEX360SceneCatalogEntry *entries = he_x360_scene_catalog_entries(&count);
    if (!entries || count == 0) {
        hel_log("HEL core: no authored scenes in this image");
        return new ::RuntimeSceneCatalog(new Array<::RuntimeSceneCatalogEntry *>(0));
    }
    hel_log_u32("HEL core: authored scenes in the catalog", (unsigned int)count);
    Array<::RuntimeSceneCatalogEntry *> *table =
        new Array<::RuntimeSceneCatalogEntry *>((int32_t)count);
    for (std::size_t index = 0; index < count; index++) {
        (*table)[(int32_t)index] = new ::RuntimeSceneCatalogEntry(
            HeCppString(entries[index].SceneId),
            HeCppString(entries[index].CookedRelativePath));
    }
    return new ::RuntimeSceneCatalog(table);
}

helengine::x360::X360RenderManager3D *g_render3d;
X360RenderManager2D *g_render2d;
X360ContentStreamSource *g_content;
::Core *g_core;
::PlatformInfo *g_platform;
::CoreInitializationOptions *g_options;
unsigned int g_frames;

} // namespace

extern "C" int hel_core_boot(void) {
    hel_log("HEL core: constructing");
    hel_log_u32("HEL core: heap allocations before", hel_heap_live_allocations());

    g_options = new ::CoreInitializationOptions();
    g_content = new X360ContentStreamSource();
    g_options->set_ContentStreamSource(g_content);
    g_options->set_SceneCatalog(BuildSceneCatalog());
    g_platform = new ::PlatformInfo(HeCppString("xbox360"),
                                    HeCppString("core-boot"));
    g_core = new ::Core(g_options);
    hel_log("HEL core: constructed");
    hel_log_u32("HEL core: heap allocations after construction",
                hel_heap_live_allocations());

    hel_log("HEL core: initializing");
    /* Render managers are supplied because the engine's draw path requires
       them; there is still no input or audio backend. */
    g_render3d = new helengine::x360::X360RenderManager3D();
    /* The engine reads the window size from the render manager. */
    g_render3d->AddWindow(0, (int32_t)hel_video_width(),
                          (int32_t)hel_video_height());
    g_render2d = new X360RenderManager2D();
    g_core->Initialize(g_render3d, g_render2d, nullptr, g_platform, g_options);
    hel_log("HEL core: ready");
    hel_log_u32("HEL core: heap allocations after initialize",
                hel_heap_live_allocations());
    hel_log_u32("HEL core: heap bytes after initialize", hel_heap_used_bytes());

    /* An authored scene wins over the one built in code. The code-built scene
       exists so this target can be exercised without the editor; once the
       builder has cooked a real scene into the image, loading both would put
       two cameras in the object graph and draw the wrong one. */
    const char *const startupSceneId = he_x360_startup_scene_id();
    if (startupSceneId && startupSceneId[0] != '\0') {
        hel_log("HEL core: loading the authored startup scene");
        hel_log(startupSceneId);
        g_core->get_SceneManager()->LoadScene(HeCppString(startupSceneId),
                                              ::SceneLoadMode::Single);
        hel_log("HEL core: authored scene loaded");
        hel_log_u32("HEL core: content opened while loading it",
                    g_content ? g_content->opened : 0u);
        hel_log_u32("HEL core: content missing while loading it",
                    g_content ? g_content->misses : 0u);
    } else {
        const int sceneError = hel_core_scene_build();
        if (sceneError != 0) {
            hel_log_u32("HEL core: scene build failed with",
                        (unsigned int)sceneError);
            return sceneError;
        }
    }
    hel_log_u32("HEL core: heap allocations after the scene",
                hel_heap_live_allocations());
    return 0;
}

::Core *hel_core_instance() { return g_core; }

helengine::x360::X360RenderManager3D *hel_core_render3d() { return g_render3d; }

/* The managed frame is split in two so the engine's draw calls land between
   the backend's clear and its present. Running Update and Draw together
   before the frame began would emit geometry that the clear then wipes. */
extern "C" int hel_core_update(double elapsed_seconds) {
    if (!g_core) {
        return 0;
    }
    hel_core_scene_update((float)elapsed_seconds);
    g_core->Update(elapsed_seconds);
    if (g_frames == 0) {
        hel_log("HEL core: first update returned");
    }
    return 0;
}

extern "C" int hel_core_draw(void) {
    if (!g_core) {
        return 0;
    }
    g_core->Draw();
    g_core->CompleteFrameBoundary();
    if (g_frames == 0) {
        hel_log("HEL core: first update and draw complete");
        hel_log_u32("HEL core: drawables drawn in the first frame",
                    g_render3d ? g_render3d->lastDrawnCount() : 0u);
        hel_log_u32("HEL core: drawables skipped in the first frame",
                    g_render3d ? g_render3d->lastSkippedCount() : 0u);
        /* What the engine itself recorded, which is only non-zero if the
           renderer's draw-call count is actually overridable. */
        hel_log_u32("HEL core: engine recorded draw calls",
                    (unsigned int)g_core->get_LastRenderManager3DDrawCallCount());
        hel_log_u32("HEL core: heap allocations after first frame",
                    hel_heap_live_allocations());
    }
    g_frames++;
    return 0;
}

extern "C" unsigned int hel_core_frames(void) { return g_frames; }

extern "C" void hel_core_shutdown(void) {
    if (!g_core) {
        return;
    }
    hel_log_u32("HEL core: frames run", g_frames);
    hel_log_u32("HEL core: content opened", g_content ? g_content->opened : 0u);
    hel_log_u32("HEL core: content missing", g_content ? g_content->misses : 0u);
    hel_log_u32("HEL core: 2D draw calls", g_render2d ? g_render2d->draws2d : 0u);
    g_core->Dispose();
    g_core = nullptr;
    hel_log("HEL core: disposed");
    hel_log_u32("HEL core: heap allocations after dispose",
                hel_heap_live_allocations());
}
