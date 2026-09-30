# Dreamcast FT4 microphone packet tests

Minimal offline Maple command exerciser for a stock controller and microphone,
or MaplePad. Capture requests and replies with a logic analyzer.

**Controls:** Up/Down selects a test, Left/Right selects 8/11 kHz, A runs,
B cancels, Start exits. The screen shows only the test menu and completion status.

Tests cover identity/AllInfo, linear PCM with all three EXTU modes, 8-bit mu-law,
Volume_Mode, Test_Mode, Planet Ring and Alien Front command probes, and overflow.
See [README.txt](README.txt) for exact sequences and protocol references.
The retail probes use observed command families with documented parameters;
they are not complete packet/timing replays.

## Download a build

Open **Actions**, select a successful **Dreamcast FT4 packet tests** run, and
download the `dreamcast-ft4-test` artifact. It contains a bootable `ft4-test.cdi`,
an ELF for dcload, a binary, usage notes, checksums, and the source commit.
No local KOS installation is needed. Hardware validation is pending.

## Build and checks

CI first checks packet construction and cleanup using a host C compiler, then
builds and packages with KOS in the versioned container used by
[maishuji/dreamcast-template](https://github.com/maishuji/dreamcast-template).
This fork retains that template's MIT license and history. The FT4 app uses the
root Makefile and C sources; the inherited CMake/raylib example is not built.

With an existing KOS environment: `make`. Host checks:

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic ft4_tests.c test_packets.c -o test-packets
./test-packets
```
