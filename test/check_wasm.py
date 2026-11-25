#!/usr/bin/env python3
"""Validate committed wasm applets without a Wasm runtime.

Checks exports, imports, memory export, and size budget. This catches stale or
accidentally WASI-linked binaries before they are embedded into the firmware.
"""

import sys
from pathlib import Path

EXPECTED = {
    "basic_sliders.wasm": {"app_init", "app_event"},
    "fx_buttons.wasm": {"app_init", "app_event"},
    "live_cam.wasm": {"app_init", "app_tick"},
    "draw_pad.wasm": {"app_init", "app_touch"},
    "bouncing_ball.wasm": {"app_init", "app_tick"},
}

ALLOWED_IMPORTS = {
    "log",
    "ui_slider",
    "ui_button",
    "set_cam_param",
    "reset_camera",
    "canvas_clear",
    "canvas_rect",
    "canvas_line",
    "canvas_flush",
}

MAX_BYTES = 64 * 1024


def uleb(data, pos):
    result = 0
    shift = 0
    while True:
        byte = data[pos]
        pos += 1
        result |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return result, pos
        shift += 7


def name(data, pos):
    length, pos = uleb(data, pos)
    return data[pos : pos + length].decode("utf-8"), pos + length


def parse(path):
    data = path.read_bytes()
    assert data[:4] == b"\0asm", f"{path}: missing wasm magic"

    imports = []
    exports = []
    pos = 8
    while pos < len(data):
        section = data[pos]
        pos += 1
        size, pos = uleb(data, pos)
        end = pos + size

        if section == 2:
            count, cur = uleb(data, pos)
            for _ in range(count):
                module, cur = name(data, cur)
                field, cur = name(data, cur)
                kind = data[cur]
                cur += 1
                if kind == 0:
                    _, cur = uleb(data, cur)
                elif kind == 1:
                    cur += 1
                    flags = data[cur]
                    cur += 1
                    _, cur = uleb(data, cur)
                    if flags & 1:
                        _, cur = uleb(data, cur)
                elif kind == 2:
                    flags = data[cur]
                    cur += 1
                    _, cur = uleb(data, cur)
                    if flags & 1:
                        _, cur = uleb(data, cur)
                imports.append((module, field))
        elif section == 7:
            count, cur = uleb(data, pos)
            for _ in range(count):
                field, cur = name(data, cur)
                cur += 1
                _, cur = uleb(data, cur)
                exports.append(field)
        pos = end

    return data, imports, exports


def main():
    wasm_dir = Path(sys.argv[1] if len(sys.argv) > 1 else "main/wasm")
    failures = []

    for filename, required in EXPECTED.items():
        path = wasm_dir / filename
        if not path.exists():
            failures.append(f"{filename}: missing")
            continue

        data, imports, exports = parse(path)
        if len(data) > MAX_BYTES:
            failures.append(f"{filename}: {len(data)} bytes exceeds {MAX_BYTES}")
        if "memory" not in exports:
            failures.append(f"{filename}: memory is not exported")
        missing = required - set(exports)
        if missing:
            failures.append(f"{filename}: missing exports {sorted(missing)}")

        for module, field in imports:
            if module != "env" or field not in ALLOWED_IMPORTS:
                failures.append(f"{filename}: unexpected import {module}.{field}")

        print(f"{filename}: {len(data)} bytes, imports={len(imports)}, exports={sorted(exports)}")

    if failures:
        print("\nWasm validation failed:", file=sys.stderr)
        for item in failures:
            print(f"  - {item}", file=sys.stderr)
        return 1

    print("Wasm validation passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
