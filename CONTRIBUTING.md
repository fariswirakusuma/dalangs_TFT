# Contributing

## Requirements

- ESP-IDF **v6.1** installed and exported (`$IDF_PATH`).
- `cmake`, `ninja`, `make`, `git`.
- Optional for applet work: `clang` with the `wasm32` target and a `wasm-ld` linker.
- Serial access to the board (default `/dev/ttyACM0`).

## Clone

```sh
git clone --recurse-submodules git@github.com:fariswirakusuma/dalangs_TFT.git
cd dalangs_TFT
make setup
```

If the repository was cloned without submodules:

```sh
git submodule update --init --recursive
```

## Branch workflow

`main` is protected. Work on a short-lived branch:

| Prefix | Use |
| --- | --- |
| `feat/` | new behavior |
| `fix/` | bug fix |
| `docs/` | documentation only |
| `refactor/` | no behavior change |
| `test/` | tests only |
| `chore/` | build, CI, tooling |

Open a pull request into `main`. The PR template lists the required checks. Squash merge keeps history readable; the squash title must follow Conventional Commits.

## Commits

```text
feat(camera): apply sensor effect from wasm button
fix(touch): swap axes for landscape panel
docs(wasm): document canvas imports
```

Allowed types: `feat`, `fix`, `docs`, `refactor`, `test`, `chore`, `build`, `ci`.
Keep the subject under 72 characters. Use the body for *why*, not *what*.

## Submodules

`third_party/wasm3` is pinned to a release tag. Do not edit files inside it. To upgrade:

```sh
cd third_party/wasm3
git fetch --tags
git checkout vX.Y.Z
cd ../..
git add third_party/wasm3
git commit -m "chore(deps): bump wasm3 to vX.Y.Z"
```

CI checks out submodules recursively. A PR that forgets the submodule bump will fail the build.

## Dependencies

Prefer the ESP-IDF component manager (`main/idf_component.yml`). Add a dependency only when the standard library or an existing component cannot do the job. Document the reason in the PR.

## Code rules

- C17, 4-space indentation, braces on the same line.
- Formatting is clang-format, LLVM base, 4-space indent, 100 columns
  (`.clang-format`). Run `make format` before committing; `.clangd` points the
  language server at `build/compile_commands.json`.
- Bitmap fonts are generated, not hand-written. `main/src/fonts` holds the
  output of `lv_font_conv` (build-time only, never linked into the firmware as
  a tool). Regenerate with the command recorded in the file header when a size
  or character range changes.
- Public headers in `main/include`, private helpers `static`.
- `pins.h` is the only place with GPIO numbers.
- English comments and documentation.
- No dynamic allocation in LVGL callbacks or render loops.
- Handle allocation and init failures; the UI must stay alive without a camera.

## Checks before opening a PR

```sh
make test
make lint-ui
make build
make wasm    # only if an applet changed
```

For UI changes, attach a photo of the TFT. For wiring changes, update `docs/HARDWARE.md` and `main/include/pins.h` together.
