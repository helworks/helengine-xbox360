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

#endif
