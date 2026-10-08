FT0 joystick viewer
===================

Boot ft0-test.cdi, or load ft0-test.elf with dcload. Connect a controller to
any port. The first controller found by KOS is used; its port is shown.

The main stick is plotted across the full raw Maple range 00..FF on both
axes, with 80 neutral. Left/up is 00; right/down is FF. No extra deadzone,
calibration, smoothing or gain is applied. KOS's signed coordinates are
converted back to the original bytes by adding 128.

The square is 448x448 pixels within a standard 640x480 display, leaving room
for readouts. All 256 coordinates have distinct positions. A white 2x2 dot
shows the live position. Grid lines are spaced by 20h; the brighter cross
marks 80h. The border encloses the complete range, including FFh.

Up to 512 motion samples remain visible. A sample is added only when X or Y
changes from the preceding observed position. The oldest sample is replaced
when full. Returning to a previously visited position is a new sample.
Holding still never ages or discards the trail. Newer overlapping dots are
drawn over older ones, with the live white dot on top.

Trail hue varies continuously from green through yellow to red by Euclidean
distance from the previous position. Zero distance is green, 16 raw units is
yellow, and 32 or more is red. RGB565 introduces only its normal color-depth
quantization; there are no speed buckets. FT0_RED_DISTANCE in history.h sets
the red endpoint. This is distance per observed sample, not physical velocity
or a time-normalized measurement. The app reads KOS controller state once per
display frame; movements between observations are not interpolated.

X: clear the trail and recorded min/max readouts immediately
Start: exit

Clear is edge-triggered: holding X does not repeatedly clear. The current
white dot and live hex readouts remain visible. Min/max stay blank and the
trail stays empty until the stick next moves. Min/max cover all movement
since clearing, including samples no longer in the FIFO.

Unplugging hides the live dot and hex values while retaining the old trail.
Reconnection or switching to another controller starts a fresh measurement.
Real ADC jitter counts as movement; this viewer deliberately does not hide it.

Build
-----
With KOS configured, run make ft0-test.elf ft0-test.bin from the repo root.
GitHub Actions also packages a bootable CDI in the dreamcast-ft0-test artifact.
The existing FT4 and FT8 apps remain separate bootable artifacts.

Host check:
cc -std=c11 -Wall -Wextra -Werror -pedantic ft0/history.c ft0/test_history.c -lm -o test-ft0-history
./test-ft0-history

Hardware checks still required: live input/axes, X hold/release, reconnect,
edge coordinates, interlaced display readability and long FIFO sweeps.
