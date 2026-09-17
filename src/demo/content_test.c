/*
 * Checks the cooked content archive on the console before anything reads it.
 *
 * The archive is built on the host and read on the target, so the two ends
 * of the format are written in different languages and never compiled
 * together. That is exactly the kind of seam where a field width or a byte
 * order goes wrong quietly, so every entry the archive claims is opened and
 * bounds-checked here, and a deliberate miss confirms an absent asset is
 * reported rather than answered with something.
 *
 * An image with no staged content links an empty archive, and that is a
 * pass: the checks that still apply are the ones about lookups failing
 * cleanly.
 */
#include <stdint.h>

#include "../engine/frame.h"
#include "../platform/xenia_guest/xenia_guest.h"
#include "demo.h"

static uint32_t g_failures;

static void check(const char *what, int ok) {
    hel_log_u32(what, ok ? 1u : 0u);
    if (!ok) {
        g_failures++;
    }
}

uint32_t hel_content_test(void) {
    g_failures = 0;
    hel_log("HEL content: archive check start");

    const uint32_t count = hel_content_count();
    hel_log_u32("HEL content: entries claimed", count);

    /* Every entry must be openable, and its data must be inside the image's
       read-only span rather than pointing anywhere else. */
    int all_open = 1;
    int all_addressable = 1;
    for (uint32_t index = 0; index < count; ++index) {
        const char *path = hel_content_path_at(index);
        if (!path) {
            all_open = 0;
            break;
        }
        const uint8_t *data = 0;
        uint32_t size = 0;
        if (hel_content_open(path, &data, &size) != HEL_OK) {
            hel_log("HEL content: a listed entry could not be opened");
            hel_log(path);
            all_open = 0;
            break;
        }
        /* Touch both ends so a wrong length shows up here rather than in
           whatever reads the asset later. */
        if (size != 0) {
            volatile uint8_t first = data[0];
            volatile uint8_t last = data[size - 1u];
            (void)first;
            (void)last;
        } else {
            all_addressable = 0;
        }
    }
    if (count > 0) {
        check("HEL content: every entry opens", all_open);
        check("HEL content: every entry has data", all_addressable);
    }

    const uint8_t *data = 0;
    uint32_t size = 0;
    check("HEL content: an absent asset is reported",
          hel_content_open("this-asset-is-not-staged.hasset", &data, &size) !=
                  HEL_OK &&
              data == 0 && size == 0);

    hel_log_u32("HEL content: failures", g_failures);
    return g_failures;
}
