Dreamcast FT4 microphone packet tests

Minimal offline Maple command exerciser. Plug in a controller and stock mic
or MaplePad. Up/Down chooses a test, Left/Right switches 8/11 kHz, A runs,
B cancels a running sampling test, Start exits. No packet readout or audio
playback: capture requests and replies with the logic analyzer.

Each test starts with Device Reset followed by Device Request. Sampling tests
send AMP_Control, configure the selected option, start, read for 120 display
frames, stop, then request an empty/stopped sample response. That is about
2 seconds NTSC or 2.4 seconds PAL. Retail command probes always use 8 kHz.
Unknown-command replies are deliberately accepted so the remaining commands
are still sent. A missing response aborts the test. KOS's normal controller
polling/device enumeration continues; its mic sample polling stays inactive.

Test list:
  Device info / AllInfo: reset, enumerate, AllInfo.
  Linear PCM / EXTU: expansion 00, 01 (copy), or 02 (binary 10).
  8-bit mu-law: Basic_Control sample type 01, at the selected rate.
  Volume_Mode: subcommand 05 with DT1 00 (+30 dB) or 01 (+12 dB), then PCM.
  Test_Mode stopped: subcommand FC with DT1/DT2/DT3 all zero.
  Test_Mode sampling: FC during PCM sampling, with the same zero parameters.
  Planet Ring probe: gain 1E, 8 kHz PCM, FC during sampling.
  Alien Front probe: gain 1F, Volume_Mode 00, 8 kHz PCM, FC during sampling.
  Overflow: start PCM, wait eight frames without reading, then resume reads.

The retail probes reproduce observed command families, not exact game traces.
Their FC parameters use documented zeros because Flycast's current log does
not record those parameters. Timing and repeated gain commands are simplified.

Protocol references:
  Ft4_100e.pdf (2000-03-15), primary FT4 protocol reference.
  03_SIP102e.pdf, stock mic rates, buffer, identity, Test_Mode register reply.
  Camera_Sip041e.pdf (2000-03-27), Volume_Mode opcode erratum (04 -> 05).

Ft4_100e section 2.2 explicitly says a mic whose function definition is 0F
returns Command Unknown for Volume_Mode. Test_Mode's documented response is
Data Transfer, despite Flycast naming this subcommand MDRE_TransmitAgain.
Capture both behaviors before deciding whether MaplePad needs changes.

No KOS installation is needed on the user's computer: the GitHub Actions
workflow will build the ELF, binary, and bootable disc image remotely.
