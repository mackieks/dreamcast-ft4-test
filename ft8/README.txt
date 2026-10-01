Dreamcast FT8 Tremor Pack characterization app
=============================================

Burn ft8-test.cdi or load ft8-test.elf with your usual Dreamcast loader.
Attach a controller and the reference rumble pack. The first FT8 peripheral
found on any port is selected when you run a test or enter an editor profile.
Capture Maple from boot to preserve KOS's ordinary enumeration traffic too.
No packet dump is displayed; use your logic analyzer for replies.

Suite controls
--------------
Up/Down: select a test. Left/Right: change page (two pages).
A: run selected test. X: run every test in order, INCLUDING Kill last.
B while running: cancel, send zero-power stop, restore source-1 AST to 13h.
Y: open the separate command editor. Start: exit.

For the first baseline, capture one X run from cold boot, then unplug/replug
the rumble pack. Kill may leave a real pack unresponsive until power-cycled.
The suite takes roughly 90 seconds plus transfer and probe time. Test numbers
and names appear while running. Error replies and missing replies are counted
as observations, not reasons to abandon the remaining probes. Done means the
sequence ran; it does not mean the reference device passed an expected spec.

Test order
----------
01 Cold DeviceInfo, AllInfo, every reported source's MediaInfo, condition, AST.
02 Reset/defaults, direct start without media query, reset during vibration,
   enumeration/media query then start again.
03 Positive levels 0-7, continuous 10Hz, 750ms each.
04 Negative levels 0-7, continuous 10Hz, 750ms each.
05 Continuous frequency sweep: 4,10,20,30,0.5,128Hz, 1s each.
06 One-shot then continuous; wait beyond default 5s AST.
07 Positive convergence/divergence, Inc=1,2,4, one-shot, 10Hz.
08 Negative convergence/divergence, Inc=1,2,4, one-shot, 10Hz.
09 Continuous positive/negative convergence/divergence, Inc=1, 2.2s each.
10 Replace after 50ms; replace mid-ramp; zero-power stops with Freq/Inc=00
   and FF. These otherwise invalid stop parameters reach the pack unchanged.
11 AST write/read: 00,01,03,13,FF (250ms,500ms,1s,5s,64s).
12 Continuous AST expiry at 250ms,1s,5s, polling every ~100ms.
13 AST=500ms, identical Set_Condition every ~300ms six times, then stop sending.
14 AST=500ms, Get_Condition polls for 1.8s without rewriting the effect.
15 Running 1s AST changed to 250ms after ~200ms; next effect uses 250ms.
16 ASR masks 0000,0002,0004,0006,8002,FFFE and ordered AST bytes/readback.
17 Invalid/edge CTRL/POW/Freq/Inc combinations, including both peaks, both
   ramps, zero ramp Inc, nonzero ordinary Inc, Inc greater than Freq, reserved
   CTRL bits, VN0/VN15, frequencies just outside OEM's 4-30Hz range. VN15
   may be valid on another device; interpret alongside reported capabilities.
18 Wrong function, missing/extra words, VN/Phase/Block errors and AST writes.
   ASR=0006 with one explicit AST also transmits a zero padding byte, which
   can be interpreted as the second AST; it is a tolerance probe, not proof
   of a physically truncated byte field.
19 Waveform VN1 read/write even when OWF=0. Twenty 10ms steps, one-shot then
   first-step CNT; last-step WFE. Observe unsupported replies rather than
   assuming a specific code (FT8/maxi prose disagree).
20 Get_Condition, host Transmit_Again FC, unsupported 0D and unknown 7F.
21 Start, Kill, Device_Request probe. NO cleanup writes after Kill.

Except cold identity, each test begins with Reset and a 100ms settling wait.
Every normal completion/cancel sends stop then restores source-1 AST=13h.
There is no mandatory media-query choreography. Timing uses milliseconds,
not a fixed video-frame count. Physical intervals include intervening Maple
transfer latency, so use LA timestamps as the measured time reference.

Editor controls
---------------
Choose Set_Condition, AST, waveform, MediaInfo or raw packet; A opens it.
Up/Down: field. Left/Right: bit (7 on left, 0 on right). A: toggle bit.
Hold Y and tap Left/Right: subtract/add one to the selected byte.
X: send. Tap/release Y: read corresponding condition/block/media state.
B: stop repeat sending and send a zero-power stop; stay in editor.
Start: return to profile picker; stop vibration and restore AST=13h.
B or Start in picker: return to suite.

Field 00 is command, 01 is payload word count (0-8), 02 is repeat interval
in 50ms units (zero=one send). Remaining fields are payload bytes in KOS
HOST order. The function word is editable too. Growing length exposes more
fixed zero-initialized storage; shortening it does not discard edited bytes.
Up/Down wraps so you can quickly reach command/length/repeat from any field.

For repeated effects: set repeat to e.g. 02=100ms, then X. The screen clearly
shows ACTIVE. The exact packet and interval are snapshotted on X; subsequent
edits take effect only when X is pressed again. B or leaving stops repeats.
Y reads do not refresh AST. No overdue burst of repeated commands is issued.
Repeat=00 takes effect on the next X or stop; simply editing it does not alter
an already active snapshot. Protocol-invalid combinations are not sanitized.

CTRL labels: VN3 VN2 VN1 VN0 reserved reserved reserved CNT.
POW labels: INH positive2 positive1 positive0 EXH negative2 negative1 negative0.
Freq=(byte+1)/2 Hz. Inc=cycles per strength step, not number of repeats.
AST=(byte+1)*250ms. ASR is big endian in the host byte stream; AST bytes are
ordered by selected source number. Waveform labels: CNT WFE R R DIR P2 P1 P0.
An added source's AST can occupy former padding; extend length for more data.

Wire order examples
-------------------
CTRL=11 POW=70 Freq=27 Inc=00:
  KOS words: 00010000 00277011
  Wire payload: 00 01 00 00 00 27 70 11
ASR=0002 AST=13 dummy=00, address VN/Phase/Block=0:
  KOS words: 00010000 00000000 00130200
  Wire payload: 00 01 00 00 00 00 00 00 00 13 02 00

Transport notes
---------------
Fixed application-owned frame and eight transmit words; no convenience rumble
API filtering. Raw responses include protocol errors. KOS ordinarily retries
FC automatically; the app retires that pending retry at an idle DMA boundary
and reports the observed FC. A scheduling race can permit an extra retry.
A 750ms timeout never unlocks or overwrites an in-flight DMA buffer. Cleanup
is best effort if the pack or bus is unresponsive. KOS controller polling and
autodetection remain in the capture.

Sources and validation
----------------------
Sega Ft8_080e: command formats, source capabilities, CTRL/POW/Freq/Inc,
ASR/AST and waveform fields. 09_Maxih080e: OEM operating ranges/defaults.
MAPLE82E: enumeration, variable All_Status, reset/kill and unknown commands.
The Tremor Pack's actual identity, support, feel and timing remain measurements.
The struck-out Freq>=Inc rule in Ft8_080e is not enforced.

CI builds FT4 and FT8 separately using the existing versioned KOS container,
checks the host packet/sequence/editor tests, and packages a CDI/ELF/bin with
source/KOS revisions and checksums. FT4 sources are unchanged.
