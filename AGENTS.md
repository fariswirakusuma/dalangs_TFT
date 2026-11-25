# Agent Guide

This repository is an ESP-IDF firmware project with a small Wasm applet runtime.

## Read first

- `main/include/pins.h` — the single source of truth for GPIOs.
- `docs/ARCHITECTURE.md` — task and module boundaries.
- `docs/WASM_ABI.md` — host/applet contract.

## Skills

Load the matching skill before changing an area:

| Area | Skill |
| --- | --- |
| C, CMake, drivers, build | `firmware-dev` |
| LVGL layout and visual review | `lvgl-designer` |
| README and docs | `docs-writer` |
| Wasm applets and ABI | `wasm-applet` |
| Wiring and on-board validation | `hardware-test` |

## Non-negotiables

- English comments and documentation.
- `make test` and `make build` stay green.
- No pin literals outside `pins.h`.
- No new dependency without a note in `CONTRIBUTING.md`.
- Never commit `sdkconfig`, build output, or credentials.
