#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=${1:-/tmp/helengine/x360}
case "$out" in /tmp/helengine/x360|/tmp/helengine/x360/*) ;; *) echo 'Use /tmp/helengine/x360 for test outputs.' >&2; exit 1;; esac
mkdir -p "$out"
export TMPDIR="$out"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -c "$repo/src/platform/xenia_guest/atomic64.c" -o "$out/atomic64-host.o"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror -pthread "$repo/tests/atomic64_tests.cpp" "$out/atomic64-host.o" -o "$out/atomic64-tests"
"$out/atomic64-tests"
