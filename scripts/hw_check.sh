#!/usr/bin/env bash
# Flash a firmware image and watch the board come up. Fails on anything fatal.
#
# Usage: PORT=/dev/ttyACM0 SECS=180 ./scripts/hw_check.sh [image]
#   image  factory image to flash (default build/dalangs_tft-factory.bin)
#   SECS   how long to watch the serial output (needs >= 120 for two leak samples)
#
# Requires esptool on PATH (ESP-IDF export) and read access to the port.
# Serial capture is plain shell: stty raw plus a timeout, no interpreter in the
# loop, so the watcher adds no latency.
set -euo pipefail

PORT="${PORT:-/dev/ttyACM0}"
SECS="${SECS:-180}"
IMAGE="${1:-build/dalangs_tft-factory.bin}"
LOG="${LOG:-/tmp/dalangs_hw.log}"
CLEAN="$(mktemp)"

cleanup() { rm -f "$CLEAN"; }
trap cleanup EXIT

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

[ -f "$IMAGE" ] || fail "missing image $IMAGE"
command -v esptool >/dev/null || fail "esptool not on PATH (source the ESP-IDF export)"

echo "==> flashing $IMAGE on $PORT"
esptool --chip esp32s3 -p "$PORT" -b 460800 write_flash 0x0 "$IMAGE"

# This board enumerates as USB-Serial/JTAG: the device node disappears on reset
# and comes back a moment later.
for _ in $(seq 1 50); do
    [ -e "$PORT" ] && break
    sleep 0.2
done
[ -e "$PORT" ] || fail "$PORT did not come back after reset"

echo "==> watching $PORT for ${SECS}s"
stty -F "$PORT" 115200 raw -echo
timeout "$SECS" cat "$PORT" >"$LOG" 2>/dev/null || true

# Firmware logs may carry colour escapes; strip them so field parsing is stable.
sed -E 's/\x1b\[[0-9;]*[mK]//g' "$LOG" >"$CLEAN"

FATAL='assert|Guru Meditation|CORRUPT|share GPIO|reserved by the module'
if grep -Eq "$FATAL" "$CLEAN"; then
    echo "--- fatal lines:" >&2
    grep -En "$FATAL" "$CLEAN" | head -5 >&2
    fail "fatal line in the boot log"
fi

grep -q 'MAIN: ready:' "$CLEAN" || fail "no 'MAIN: ready:' line: init did not finish"
grep -q 'pin audit: .* no conflicts' "$CLEAN" || fail "pin audit did not run"
grep -q 'CAMERA: first frame' "$CLEAN" || fail "camera never produced a frame"

# Frame counter must keep growing: a frozen pipeline is as bad as a dead one.
mapfile -t frames < <(sed -n 's/.*CAMERA: frames: \([0-9]*\).*/\1/p' "$CLEAN")
[ "${#frames[@]}" -ge 2 ] || fail "need two frame-counter samples, raise SECS"
[ "${frames[-1]}" -gt "${frames[0]}" ] || fail "frame counter stalled at ${frames[0]}"

# Two leak samples at least, and the last one must not be below the first.
mapfile -t leak < <(sed -n 's/.*leak watch: internal \([0-9]*\) KB free, PSRAM \([0-9]*\) KB.*/\1 \2/p' "$CLEAN")
[ "${#leak[@]}" -ge 2 ] || fail "need two leak-watch samples, raise SECS to 120 or more"

first=(${leak[0]})
last=(${leak[-1]})
[ "${last[0]}" -ge $((first[0] - 4)) ] || fail "internal SRAM fell ${first[0]} -> ${last[0]} KB"
[ "${last[1]}" -ge $((first[1] - 4)) ] || fail "PSRAM fell ${first[1]} -> ${last[1]} KB"

echo "OK: booted, no fatal lines, memory flat (SRAM ${first[0]} -> ${last[0]} KB, PSRAM ${first[1]} -> ${last[1]} KB)"
