#!/bin/bash
set -euo pipefail
export TMPDIR=/work/build/menu-active
gcc -std=c11 -O1 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -Wl,--gc-sections -o build/menu-active/texture_retirement_tests tests/texture_retirement_tests.c src/platform/xenia_guest/texture.c src/platform/xenia_guest/x360_native_texture.c src/platform/xenia_guest/mesh.c
build/menu-active/texture_retirement_tests
