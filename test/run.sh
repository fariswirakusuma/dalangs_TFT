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

echo "==> test_wasm_abi"
WASM3_DIR="$ROOT/third_party/wasm3/source"
WASM3_SRCS=(
    "$WASM3_DIR/m3_bind.c"
    "$WASM3_DIR/m3_code.c"
    "$WASM3_DIR/m3_compile.c"
    "$WASM3_DIR/m3_core.c"
    "$WASM3_DIR/m3_emit.c"
    "$WASM3_DIR/m3_env.c"
    "$WASM3_DIR/m3_exec.c"
    "$WASM3_DIR/m3_function.c"
    "$WASM3_DIR/m3_info.c"
    "$WASM3_DIR/m3_module.c"
    "$WASM3_DIR/m3_parse.c"
)
"$CC" -std=c17 -O1 -Wall -Wextra -fno-strict-aliasing \
    -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare \
    -I "$ROOT/main/include" -I "$WASM3_DIR" \
    "$ROOT/test/test_wasm_abi.c" "${WASM3_SRCS[@]}" -lm -o "$OUT/test_wasm_abi"
DALANGS_WASM_DIR="$ROOT/main/wasm" "$OUT/test_wasm_abi"

python3 "$ROOT/test/check_wasm.py" "$ROOT/main/wasm"

echo "All host tests passed."
