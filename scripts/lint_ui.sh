#!/usr/bin/env bash
# UI guardrails: keep pins in pins.h and keep LVGL object creation in the host.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

fail=0

if grep -RInE 'GPIO_NUM_[0-9]+' main/src main/main.c 2>/dev/null; then
    echo "error: GPIO literal outside pins.h" >&2
    fail=1
fi

if grep -RInE 'lv_(obj|btn|slider|label|canvas)_create' main/wasm/*.c 2>/dev/null; then
    echo "error: wasm applet creates an LVGL object; use the host ABI" >&2
    fail=1
fi

python3 test/check_wasm.py main/wasm

if [[ $fail -ne 0 ]]; then
    exit 1
fi

echo "UI lint passed."
