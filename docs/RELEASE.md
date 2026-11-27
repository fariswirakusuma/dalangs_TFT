# Release and rollback

This firmware has no server and no OTA. A release is a file you flash over USB,
so "rollback" means flashing an older file. The CI exists to make sure the file
you keep is worth flashing, and that the one you go back to still exists.

## Two gates, both required

| Gate | Who | Proves |
| --- | --- | --- |
| CI on the tag | GitHub Actions | The tagged commit builds, host tests pass, lint passes, the image parses, the size fits, the version matches the tag |
| Board checklist | you, on hardware | Camera, touch, display, temperature, and a runtime soak all behave |

CI cannot cover the second row: there is no board on the runner. A green CI run
on a tag means "safe to flash and test", not "proven in use". Do not tag before
the checklist in [TESTING.md](TESTING.md) is green.

## Tagging

`APP_VERSION` in `main/include/app_config.h` is the single source of truth. The
tag must equal it, and the release job enforces that.

```sh
# 1. board checklist green
# 2. bump APP_VERSION if needed, commit
git tag -a v0.1.0 -m "v0.1.0"
git push origin v0.1.0
```

Rules the release job checks:

- the tag matches `vMAJOR.MINOR.PATCH`;
- the tag's commit is an ancestor of `main`;
- the tag equals `APP_VERSION`;
- the same tests, lint and build that run on `main` pass on that commit.

## What a release contains

| File | Why |
| --- | --- |
| `dalangs_tft-factory.bin` | bootloader, partition table and app merged; flash at offset 0 |
| `SHA256SUMS` | verify the download before flashing |
| `dalangs_tft.map` | decode a crash address without rebuilding |
| `dalangs_tft.elf` | same, with symbols |

## Flashing a release

```sh
sha256sum -c SHA256SUMS
esptool --chip esp32s3 -p /dev/ttyACM0 write_flash 0x0 dalangs_tft-factory.bin
```

## Rolling back

Flash the previous release's factory image the same way. Nothing else changes:
there is no persistent state, no schema, no migration. `sdkconfig` is not
committed, so a rollback also restores the build defaults of that tag.

If the previous release is older than the artifact retention window, use the
GitHub release for that tag: tags keep their files permanently.

## Known limits

- No automatic on-board test yet; the hardware job described in
  [TESTING.md](TESTING.md) is planned, not implemented.
- A release proves the code, not the wiring. A change to `main/include/pins.h`
  needs the board checklist again even if CI is green.
