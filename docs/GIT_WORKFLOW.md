# Git Workflow

## Branches

```text
main
 └── feat/camera-tuner
 └── fix/touch-axis
 └── docs/wasm-abi
```

- `main` always builds and is protected.
- Feature branches are short-lived and rebased on `main` before review.
- One logical change per PR. Split driver work, UI work, and docs when they can land independently.
- Hotfixes use `fix/` and still go through a PR unless the board is bricked.

## Commit format

```text
<type>(<scope>): <subject>
```

Scopes in this repo: `board`, `camera`, `touch`, `lvgl`, `wasm`, `ui`, `build`, `ci`, `docs`.

Examples:

```text
feat(wasm): add declarative slider host import
fix(lcd): set BGR order for the 2.8 inch panel
chore(ci): cache managed components
```

## Pull request flow

1. Branch from `main`.
2. Commit in small, buildable steps.
3. Run `make test` and `make build`.
4. Open the PR with the template. Describe hardware used.
5. CI must pass: host tests, firmware build, docs lint.
6. Squash merge. Delete the branch.

## Submodules

- `third_party/wasm3` is pinned by commit.
- Never vendor edits inside the submodule; patch through a wrapper in `components/wasm3`.
- CI uses `submodules: recursive`.
- Release tags must point to a commit with the submodule recorded correctly.

## Releases

- Tag `vMAJOR.MINOR.PATCH` from `main`.
- Attach `build/dalangs_tft.bin`, `bootloader.bin`, `partition-table.bin`, and the applet `.wasm` files.
- Release notes list hardware assumptions and known limitations.

## What not to commit

- `sdkconfig` (commit `sdkconfig.defaults` only).
- `build/`, `managed_components/`, test binaries.
- Credentials, Wi-Fi passwords, tokens.
- Generated `.wasm` files are the exception: they are build inputs for the firmware and are committed on purpose.
