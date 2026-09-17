/* Demo modes selected at build time with -DHEL_DEMO_MODE=<n>. */
#ifndef HEL_DEMO_H
#define HEL_DEMO_H

#include <stdint.h>

#define HEL_DEMO_MODE_CLEAR 0
#define HEL_DEMO_MODE_TRIANGLE 1
#define HEL_DEMO_MODE_CUBE 2

int hel_demo_triangle_init(void);
int hel_demo_triangle_frame(uint32_t frame, void *user);
int hel_demo_cube_init(void);
int hel_demo_cube_frame(uint32_t frame, void *user);

/* Transpiled engine core (src/demo/core_boot.cpp). hel_core_boot constructs
   and initializes the generated Core; hel_core_frame runs one managed frame.
   Both return a HEL_ERR_* code, zero on success. */
int hel_core_boot(void);
/* The managed frame is split: the update runs before the backend's frame
   begins, the draw between hel_frame_begin and hel_frame_present so its
   geometry survives the clear. */
int hel_core_update(double elapsed_seconds);
int hel_core_draw(void);
uint32_t hel_core_frames(void);
void hel_core_shutdown(void);

/* Cooked content archive check (src/demo/content_test.c). */
uint32_t hel_content_test(void);

/* Binary serialization check for this big-endian target
   (src/demo/endian_test.cpp): returns the number of failed checks. */
uint32_t hel_endian_test(void);

/* Engine/backend matrix convention check (src/demo/matrix_test.cpp). */
uint32_t hel_matrix_test(void);

/* C++ environment self-test (src/demo/cxx_selftest.cpp): returns the number
   of failed checks, zero when the guest's C++ runtime is sound. */
uint32_t hel_cxx_selftest(void);

#endif
