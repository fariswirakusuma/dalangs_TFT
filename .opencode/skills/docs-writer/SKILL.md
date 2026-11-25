---
name: docs-writer
description: Write and review dalangs_TFT documentation in English. Use for README updates, hardware wiring docs, wasm ABI docs, testing notes, changelogs, and code comments.
---

# Documentation Writer

## Rules

- All documentation and code comments are **English**.
- Keep the reader's path short: what it is, what it needs, how to run it, how to verify it.
- Every command must be copy-pasteable from the repo root.
- Document the board revision and pin assumptions; never imply a wiring detail is universal.
- State limitations explicitly, for example "XPT2046 needs calibration" or "ST7789 vs ILI9341 must match the panel".
- No secrets, tokens, Wi-Fi credentials, or personal paths in docs or examples.
- Prefer tables for pins and ABI signatures.

## Structure

- `README.md`: overview, hardware, quick start, structure, status.
- `docs/HARDWARE.md`: pin map, power, calibration, known conflicts.
- `docs/ARCHITECTURE.md`: tasks, data flow, memory budget.
- `docs/WASM_ABI.md`: imports, exports, signatures, example applet.
- `docs/TESTING.md`: host tests and hardware acceptance checks.
- `docs/GIT_WORKFLOW.md`: branches, commits, PR rules.
- `.opencode/skills/*/SKILL.md`: agent-facing rules, not user manuals.

## Comment style

Comment the *why*, not the *what*.

```c
/* The XPT2046 shares the LCD SPI bus, so its CS must stay deasserted during flushes. */
```

Avoid commented-out code. Keep short `shortcut:` notes only when a real limit exists, with the upgrade condition.

## Review

- Run the docs lint: `npx --yes markdownlint-cli2 "**/*.md" "#third_party/**"`.
- Check every internal link.
- Re-read the quick start on a clean checkout assumption.
