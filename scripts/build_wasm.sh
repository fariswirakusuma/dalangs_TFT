#!/usr/bin/env bash
# Build freestanding wasm32 UI applets. No WASI, no libc.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WASM_DIR="$ROOT/main/wasm"
CLANG="${CLANG:-clang}"
WASM_LD="${WASM_LD:-}"

if ! "$CLANG" --print-targets 2>/dev/null | grep -q "wasm32"; then
    echo "error: $CLANG does not support the wasm32 target" >&2
    exit 1
fi

if [[ -z "$WASM_LD" ]]; then
    WASM_LD="$(command -v wasm-ld || true)"
fi
if [[ -z "$WASM_LD" ]]; then
    WASM_LD="$(find "$HOME/.espressif/tools/esp-clang" -path '*/esp-clang/bin/wasm-ld' -print -quit 2>/dev/null || true)"
fi
if [[ -z "$WASM_LD" || ! -x "$WASM_LD" ]]; then
    echo "error: wasm-ld not found. Install lld or set WASM_LD=/path/to/wasm-ld" >&2
    exit 1
fi

build_one() {
    local src="$1"
    local out="${2:-${src%.c}.wasm}"
    echo "==> $(basename "$out")"
    "$CLANG" --target=wasm32 \
        -std=c11 -O2 -nostdlib -fno-builtin \
        -ffunction-sections -fdata-sections \
        -fuse-ld="$WASM_LD" \
        -Wl,--no-entry \
        -Wl,--export-memory \
        -Wl,-z,stack-size=16384 \
        -Wl,--initial-memory=65536 \
        -Wl,--max-memory=131072 \
        -Wl,--allow-undefined \
        -Wl,--gc-sections \
        -Wl,--strip-all \
        -o "$out" "$src"
}

if [[ $# -ge 1 ]]; then
    build_one "$1" "${2:-}"
    exit 0
fi

for src in "$WASM_DIR"/*.c; do
    build_one "$src"
done

echo "Wasm applets built in $WASM_DIR"
