/*
 * Handles onto the running core, shared between the boot host
 * (core_boot.cpp) and the scene it builds (core_scene.cpp).
 */
#pragma once

class Core;

namespace helengine::x360 {
class X360RenderManager3D;
}

/// <summary>The constructed core, or null before hel_core_boot runs.</summary>
::Core *hel_core_instance();

/// <summary>The renderer the core was initialized with, or null.</summary>
helengine::x360::X360RenderManager3D *hel_core_render3d();

extern "C" {
/// <summary>Creates the camera and cube scene. Returns a HEL_ERR_* code.</summary>
int hel_core_scene_build(void);

/// <summary>Advances the scene's own animation.</summary>
void hel_core_scene_update(float elapsed_seconds);

/// <summary>The scene's current spin angle, for the log.</summary>
unsigned int hel_core_scene_angle_milliradians(void);
}
