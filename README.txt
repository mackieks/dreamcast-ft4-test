Dreamcast FT4 microphone packet tests

Minimal offline Maple command exerciser. Plug in a controller and stock mic
or MaplePad. Up/Down chooses a test, Left/Right switches 8/11 kHz, A runs,
B cancels a running sampling test, Start exits. No packet readout or audio
playback: capture requests and replies with the logic analyzer.

New capture session: use a stock controller + mic. The menu opens on test 13
at 11 kHz. Run test 13, then test 14; switch to 8 kHz and run 13 then 14 again.
Keep one LA capture running through all four tests. Speak or tap the mic during
each test so sample contents are distinguishable from silence. Each takes a
little over two seconds NTSC (2.4 seconds PAL). No additional Test_Mode or
overflow tests are needed for this session. Up/Down automatically pages the
menu; the original twelve tests remain on page 1.

Each test starts with Device Reset followed by Device Request. Sampling tests
send AMP_Control, configure the selected option, start, issue 120 sample reads,
stop, then request an empty/stopped sample response. Each queued request uses
the next Maple display-frame dispatch. Control commands add frames; no extra
wait is inserted between sample reads. Retail command probes always use 8 kHz.
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
  13 PCM / mu-law transitions:
     EXTU zero, gain 0F. Start PCM; 20 reads; request mu-law with sampling still
     on; 20 reads; request PCM with sampling still on; 20 reads; stop + read.
     Start mu-law while stopped; 20 reads; request PCM while still on; 20 reads;
     request mu-law while still on; 20 reads; stop + read. Both sessions use
     the selected rate; no stop/reset separates phases within a session.
  14 EXTU while sampling:
     EXTU zero, gain 0F, start PCM; 30 reads; EXTU copy; 30 reads; EXTU binary 10;
     30 reads; EXTU zero; 30 reads; stop + read. Only one start is sent.

Tests 13/14 deliberately attempt changes while sampling. SIP section 2.7.2
says rate/quantization changes require stopping first; these traces establish
the ASIC's actual replies, status bits and subsequent sampling behavior.
The PCM/mu-law test starts independently in both formats so an ignored or
rejected active change cannot prevent coverage of the reverse direction.
Each control is immediately followed by sample reads. Inserting a control
can cause buffer overflow at the high rate; capture SBFOV as well as the
format/EXTU status and count. Stop uses Basic_Control DT1=00, as KOS does,
without assuming that the preceding active format change was accepted.
"Done" means the sequence completed, not that every command was ACKed.

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
