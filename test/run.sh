#!/usr/bin/env bash
# Host-only unit tests. No ESP-IDF or board required.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CC="${CC:-cc}"
OUT="${TMPDIR:-/tmp}/dalangs_tft_tests"
CFLAGS=(-std=c17 -Wall -Wextra -Werror -I "$ROOT/main/include")

mkdir -p "$OUT"

run_test() {
    local name="$1"
    shift
    echo "==> $name"
    "$CC" "${CFLAGS[@]}" "$@" -o "$OUT/$name"
    "$OUT/$name"
}

run_test test_cam_params \
    "$ROOT/test/test_cam_params.c" \
    "$ROOT/main/src/cam_params.c"

run_test test_ui_model \
    "$ROOT/test/test_ui_model.c" \
    "$ROOT/main/src/ui_model.c"

echo "All host tests passed."
