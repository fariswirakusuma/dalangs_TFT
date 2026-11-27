# Known issues

Everything here is measured on the reference board, not reproduced from a
datasheet. Each entry says what it means for a user and what would fix it.

## Camera preview is noisy in the dark, and can be off-colour

The OV5640 on this board does not run its own auto exposure and its auto white
balance never converges. Measured on the board:

| Symptom | Measurement |
| --- | --- |
| Preview far too dark with auto exposure on | R/G/B average 10-30 out of 255, no change when the exposure settings changed |
| Auto white balance leaves a cast | R/G/B spread 36-56 with AWB on, 3-9 with fixed daylight gains |
| Forcing maximum exposure and gain overexposes | R/G/B average 180 at maximum, so the sensor does receive light |

Both loops are therefore closed in software in `camera_tuner.c`: the gain
follows the measured luma, and the red and blue channels are scaled until they
match green. Consequences:

- A scene lit by one strong colour (the green LED in our test room) pulls skin
  tones away from neutral. Point the camera at a neutral subject to judge.
- High gain means visible chroma noise in dim light. That is the sensor, not the
  preview path.
- A very bright scene can still clip: the exposure is fixed and only the gain
  moves.

## Preview is softer than the sensor suggests

Captured at VGA and averaged down 2x2 in software, because the sensor's own QVGA
scaler measures far softer (mean neighbour difference 2-3 at QVGA against 19-36
with the downscale). The lens is fixed focus; close subjects stay soft.

## Panel must not be inverted

`CONFIG_DALANGS_LCD_INVERT_COLOR` has to stay **off** on this module. With
inversion on, black renders white and every screenshot looks washed out. If a
different module is fitted, verify with a solid black rectangle first.

## Touch needs a firm press

The XPT2046 is resistive: the pressure threshold is set to 60
(`CONFIG_XPT2046_Z_THRESHOLD`) so a finger tip registers. Lower values risk
phantom touches; a nail or stylus always works.

## CI does not test hardware

Continuous integration proves that the sources build, the host tests pass, the
image is well formed, and the size fits the partition. It cannot test the
camera, the touch panel, the display, temperature, or hours of runtime: there is
no board on the runner. Those stay manual, see [TESTING.md](TESTING.md), and are
the reason a release tag requires a green board checklist.
