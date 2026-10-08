/* One small textured draw keeps the diagnostic's incremental ring cost low. */
static HelTexture g_shader_gpu_probe_texture;
static uint32_t g_shader_gpu_probe_last_reported_frame;

static int hel_demo_shader_gpu_probe_overlay(void) {
    static const uint8_t pixels[16] = {
        0xFFu, 0x30u, 0xC0u, 0xFFu, 0x20u, 0xD0u, 0xE0u, 0xFFu,
        0xF0u, 0xC0u, 0x20u, 0xFFu, 0xF0u, 0xF0u, 0xF0u, 0x80u
    };
    static const HelVertex vertices[4] = {
        {-0.32f, -0.30f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
         1.0f, 1.0f, 1.0f, 1.0f},
        {-0.32f,  0.30f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
         1.0f, 1.0f, 1.0f, 1.0f},
        { 0.32f,  0.30f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
         1.0f, 1.0f, 1.0f, 1.0f},
        { 0.32f, -0.30f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f,
         1.0f, 1.0f, 1.0f, 1.0f}
    };
    static const uint16_t indices[6] = {0u, 1u, 2u, 0u, 2u, 3u};
    static const HelMat4 identity = {{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    }};
    static const HelDrawParams params = {{0.0f, 0.0f, 0.0f, 0.0f},
                                         {0.0f, 0.0f, 0.0f},
                                         {0.0f, 0.0f, 0.0f},
                                         {0.0f, 0.0f, 0.0f}};

    if (g_shader_gpu_probe_texture == 0u) {
        int err = hel_texture_create(pixels, 2u, 2u,
                                     &g_shader_gpu_probe_texture);
        if (err) {
            return err;
        }
    }

    hel_shader_gpu_probe_set_active(1);
    int err = hel_draw_ui_textured(vertices, 4u, indices, 6u, &identity,
                                   &params, g_shader_gpu_probe_texture);
    /* mesh.c also resets the override on every draw exit, including errors. */
    hel_shader_gpu_probe_set_active(0);
    return err;
}

static void hel_demo_shader_gpu_probe_after_present(void) {
    const uint32_t presented = hel_frame_count();
    if (presented != 0u && presented % 60u == 0u &&
        presented != g_shader_gpu_probe_last_reported_frame) {
        g_shader_gpu_probe_last_reported_frame = presented;
        hel_log_u32("HEL shader probe: presented frames", presented);
        if (presented == 600u) {
            hel_log("HEL shader probe: 600 presented frame marker");
        }
    }
}
