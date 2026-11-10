# FTX for Linux — project context

Instructions and context for Claude Code working in this repo. Richard's global
`~/.claude/CLAUDE.md` rules apply (Czech, consent before major/irreversible or
outward actions, trash instead of rm, verify before claiming, one question at a
time).

## What this is

A native **GTK4/libadwaita FT8/FT4 program** for Linux, the digital-modes member
of the OK1BR family (sdr-for-linux, skimmer-for-linux, log-for-linux,
icom-for-linux). Radio link is **TCI only** (sdr-for-linux and icom-for-linux
are TCI servers). It replaces Decodium 4 on Richard's desk.

Names (confirmed by Richard 2026-10-10): repo `OK1BR/ftx-for-linux`, display
name **"FTX for Linux"**, app id `cz.ok1br.ftx_for_linux`, binary and config
dir `ftx-for-linux`. License **GPL-3.0-or-later** (a derivative of WSJT-X).

## Founding decisions (Richard, 2026-10-10)

- **Pure C.** No Fortran in the product. The WSJT-X FT8/FT4 decoders are
  **ported by hand** from Fortran 90 to C (no tool exists: f2c is F77 only).
- **WSJT-X is the source of truth**, in two roles, both dev/test only:
  1. *Port reference*: a clone pinned to tag **v3.0.2** at
     `~/.local/opt/wsjtx` (read, never build into the product).
  2. *Oracle*: the AUR `wsjtx` 3.0.2 package (built from the same tag) ships
     `jt9`, a CLI WAV decoder. The gate runs the same WAV through `jt9` and
     our decoder and compares decode sets, freq, DT, SNR within tolerance.
     `jt9 -8 -d 3 -c CALL -G GRID [-x HISCALL -Q n] file.wav` = FT8,
     `-5` = FT4 (flags verified in `lib/jt9.f90` @ v3.0.2). Installing the
     AUR package is a system change → Richard's consent first.
- **Provenance done right**: every ported file carries a header naming the
  WSJT-X source file, the tag, and the original copyright — the ten holders
  named in WSJT-X `widgets/about.cpp` @ v3.0.2: Joe Taylor K1JT, Bill
  Somerville G4WJS, Steve Franke K9AN, Nico Palermo IV3NWV, Uwe Risse DG2YCB,
  Brian Moran N9ADG, Roger Rehr W3SZ, John Nelson G4KLA, Charlie Suckling
  DL3WDG, Terrell Deppe KJ5HST (no "et al."); About acknowledges WSJT-X. (The Decodium
  provenance dispute of 2026-02 is the counter-example.)
- **Decoder core = a GLib-free library**: C99 + FFTW3, stateless context
  struct (WSJT-X keeps module/`save` state → one decode per process; ours
  must allow parallel instances so skimmer-for-linux can reuse it later for
  whole-band decoding from the IQ stream). The app layer above it is
  GLib/GTK4/libadwaita.
- **Family template** from sdr-for-linux: meson, CI on ubuntu-26.04 +
  fedora:44, AppImage/.deb/.rpm, the About-dialog rule (version findable from
  the UI, debug_info), GitHub Issues + Discussions, `docs/` keeps only what
  has future value, UI mockups in `docs/mockups/` before GUI code.

## Phase 1 (Richard: "v první fázi FT8 a FT4 dekodéry funkční")

**Repository content rule (Richard, 2026-10-10): the public repo holds
finished code and libraries only.** Tests in any language, vector generators
(Fortran/C++ drivers) and vector files stay local in `dev/` (gitignored:
`dev/tests/`, `dev/tools/`); the root meson.build picks `dev/tests` up when
present, so `meson test -C build` is the local gate and never a repo artifact.
Commit and push after each finished module ("průběžně ukládat na GitHub").

Deliverable: CLI `ftx-decode` (WAV in, WSJT-X-style decode lines out) for
**FT8 and FT4**, depth 3 incl. a-priori (AP) decoding, multipass with signal
subtraction, and the **complete** 77-bit message unpacking (`packjt77`; the
ft8_lib-based OpenWSFZ lost every `RR73;`/contest message because it skipped
types). Plus a meson test gate: corpus = WSJT-X sample WAVs + 15 s slices
recorded from Richard's TCI audio (a small recorder tool comes first);
decode-set equality per period vs `jt9 -d 3`; integer-exact stages (packjt77,
CRC14, LDPC 174/91 BP + OSD) as bit-exact unit tests with vectors from the
Fortran. Later phases: encoder (verified by `jt9` decoding its output, then
encoder + noise = SNR ladder), live RX over TCI (12 kHz int16, UTC periods,
mean-DT clock health), GUI (slice waterfall, decode list, auto-sequencer,
standard messages), TX over TCI + logging via WSJT-X UDP (log-for-linux
already speaks it: schema 3, port 2237, QSO Logged + Highlight Callsign).
Parked: Fox/Hound, contest exchanges on TX, PSK Reporter, wideband IQ decode.

## Verified facts about the source (2026-10-10)

- WSJT-X moved to **https://github.com/WSJTX/wsjtx** (SourceForge abandoned);
  release 3.0.2 of 2026-06-18, GPL-3.0. Counts from master 967c85a
  (2026-09-23): `lib/ft8/` 45 files 4 861 lines, `lib/ft4/` 14 files 1 121,
  `lib/ft8_decode.f90` 330, `lib/ft4_decode.f90` 467, `lib/77bit/packjt77.f90`
  3 499. Dependency closure of FT8+FT4 decoder+encoder ≈ **9 000 lines F90**
  (own call-graph script, false matches removed) + small non-Fortran pieces:
  `crc14.cpp` (Boost header-only CRC), `four2a` FFTW wrapper, `shmem.cpp`
  (not needed). pavel-demin/ft8d = a 36-file minimal map of the FT8-only
  decoder (still Fortran, GPL-3, has a `crc14.c`).
- Decoder entry: module `ft8_decode`, type `ft8_decoder` with a procedure
  pointer callback; `decode(this,callback,iwave int16 15 s @ 12 kHz,
  nQSOProgress,nfqso,nftx,newdat,nutc,nfa,nfb,nzhsym,ndepth,emedelay,ncontest,
  nagain,lft8apon,ltry_a8,lapcqonly,napwid,mycall12,hiscall12,hisgrid,
  ldiskdat)`; callback `(this,sync,snr,dt,freq,decoded char37,nap,qual)`.
  FT4 analogous (`ft4_decode.f90`, 12 kHz input, NDOWN 18).
- WSJT-X also carries a second optional decoder `lib/ft8var` ("ft8md",
  53 files 12 686 lines, JTDX lineage, default OFF, `jt9 -M`). Not in scope;
  whatever we port is a chosen snapshot.
- Precedents of faithful ports: e04/ft8ts (TypeScript, 24 files 7 064 lines,
  FT8+FT4 from v3.0.1, parity claimed on one sample WAV), jl1nie/mfsk-core
  (Rust, stateless, refuses ft8md), MSHV (C++/Qt5, years; source zip
  unreachable 2026-10-10, SourceForge 403). Rejected bases: kgoba/ft8_lib
  (MIT C; OpenWSFZ issue #3 measured 69.7 % of WSJT-X's off-air decodes,
  synthetic S7 78.6 vs 97.7 %), Decodium 4 Core Shannon.

## Family plumbing already in place

- sdr-for-linux TCI server (:40001): RX audio 8/12/24/48 kHz, TX audio over
  TCI under the DIGU clean-chain rule (first FT8 QSOs ran Decodium → TCI →
  DIGU on 2026-07-10), trx/PTT, dds/vfo, IQ stream.
- skimmer-for-linux `src/engine/tci_client.[ch]`: GLib TCI client in C
  (IQ, vfo, tx callback, spots, tune) — the starting point for ours.
- log-for-linux `src/engine/wsjtx_udp.[ch]`: WSJT-X UDP server side.
- Richard's machine: systemd-timesyncd active and synchronized (FT8 needs
  < 1 s); sdr-for-linux at 192.168.1.18 talks to the ANAN G2E 192.168.1.247.

## Status

2026-10-10: project founded (decisions above); public repo
https://github.com/OK1BR/ftx-for-linux created with Richard's consent
(homepage rifak.cz per the family rule; description set 2026-10-10 with
Richard's "ano": "Native GTK4/libadwaita FT8 and FT4 program for Linux (TCI
radios); decoders ported from WSJT-X").

2026-10-10 (evening): WSJT-X v3.0.2 (git ccdfaf3) shallow-cloned to
`~/.local/opt/wsjtx` (sample WAVs are in the tree: `samples/FT8/210703_133430.wav`,
`samples/FT4/000000_000002.wav`). Scaffold from the family template (meson,
GPL-3 LICENSE, README, CI build job on tags/PRs/dispatch only, `docs/PORT-MAP.md`).
First two modules ported with passing bit-exact gates (`meson test -C build`):
`ftx_hash` (13 vectors from WSJT-X `tests/unit/packjt77/`) and `ftx_crc14`
(314 vectors dumped from WSJT-X's own `lib/crc14.cpp` by
`dev/tools/vectors/crc14_dump.cpp`; Boost headers are installed, no Fortran needed).
gcc-fortran installed with Richard's consent (2026-10-10, dev/test only; the
WSJT-X Fortran is compiled per file straight from the clone, no CMake/Qt).
Third module `ftx_pack77` (the complete 77-bit codec incl. chkcall/fmtmsg,
state in `ftx_pack77_ctx`) ported and gated: `dev/tools/vectors/pack77_dump.f90`
drives WSJT-X's own `packjt77` over the scenario files in
`dev/tools/vectors/pack77/`, 1 693 pack + 12 697 unpack vectors, 0 mismatches
(see dev/tools/vectors/README.md for the pinned-UB flags and the in/out `c77`).
Fourth module `ftx_ldpc` (encode174_91, the hybrid BP/OSD `decode174_91`,
`osd174_91`, indexx, platanh, tables generated by
`dev/tools/vectors/ldpc_tables_gen.py`) ported and gated by
`dev/tools/vectors/ldpc_dump.f90`: 1 953 encode + 8 100 decode vectors across
BP-only, maxosd 0/2/3, OSD orders 0-4, AP masks 29/58/77 bits and Keff
77/84/91; decoded bits and `dmin` bit-exact, 0 mismatches. OSD state lives in
`ftx_ldpc_ctx` (~2.4 MB, heap). FT8 signal path ported and gated bit-exactly against the Fortran
run from the clone (dev drivers in `dev/tools/vectors/`: `ft8_sync_dump.f90`,
`ft8b_dump.f90`, `ft8_decode_dump.f90`; ft8sim built with a fixed seed +
`dev/tools/vectors/` mixer for synthetic WAVs in `dev/tests/wav/`): sync8
chain, ft8b (all AP variants), subtractft8, and the `decode()` state machine
with jt9's 41/47/50 call sequence. `ftx-decode` CLI prints jt9-format lines;
decode sets identical on the sample WAV (depth 1/2/3) and 5 synthetic WAVs.
The oracle is our own Fortran driver linked from the v3.0.2 sources, so the
AUR `wsjtx`/`jt9` is only an optional cross-check now. Milestones 2+3 done: `ftx_ft8_a7a8` (ft8_a7d, getmsg, ft8_a8d, genft8,
smo121, db) ported; ft8b refactored into shared refine/bit-metric helpers;
gates with my/DX call set (AP types 1/3/4/6, a8 on 3 files, a7 on two-period
sequences) all bit-exact — 57 local gates green. FT4 ported too (`ftx_ft4`, own context; decode lines bit-exact vs
`ft4_decode_dump.f90` on the FT4 sample, 5 synthetic and 5 AP-only files;
CLI `-5`). 76 local gates green. Phase 1 decoder deliverable is complete.
Not ported: real-time wall-clock bail-outs (ldiskdat=false; live RX phase).
2026-10-11: TX encoder API (`ftx_ft8_tx_tones/_tx_wave`, `ftx_ft4_tx_tones/_tx_wave`,
real waveform as mainwindow.cpp transmits: 48 kHz, nsps 7680/2304) gated
bit-exactly vs `dev/tools/vectors/txwave_dump.f90` (19 messages, 4 full
waveforms, 15 checksums; 12 kHz too). 77 gates green. TCI RX-audio client (`src/app/tci_client.[ch]`, libwebsockets + GLib,
modelled on skimmer-for-linux's IQ client; commands audio_samplerate:12000,
audio_stream_channels:1, audio_start:0; Stream type 1 float32), UTC period
assembler (`src/app/period.[ch]`: sample counting anchored to CLOCK_REALTIME,
re-anchor above 100 ms drift, gap figure for partial periods; unit test with
an injected clock) and `ftx-record`. End-to-end test against
`dev/tools/tci_mock_server.c` (streams a WAV UTC-aligned): recordings decode
identically to the source, DT preserved. The real sdr-for-linux server was
not running during this work (connection refused on :40001); the first live
connection is still pending. `ftx-rx` (live RX: decoder in a worker thread fed by the period
assembler, jt9 lines on stdout, per-period status with mean-DT clock check on
stderr) done; against the mock it reproduces the sample WAV's 21 decodes in
1.9 s per period. Live test against the real server pending (needs
sdr-for-linux running). GUI mockups done 2026-10-11 (`docs/mockups/`:
`ftx_main_window.c`, `ftx_preferences.c`, PNGs, README with the design
decisions and the Broadway + headless-Chrome screenshot recipe; Richard's
user GTK stylesheet recolours stock widgets, hence TX state in custom-drawn
elements). Installing AUR `wsjtx` (jt9 oracle) is a later, separate consent.

2026-10-11 (later): Richard on the mockups: "zatím nevím, postavíme to a
budeme to pak dál ladit a upravovat podle používání v praxi" — so the GUI
was built as mocked and is tuned from use, not from further layout questions.
**GUI built, RX only** (`src/ui/`, binary `ftx-for-linux`, `data/` with
desktop file, icons, metainfo, CSS resource): `window.c` (header with the
radio's dial/mode from TCI, Monitor = connect/disconnect in a GTask thread,
FT8/FT4), `waterfall.c` (rows with UTC stamps → period tags, decode labels
on traces, Rx/Tx brackets; click = Rx, right/Shift = Tx, Ctrl = both, as
WSJT-X's plotter), `decode_list.c` (jt9 line layout, period separators,
double-click = answer), `qso_panel.c` (DX call/grid/report, the transmission
card, Tx1–Tx6, auto-sequencer switch; TX buttons insensitive with the reason
as tooltip until TX over TCI exists), `status_bar.c`, `preferences.c`
(Station/Radio/Decoder/Logging/Display, every row a real parameter), About
with debug info. `src/app/rx_engine.c` is the live RX engine shared by
`ftx-rx` and the GUI (jt9's 41/47/50 sequence, ltry_a8, skipped periods,
spectrum rows for the waterfall; callbacks marshalled to a GMainContext);
`src/app/messages.c` ports genStdMsgs/genCQMsg/stdCall, base_callsign, the
DecodedText tokenizer, deCallAndGrid, report(), stdmsg; `window.c` ports
processMessage (SpecOp::NONE, FT8/FT4), the Rx-pane rule and write_all
(ALL.TXT in `~/.local/share/ftx-for-linux/`, optional). Settings are a
GKeyFile at `~/.config/ftx-for-linux/config.ini` (`src/app/settings.c`).
Decimal points everywhere (`setlocale(LC_NUMERIC, "C")` after GTK init).
Minimum versions declared: gtk4 ≥ 4.12, libadwaita ≥ 1.5.
Verified headless (Broadway :22 + headless Chrome with remote debugging,
`dev/tools/cdp_shot.js` and a click driver, mock TCI server streaming the
FT8 sample; work dir /tmp/ftx-gui, trashed afterwards): the window connects,
shows the sample's 21 decodes per period in Band activity, the K1JT rows in
Rx frequency with "→ me", labels and period tags on the waterfall, mean DT
in the status bar; double-click on "K1JT EA3AGB -15" sets DX EA3AGB,
report -17, received -15, next = Tx3 "EA3AGB K1JT R-17" (as processMessage
does); right-click moves Tx; the auto-sequencer re-sets Rx to the partner's
frequency on his next decode (WSJT-X behaviour). Under Richard's stylesheet
the card and tags stay readable. `ftx-rx` after the refactor onto the engine
reproduces the same decode sets on the mock (per-line ±1 dB/±0.1 s between
runs = stream alignment, identical periods within a run). 79 local gates
green (new `dev/tests/test_messages.c`). Deviations to remember: the
sequencer reacts on Auto sequence alone (WSJT-X also requires Enable TX —
that gate comes with TX over TCI); decoding runs after the period ends (no
early decode at 11.8 s yet); country/zones/B4 are blank until log-for-linux
is linked. Next: Richard runs it live against sdr-for-linux and we tune from
practice; then TX over TCI + logging.

2026-10-11 (afternoon, live): Richard started sdr-for-linux; the GUI runs on
his desktop against the real TCI server (127.0.0.1:40001, 12 kHz float
audio), first live period decoded 13 FT4 stations at 14.080, DT ≈ 0.0–0.2 s.
His station config was created at `~/.config/ftx-for-linux/config.ini`
(OK1BR, JO70 — grid assumed, ALL.TXT on). Tuning from practice so far:
preferences dialog 1000 px wide so the page tabs stay in the header
(libadwaita drops them to a bottom bar when the switcher does not fit);
the Tx1–Tx6 list and Auto sequence moved into a popover on the card's
`TxN · Hz` button ("rarely skipped steps, almost never used it in WSJT-X");
the waterfall palettes are the family table from sdr-for-linux's
`waterfall.c` (Classic, Mono white, Mono green, Mono amber, Inferno, Turbo —
same stops, same names, selectable in Preferences › Display); the spectrum
strip above the waterfall is gone ("spektrum dej pryč úplně, nech tam jen
vodopád"), only the frequency axis and the rows remain; 8 rows/s default,
1–10 selectable (the spectrum FFT hops 1024 samples, up to ~11 rows/s; the
hop once kept the wrong sample count — fixed when 8 rows/s showed as 4).
Layout changed to one column (Richard: three columns side by side were not
ergonomic, the window too wide): the controls strip directly under the
waterfall (two rows), the two decode panes side by side beneath it; decode
lines and status items ellipsize, default window 1180×960.
**Deep decoding timed like WSJT-X** (Richard: "zaměř se na deep dekódování …
nesmí to být moc pomalé"): the period assembler fires stage callbacks
(`ftx_period_set_stages`) at WSJT-X's half-symbol marks — FT8 at 41/47/50
(11.8/13.5/14.4 s, `m_earlyDecode`/`m_earlyDecode2`/`m_hsymStop` in
mainwindow.cpp), FT4 at 21 (6.05 s = FT4_NMAX) — and the RX engine decodes
each stage as it arrives (STAGE jobs, the period's END job carries the
statistics and is processed after the last stage).  The Fortran's live
bail-outs are ported (`tseq_now()` in ftx_ft8_decode.c, `ldiskdat` false:
the nzhsym-41 candidate loop stops at 13.4 s into the sequence, the
nzhsym-47 subtraction loop at 14.3 s; gates pass ldiskdat true).  Measured
on Richard's Core Ultra 7 265: nzhsym 41 ≈ 780 ms (17 decodes of the
sample), 47 ≈ 150 ms, 50 ≈ 1.55 s (a7/a8 and OSD); with staging the first
decodes print at 11.85 s into the period and the last 0.2 s after its end
(before: all ≈ 2.5 s after).  Message set identical to ftx-decode.
`FTX_RX_TIMING=1` prints per-stage times.  Waterfall labels are anchored
10 s into the period (FT4 55 %) so early decodes do not slide; CQ labels
no longer carry the "CQ" prefix (green says it).  The DX call / DX grid
entries are gone from the strip (Richard: the program can keep that in the
background, the card shows whom I call) — row A shows the partner as text
(call · grid · km · °); the entries live in the card's popover for a sked.
Decodes carry jt9's AP marks (aN, "?") in the lists, the labels and ALL.TXT
(two false "OK1BR … R MJ61"-style AP decodes on the air prompted it).

2026-10-11 (afternoon): **TX over TCI done** — Richard: "chybí nám ladicí
tlačítko tune" / "počkej, TX ještě není podporováno?".  `src/app/tx_engine.c`
(GLib): keys with `trx:0,true,tci;`, streams the 48 kHz FT8/FT4 waveform
(`ftx_ft8_tx_wave`/`ftx_ft4_tx_wave`, nsps 7680/2304, scaled by the TX
audio level) as TX audio Stream blocks (type 2, float32 mono, 2048 frames)
with WSJT-X's Modulator timing (FT8 starts 0.5 s into the period, FT4 0.3 s,
late start skips into the waveform), 150 ms of silence first so the stream
runs that much ahead of real time (sdr-for-linux's exciter plays the first
block on arrival and pads underruns with silence; its ring is 341 ms), a
100 ms silent tail, then `trx:0,false;`.  Tune = a steady tone at the Tx
frequency the same way.  `tci_client` got `ftx_tci_client_send` and
`_send_tx_audio` (the out queue carries text or binary).  `window.c`:
Enable TX = WSJT-X's m_auto — the tick starts the current message in our
period (tx_first ↔ even; not after 0.75 of the period), Call CQ = Tx6 +
Enable TX, Halt TX unkeys now, the first 73 sent ends auto TX
(cease_auto_Tx_after_QSO), a runaway watchdog (`watchdog_min`, same message
too long), mode check (DIGU/USB) before keying, the Tx line in the Rx pane
and in ALL.TXT (WSJT-X write_all "Tx" format); the sequencer's automatic
reaction is now gated on Enable TX like WSJT-X; the RX engine skips a period
in which the radio was keyed at any time.  Preferences › Radio › Transmit:
TX audio level (% FS, default 90) and the mode check.  Verified against the
mock (dev/tools/tci_mock_server.c now echoes trx and captures TX audio
into a 48 kHz WAV aligned to the period): Call CQ → keyed in the next odd
period, first block 0.353 s into it, 12.9 s captured, decimated to 12 kHz
it decodes as "CQ K1JT FN20" at 1500 Hz with DT 0.0; Tune → 1500 Hz at
0.90 FS until the second click.  Not yet verified: on the real radio (the
first key-up is Richard's — DIGU, antenna/dummy load).  Log QSO stays
greyed out until the log-for-linux link.
Headless testing note: the app is a unique GApplication — run test
instances under `dbus-run-session`, or they just activate the live one.
The live instance is relaunched from `/tmp/ftx-live` (log `app.log`) via
`gapplication action cz.ok1br.ftx_for_linux quit` + start.

2026-10-11 (late afternoon): **second layout pass, built** (Richard: "zatím se
mi to zdá takové moc nedodělané … připrav mi nejdřív návrhy", then on the
rendered proposal "ok, zkus to, mockupy vypadají dobře").  His window is tiled
by Hyprland next to sdr-for-linux at 1519×1364 (5120×1440 screen), palette
Mono white, his own gtk.css — proposals and checks are rendered in that
geometry and theme (mock TCI server + Broadway + headless Chrome,
`dev/tools/cdp_shot.js`; the mock serves one client at a time).  Mockup
`docs/mockups/ftx_main_window_v2.c` + PNGs, README there lists the seven
changes.  Implemented: `qso_panel.c` is the console (QSO group: call in large
type, grid · km · °, report to send / received, Log QSO; Transmit group: badge
RX/ARMED/ON AIR/TUNE drawn by the program with the group border following,
the message, `TxN · Hz` popover now also holding Tx frequency + Hold + Tx = Rx
and the DX call/grid by hand, state line, four equal buttons); the period
line (8 px, full width, under the waterfall, green receiving / red our slot
or on the air) in `window.c` replaces the status-bar progress bar
(`ftx_status_bar_set_clock` takes the time only); waterfall flags in the axis
instead of the readout box, labels in three lanes by overlap; the lists in a
horizontal GtkPaned (`[window] lists`, default 820, Band activity keeps its
width), tags right after the text, pane header notes (`ftx_decode_list_set_note`:
"21 decodes · 152815" / "Rx 1648 Hz"); band beside the dial.  Verified
headless against the mock in his geometry and theme: idle, double-click →
HA0DU KN07 · 7210 km · 48°, Enable TX → ARMED then ON AIR with the red line
and the Tx row, Halt, the popover.  79 gates green.
Tuned minutes later from Richard's first look: the header carries icons as
the family does (Monitor = `network-transmit-receive-symbolic` toggle, FT8/FT4
= a GtkDropDown; "v té horní liště by měly být ikony jako v sdr"); the report
to send is a read-only label ("v reálném provozu ho stejně nikdy
neopravuji"); Esc = Halt TX via a key controller on the window, bubble phase
so an open dialog or popover takes the Esc first ("halt tx tlačítko musí
reagovat na ESC klávesu").  Verified headless: double-click → report −14 shown,
Enable TX → ON AIR, Esc → RX with the "Halt TX" toast.
Then: the Tx passband marker had its line on the upper edge with the fill
below ("vypadá jako by ukazoval LSB a ne USB") — both passbands now start at
the base tone and extend upwards, the Tx edges dashed and its axis flag an
outline so Rx = Tx stays readable; the shades are sdr-for-linux's (gui.c
COL_RX_FILL/EDGE, COL_TX_FILL/EDGE, fill 0.22·op, 1 px edges 0.95·op) with
the same "Filter opacity" slider (`[display] filter_op`, default 60) in
Preferences › Display ("odstíny filtru udělej stejně jako má SDR, včetně
případného nastavení").
Then: the period line uses the same shades as the passbands (sdr-for-linux's
COL_RX_FILL / COL_TX_FILL at 0.65 / 0.55 alpha; "moc svítí"), and Halt TX is
a plain button — the salmon came from libadwaita's `destructive-action`
class, which Richard's ok1br.css deliberately leaves alone (`:not(.destructive-action)`),
not from our stylesheet.
Then: the status bar values are plain white, inactive items in sdr-for-linux's
.dim grey (opacity 0.6), red only for faults (CLOCK OFF?, TCI disconnected);
no green, no salmon ("hodnoty v patičce nemusí být zelené, tam můžou být
normálně bílé, případně šedivé pro neaktivní") — CSS only, the state enum stays.
Then: **the DX station's country** on the console's partner line ("hodila by se
i informace o jakou jde zemi… do toho okna jak je lokátor, vzdálenost a azimut"):
`src/app/cty.[ch]` is log-for-linux's `src/engine/cty.[ch]` (AD1C cty.dat parser,
ftx_ prefix, logic untouched so fixes port by diff) with `data/cty.dat` +
`cty.dat.LICENSE` (MIT, VER20260803, the snapshot log-for-linux ships, installed to
`datadir/ftx-for-linux`); `ftx_cty_locate` prefers a user copy in
`~/.local/share/ftx-for-linux/` then `~/.local/share/log-for-linux/` (one refreshed
snapshot on the desk serves the family), then the system data dirs, then
`<exe>/../data/cty.dat` (build-dir runs from any cwd — the live instance runs from
/tmp/ftx-live).  Line format "KN07 · 7210 km · 48° · Hungary", country alone
when the grid is unknown; About debug info lists the cty.dat version and path;
gate `dev/tests/test_cty.c` (log-for-linux's cty_test with the sample as a vector
file) → 80 gates.  The lists' country column still waits (not asked).
Then: **list order and follow** (Richard: "výpisy band a rx řadit sestupně časově,
volba v nastavení … aby se to vždy odscrollovalo na začátek výpisu"): Preferences ›
Display › Decode lists has "Newest period first" (`[display] newest_first`, default
off = WSJT-X order) and "Follow new decodes" (`follow_new`, default on: every new
line scrolls to the newest period — the top when newest first, the bottom otherwise;
off = the old sticky behaviour, follow only when already there).  `decode_list.c`
keeps a chronological GArray model; newest first inserts the separator at index 0 on
a period change and the lines at `cur_count` so a block keeps the decoder's order as
stages 41/47/50 arrive; prune drops the first block (oldest first) or the last
(newest first) plus the model's first run of equal nutc/ft4; a runtime order change
rebuilds from the model.  Verified headless (newest_first, keep 2): block order,
within-block order, wheel-scroll then back at the top on the next period, the oldest
block gone after the third period.  The order flip in the dialog itself is not
exercised headless (Broadway tab limitation).

2026-11-09 (after a month's pause): live again.  sdr-for-linux could not find
the G2E: the PC's new input firewall of 2026-11-02 (`/etc/nftables.conf`,
table `inet pc`, policy drop) let in UDP from the radio's source ports
1024–1042 only, while gateware 11.0.5 answers P2 discovery from source port 0
(kernel log `PC-DROP … SRC=192.168.1.247 … SPT=0`); with Richard's consent the
rule became `udp sport { 0, 1024-1042 }` (backup
`/etc/nftables.conf.bak-2026-11-09`).  The PC is 192.168.1.12 now (DHCP; the
.18 in the notes above is old).  First live period after the fix: 18.100 FT8,
22 decodes, DT 0.0–0.5 s.
**Band switcher, working frequencies, DIGU** (Richard: "nahoře máme přepínač
FT4/8 a potřebujeme tam mít i přepínač pásem… frekvenční tabulku… aby se
správně přepnul režim DIGU"): `src/app/bands.[ch]` (160–6 m; IARU-wide edges =
sdr-for-linux's BANDS table; WSJT-X `models/FrequencyList.cpp`
default_frequency_list @ v3.0.2, the preferred FT8/FT4 entries — 160 m has no
FT4), settings `[frequencies] ft8_<band>` / `ft4_<band>` in Hz with those
defaults, Preferences › Frequencies (two groups of MHz entry rows, empty = none,
comma accepted; dialog 1120 wide for six tabs), a GtkDropDown of bands in the
header left of FT8/FT4 that follows the dial (nothing selected out of band,
insensitive while disconnected).  Selecting a band sends `vfo:0,0,<hz>;` and,
250 ms later, `modulation:0,digu;` (sent once more at +700 ms if the echo still
says otherwise): sdr-for-linux's `band_apply` runs on its next GUI tick after
the dial crosses into another band and restores the band-stack mode, and a mode
set before that tick is filed under the old band and then overridden (gui.c
on_mode_toggled / band_apply), so DIGU must follow the vfo, not ride with it.
FT8 ↔ FT4 keeps the band and retunes to the new mode's frequency (WSJT-X).  The
TCI client gained a modulation callback (`FtxTciModeCb` → engine `EM_MODE`) so
the header and the status bar show the radio's mode echo at once.  Pitfall found
headless: GtkDropDown delivers the notify of a programmatic set_selected after
the handler that made it has returned, so a "syncing" flag alone let a
dial-follow sync retune the radio — `on_band` ignores a selection equal to the
dial's band.  Verified headless against the mock (dev/tools/tci_mock_server.c
now answers vfo/modulation, emulates band_apply with lsb 30 ms after a band
change, SIGUSR1/SIGUSR2 = the operator turning the dial to 18.130 / 14.242;
dev/tools/cdp_click.js drives clicks): 20 m → 17 m gives vfo 18100000, the
mock's lsb, digu at +253 ms, header "18.100.000 · 17 m · DIGU"; FT8 → FT4 gives
vfo 18104000 + digu; 160 m in FT4 toasts and reverts with no command; operator
dial moves update the drop-down and send nothing back.  Preferences ›
Frequencies rendered headless (the tab click worked with a 1 s settle).  81
gates (new `dev/tests/test_bands.c`).  Verified live by Richard the same afternoon ("zkusil jsem přepnout jak pásmo tak
režim a vypadá to, že to funguje"); the sdr-for-linux log shows the designed
sequence on the real radio: band change 16:13:04.868 → band_apply opens the
mic in voice mode (band-stack mode) → 242 ms later "capture closed — non-voice
mode" as our DIGU lands.
Then (Richard: "pásmo a režim musí vynulovat band a rx logy, seznamy v těch
oknech… ty údaje už nejsou relevantní, patří na jiné pásmo"): a band change
(the dial's band in `on_vfo` differs from the last one seen, `cur_band`;
G_MININT until the first vfo so a start does not clear) and a mode change both
run `clear_decodes()` = the menu's Clear decodes (both panes, the Band activity
note, the waterfall).  A dial move inside the band does not clear (not asked;
a nudge mid-QSO would wipe the panes).  Verified headless: FT8 → FT4 emptied
full panes, the next period refilled them after a band change, 20 m → 17 m
with full panes (21 decodes) emptied both panes, the note and the waterfall.
**First live TX (2026-11-09 16:24–16:25, Richard: "zkusíme to na anténě… krátce
menším výkonem")**: Tune from FTX on 14.080 (20 m, FT4 dial), antenna.  FTX keys
with `trx:0,true,tci;` and streams the tone, so sdr-for-linux applies the band's
**Drive** (not its Tune power): the first two overs had Drive 0/255 → MOX,
PA on, ANT1, 0.00 W, the tone at the exciter at −1 dBFS peak (90 % FS); with
Drive 23/255 four overs of 15.5–16.8 W forward, 0.00 W reverse, SWR 1.02–1.03,
0 rf-muted slots, clean UNKEY each time (sdr-for-linux `tx:` log lines).  The
TX path FTX → TCI → key → audio → RF is verified on the air.
**First QSO on the air with FTX, 2026-11-09 15:38–15:39 UTC, 14.080 FT4, 16 W:**
Call CQ ("CQ OK1BR JO70" at 1773 Hz, every second period) → G7BTP IO93 answered
on the second CQ (−5 dB, DT −0.5) → the sequencer sent "G7BTP OK1BR -05" →
"OK1BR G7BTP R+03" → "G7BTP OK1BR RR73" → "OK1BR G7BTP 73"; auto TX stopped
after our RR73 (cease_auto_Tx_after_QSO).  Four overs of 16.8–16.9 W, 0 muted
slots.  Richard: "hned to chytlo a udělalo se jedno QSO".  Logged by hand in
log-for-linux (Log QSO is not linked yet) — the link is the next step.
**log-for-linux link (2026-11-09 afternoon, Richard: "nemáme ještě napojení na
log, tak mi to QSO prosím přenes… ať o něj nepřijdeme")**: the G7BTP QSO was
first hand-sent as a WSJT-X "QSO Logged" datagram (Python) to log-for-linux
on 127.0.0.1:2237 — accepted, id 628 in its log.db (main log, contest_ref 0)
— and that datagram is now the gate vector
`dev/tests/vectors/wsjtx_qso_logged_g7btp.bin`.  Then `src/app/wsjtx_udp.[ch]`
(GLib/Gio): WSJT-X UDP schema 3 as MessageClient sends it — Heartbeat every
15 s (`NetworkMessage::pulse`), Status on every change (dial, mode, DX, TX
enable, keyed, period), Decode per decode (period time as QTime ms, jt9 mode
letter `~`/`+`, "?" decodes as low confidence), Clear with Clear decodes,
QSO Logged + Logged ADIF (`\n<adif_ver:5>3.1.0\n<programid:13>FTX for
Linux\n<EOH>\n` + LogBook::QSOToADIF record + ` <EOR>`; FT4 = MFSK/submode),
Close on quit; incoming Highlight Callsign (log-for-linux answers a Status
that names a DX with yellow = worked before / green = new → `win->b4`, decode
kind B4 under `dim_b4`) and Halt Tx.  Frequency = dial + Tx audio offset
(mainwindow.cpp `m_freqNominal + TxFreqSpinBox`), RST sent = the report on the
console (m_rptSent), time on = first transmission to him backdated by
(ntx−1)·TRperiod (m_dateTimeQSOOn), TX power = the radio's `drive:0,<W>`
(sdr-for-linux sends watts; `ftx_tci_client_drive`).  Logging fires when our
73/RR73 has gone out and progress ≠ CALLING (mainwindow.cpp ~8113, once per
QSO via `qso_logged`; `auto_log`, default on); Log QSO on the console logs by
hand any time (`LOG_NO_QSO` until a DX is set).  Settings `[logging]
udp_enable/udp_host/udp_port/auto_log`, Preferences › Logging › log-for-linux,
status bar "● log-for-linux host:port · N logged (· no reply yet)".  Two bugs
found by the headless run: the received report was stored only on
double-click (the sequencer path left `rst_rcvd` empty — now m_rptRcvd in both
paths, a message without a report leaves it alone) and it was never cleared
when the DX changed (set_dx clears it).  Known limit: log-for-linux
highlights only the Status DX call, so B4 marks only the partner; per-decode
worked-before needs a log-for-linux change (it would answer Decode messages).
Verified headless against a fake WSJT-X UDP server (`dev/tools/wsjtx_fakelog.py`,
port 22370 so Richard's live log-for-linux on 2237 stays untouched): heartbeat
and Status at start, Status with DX after a double-click → Highlight back →
"linked"; Log QSO → QSO Logged HA0DU KN07 14075500 FT8 sent −14 + ADIF;
auto-log: mock switched by SIGHUP to sim_ap4 ("K1JT HA0DU R-15", AP) → the
sequencer sent Tx2 then RR73 (12.92 s captured) → QSO Logged on 16:08:30
(= first Tx period − 15 s) off 16:09:28, "73 sent — QSO complete, TX off";
after the two fixes the rerun logged `sent -22 rcvd -15`, ADIF `<rst_rcvd:3>-15`.
82 gates (new `test_wsjtx_udp`: vector, Julian days, ADIF, Highlight parse,
loopback client).  Headless notes: gtk4-broadwayd can SIGABRT on a TCI
reconnect (hence the mock's SIGHUP WAV switch instead of a restart), and a
`pkill -f` pattern without `^` kills the test shell itself.
**First QSO logged automatically on the air (2026-11-09 16:37–16:38 UTC):**
Richard called LZ2TS (KN22) on 14.080 FT4, Tx1 three times, "OK1BR LZ2TS
-07" → our "LZ2TS OK1BR R-05" → his RR73 → our 73 → QSO Logged landed in
log-for-linux as id 629 (20m, 14.08071 = dial + 710 Hz, FT4, rcvd −07, 40 W
from the radio's drive, ts 16:37:52 = the first Tx period).  One deviation
seen: the logged sent report was −04 while we transmitted R-05 — the console
report follows his signal within an FT8/FT4 QSO (mainwindow.cpp 9427 "Change
report within a QSO", ours too), but WSJT-X logs `m_rptSent`, taken from the
transmitted message (mainwindow.cpp ~8153).  Fixed: `win->rpt_sent` is parsed
from the message we send ("-05" / "R-05" → "-05"), cleared with a new DX or a
new QSO, and log_qso prefers it over the console value.  Richard's live
instance runs the previous build until he allows a restart.
**Worked-before for every decode (2026-11-09 evening, Richard: "musíš vzít v
úvahu jak pásmo, tak i mód… stanici pro FT4 aby šel udělat i na FT8… stejně
ho můžeme mít na více pásmech")**: the verdict is per band AND mode.  FTX
already sends a Decode per line; log-for-linux (commit in its repo, built and
installed to ~/.local) now parses Decode, takes the sender call as
decodedtext.cpp's tokens_re does, looks up `logfl_store_worked_b4` for
(call, band from its last Status dial, mode from the Status) and answers a
yellow Highlight only when `n_band_mode > 0`; its Status answer uses
`n_band_mode` too (was `n_total`).  Verdicts are cached per call|band|mode
and dropped on every log change.  FTX forgets `win->b4` on a band or mode
change (clear_decodes).  Live: after log-for-linux was restarted on the new
binary nothing came back, because it learns dial and mode from Status and
FTX sent one only on changes — now FTX sends a Status at every period end
(WSJT-X does around each decode cycle), and the sniff on lo showed a yellow
Highlight for LZ2TS (worked today on 20 m FT4) in each period it was
decoded, nothing for the others.  The log-for-linux commit (e79555c) was
pushed the same evening with Richard's "to se mi líbí, můžeš to uložit na github".

**Country in the decode lists (2026-11-09 evening, Richard: "zvýraznění země
určitě budeme chtít")**: the DE station's DXCC entity stands in dim type after
the message, at WSJT-X's column (displaytext.cpp `leftJustifyAppendage`: a
fixed column while the message is short, else it floats right; one Pango
markup label with `alpha="60%"`, so a narrow pane cuts the country first).
`FtxDecodeRow.country` is filled in `on_decode` from `ftx_cty_lookup` of the
tokenizer's w2 (the DE call, also for "CQ DX …") through
`ftx_msg_country_short` (messages.c: WSJT-X's abbreviations in its order —
Islands → Is., Republic → Rep., "Fed. Rep. of " dropped, United States →
U.S.A., European/Asiatic → EU/AS, …, and its merge of the WAE-only entities
into their DXCC entity: Sicily/African Italy → Italy, Bear Is. → Svalbard,
Shetland Is. → Scotland, Vienna Intl Ctr → Austria; one deviation: "Fr. "
keeps its space).  Setting `[display] country`: 0 off, 1 CQ calls (default;
WSJT-X's "Show DXCC, grid, and worked-before status"), 2 all messages
(WSJT-X's "Show DXCC for all messages"), Preferences › Display › Decode lists
› Country; read per decode, so a change applies to new lines.  The console's
partner line keeps the full entity name.  Verified headless against the mock
(user theme, FT8 sample): CQ lines "CQ EA2BFM IN83 … Spain", "CQ DX DL8YHR
JO41 … Germany", the rest bare; with 2 every line (Spain, Hungary, Crete,
Germany) with the → me tags after the country.  Vectors for the abbreviations
in `test_messages`; 82 gates green.

**Icon (2026-11-09 evening, Richard: "ještě nemáme ani vhodnou ikonku" → "jo
asi může být")**: the first icon's small tone cells did not read as FT8 at any
size.  New `data/cz.ok1br.ftx_for_linux.svg` in the family language (sdr /
skimmer / log: Papirus plate #4f4f4f, 4 px white strokes, green #4caf50
accent): a bold FSK trace stepping between the tones over time, the green Rx
bracket beneath; the symbolic icon is the same trace in a 2 px stroke like
sdr-for-linux's.  Proposal rendered beside the family icons at 256/48/16 px
and approved before it went in.  Install to ~/.local is the next step.

**Installed and the first release prepared (2026-11-09 evening, Richard: "no,
můžeme... a připravil bych vydání první verze")**: `meson configure build
-Dprefix=$HOME/.local` + `meson install` — FTX now lives in ~/.local like the
rest of the family (bin, desktop file with the absolute Exec, both icons,
metainfo, cty.dat); the live instance still runs from the build tree via
/tmp/ftx-live until the next restart.  Release machinery = the family's
(log-for-linux's build.yml is the newest, copied and adapted): version
**0.1.0** in meson.build (the tag must match), LICENSE installed to
`datadir/licenses/ftx-for-linux`, `.github/workflows/build.yml` with the
`gates` job (build + an ftx-decode smoke on a missing file — which made
ftx-decode exit 1 on an unreadable input, it returned 0 before) and the
`artifacts` job (AppImage via linuxdeploy + GTK plugin, .deb via nfpm on
ubuntu-26.04, .rpm compiled inside fedora:44, staged ⊆ packaged check,
install tests with an xvfb window start, attach on v* tags; dispatch = dry
run), `packaging/nfpm.yaml` (all four binaries, LICENSE twice for deb/rpm,
Debian copyright, desktop, icons, cty.dat + LICENSE, metainfo; deb depends
libfftw3-single3 + libwebsockets19t64 + the t64 GLib, rpm fftw-libs-single),
`packaging/PKGBUILD` (sha256 SKIP until the tag exists), `packaging/copyright`
(src/ftx/* names the ten WSJT-X holders), metainfo release 0.1.0 (the never-
released 0.0.1 entry replaced), README › Install + Build from source.
Checked locally: desktop-file-validate, appstreamcli validate, 82 gates,
`makepkg -C -f` from a tarball of the tree → ftx-for-linux-0.1.0-1 with the
same 11 files nfpm.yaml lists, namcap only the family's implicit warnings.
Not checked locally: nfpm itself (no binary here; the classifier blocked
downloading it, Richard: skip, CI does it) and the xvfb GUI smoke (no xvfb
here).  Two compile warnings in wsjtx_udp.c removed (G_SOURCE_FUNC, unused
wr_u16).  Next: push → `gh workflow run build.yml` dry run → notes EN + CZ
approved → second "ano" for `gh release create v0.1.0 --target main` → the
tag build attaches the artifacts → refresh ~/.local → AUR package (new,
separate consent).

**★ RELEASED 2026-11-09 evening: v0.1.0, the first release.**  Dry run
42689782603 green (both jobs; the .deb/.rpm install tests with a window
start passed; the dry-run AppImage also ran here under Broadway, found
cty.dat and connected to the live TCI).  Notes EN + CZ approved, second
"ano" for `gh release create v0.1.0 --target main --title "FTX for Linux
0.1.0"`.  ⛔ The first tag build (42691426397) failed in "Determine
version": the family's `grep -Po "version : '\K[^']+" meson.build` matched
our gtk4/libadwaita `version : '>= …'` constraints too (three lines ≠ the
tag; the dry run never compares).  Fixed with `grep -Pom1 "^\s+version :
'\K…"` (d21dd58), and with Richard's "můžeš" the tag was moved there
(annotated + signed — `tag.gpgsign` is on, a bare `git tag -f` asks for a
message; `git push --force origin v0.1.0`), the release stayed; tag build
42691902053 attached FTX_for_Linux-0.1.0-x86_64.AppImage,
ftx-for-linux_0.1.0_amd64.deb and ftx-for-linux-0.1.0-1.x86_64.rpm.  The
public AppImage embeds 0.1.0 and runs here.  PKGBUILD sha256 recomputed
after the move (9d12ccb; the tarball verified to carry the fix).  ~/.local
refreshed to 0.1.0.  Not done: the AUR package (new for FTX, separate
consent); the live instance still runs from the build tree.

**AUR package created the same evening (Richard: "ano", then a second "ano"
after the classifier blocked the push as "Create Public Surface")**:
https://aur.archlinux.org/packages/ftx-for-linux — `PKGBUILD` + `.SRCINFO`
(`makepkg --printsrcinfo`) pushed to ssh://aur@aur.archlinux.org/ftx-for-linux.git
(master, 6c40c56; ~/.ssh/config already carried the aur host with
`aur_ed25519`); a full `makepkg -C -f` from the tagged tarball passed first
(sha verified by makepkg, package built, namcap clean).  The package page
showed 0.1.0-1 at once; the RPC index lagged.  Release checklist for every
later tag: bump `_pkgtag`/`pkgver`, `updpkgsums`, full makepkg, regenerate
`.SRCINFO`, push to the AUR repo with Richard's explicit OK.  README › Install
now points at the AUR package.

2026-11-10 (night): the live instance runs from `~/.local/bin` (0.1.0+).
**Decode lists black under Richard's theme**: Graphite's `list { background-color:
#0F0F0F }` arrives at USER priority and beat `.decodes { background: transparent }`
(the panes stood out dark grey on the black window; log-for-linux's table is a
columnview and was black) — `main.c` loads that one rule at USER + 1.  Richard
rejected a theme edit ("proč u logu je tabulka pěkná černá a u ftx mi nabízíš
změnu tématu?"): look fixes belong in the app.
**DXCC tag ("zkusíme štítky")**: log-for-linux answers every Decode with a
verdict — yellow = worked on this band and mode, magenta (WSJT-X's default New
DXCC colour ff00ff) = the DE station's DXCC entity is not in the log on this
band and mode (entities resolved from the logged calls through cty.dat, WAE-only
entities folded into their DXCC entity; the set is rebuilt after every log
change), and it takes a magenta back (invalid colour) once a logged QSO makes it
stale.  FTX: `win->newdx`, `FtxDecodeRow.de/new_dxcc`, the `new DXCC` tag as a white outline (purple first: "růžová mi tam nesedí")
(`.tag-dxcc`), `[display] tag_dxcc` (default on, Preferences › Display › Decode
lists › Tag new countries).  A verdict arrives after its line was added, so
`ftx_decode_list_mark` redraws the lines of the running period — B4 no longer
lags a period either.  Verified end to end headless (mock TCI with the FT8
sample, a test log-for-linux on port 22370 with a copy of Richard's log.db):
Spain (worked on 20 m FT8) untagged, France/Hungary/Balearic Is./Crete/Germany
tagged in the period they were decoded; after Log QSO for F5RXL the next period
showed F5RXL as B4 and the other French stations untagged.  With Richard's
three digital QSOs nearly every station is tagged for now.

2026-11-10 (night): tickets are GitHub issues in this repo (Richard: "založ na to
ticket"): #1 PSK Reporter spotting (open), #2 TCI over the Unix socket.
**#2 TCI client over sdr-for-linux's Unix socket, TCP as the fallback** (mirror of
log-for-linux#32 against sdr-for-linux#46): `ftx_tci_client_new(server)` takes
`unix:/path`, `ws://host[:port]` or a bare `host[:port]`; NULL/"" = the default
order `$XDG_RUNTIME_DIR/sdr-for-linux/tci.sock`, then `ws://127.0.0.1:40001`
(a missing socket file is skipped without dialling, a refused one falls
through; the error lists every attempt).  libwebsockets dials a Unix socket
when `ci.address` starts with `+`.  Settings: `[radio] tci_server` (empty =
automatic) replaces `tci_host`/`tci_port` — an old config with the default
pair migrates to empty, any other pair to `ws://host:port`; the old keys are
dropped on the next save.  Preferences › Radio has one address row; the status
bar says "socket" or host:port, About › Debug info the address in use and the
configured one; `ftx-rx`/`ftx-record -H` take the same forms (`-p` for a bare
host).  `dev/tools/tci_mock_server.c` listens on `unix:/path` too (lws makes
the socket 0660 and `unix_socket_perms` is "user:group", not a mode — chmod
0600 after the context is up).  Verified against the mock: ftx-rx over the
socket = the TCP run line for line and ftx-decode's 21 messages; default order
with XDG_RUNTIME_DIR pointed at the mock's socket; the GUI on Richard's
migrated config found the socket ("TCI ftx-mock socket DIGU"), Call CQ over
it was captured and decodes as "CQ OK1BR JO70" at 710 Hz, DT 0.0, first block
0.352 s into the period; a stale socket file and a missing one fall back to
TCP; `wss://x` is refused as an address.  Against the real radio (receive
only, seconds): the default order connects to
`unix:/run/user/1000/sdr-for-linux/tci.sock`.  Live 2026-11-10 01:32–01:39
(Richard: "vyzkoušej to sám"): the installed instance restarted onto the
socket — sdr-for-linux log `tci: client 0 connected (unix ftx-for-linux[pid])`,
no TCP connection to 40001 from FTX, decodes on 3.573 FT8; a second, headless
instance of the installed binary on the same socket keyed Tune for 4.7 s
(80 m, the band's Drive was 0 W, so no RF): `KEY MOX … PA=ON ANT1 drive=0/255`,
fwd 0.00 W, the tone at the exciter −4…−1 dBFS, 0 rf-muted slots, clean UNKEY.
Not done: switching the server's TCP listener off (a switch in sdr-for-linux's
Preferences › TCI — no remote way, and restarting Richard's radio is off
limits); the evidence instead is that FTX holds no TCP connection.  The
Preferences row was not looked at headless.

2026-11-10 (day): **#1 PSK Reporter spotting** (Richard: "ano můžeš").
`src/app/pskreporter.[ch]` ports WSJT-X v3.0.2 `Network/PSKReporterIPFIX.cpp`
(the packet builder, pure: templates 0x50e2/0x50e3 under PEN 30351, 5-byte
frequency, 4-byte padding, split at 952 bytes UDP / 65535 TCP) and
`Network/PSKReporter.cpp` (the client: report.pskreporter.info:4739, UDP or
TCP, the report timer at 121 s, templates in the first three UDP datagrams
and hourly / once per TCP connection, a call again only after 300 s — always
above 49 MHz and ±6 h around an eclipse, the dates of WSJT-X's eclipse.txt
compiled in — at most 2048 pending spots, a flush every 125 intervals).
`window.c` ports `pskPost`/`pskSetLocal`: a spot for a standard message that is
not a "?" decode, does not carry my own call and grid, and has a grid or a CQ
(so replies with a report are not spotted, as in WSJT-X); frequency = the
period's dial (`FtxRxDecode.dial_hz`, new) + audio offset; queued spots go out
on a band change; spotting runs only while switched on, the radio is connected
and callsign + grid are set, otherwise the pending spots are sent and the
socket closes.  Settings `[logging] psk_enable` (default **off** — nothing
leaves the machine), `psk_antenna`, `psk_tcp`; Preferences › Logging › PSK
Reporter; status bar item "● PSK Reporter · N spots" (hidden when off, red
"set callsign and grid" / "no connection", tooltip with the counts); About ›
Debug info line.  rigInformation = the TCI device name ("SDR-for-Linux").
Deviations from WSJT-X, all in the header: the spot time takes the period's
own date (WSJT-X pairs HHMMSS with today's date — its midnight guard
`sTimeString + m_TRperiod < 236000` can never be false); a period in which the
dial moved is not spotted (WSJT-X: nothing within 0.8 period of a frequency
change); the call cache is per reporter; a transport change carries pending
spots to the new socket; a failed connection is retried after 30 s; the TCP
path ignores the system proxy.  Dev hook: `FTX_PSKREPORTER=host:port` sends to
a local collector (`dev/tools/pskreporter_collector.py`, a template-driven
IPFIX decoder on 127.0.0.1 UDP+TCP).  **Never run a test instance with
psk_enable=true without that override** — the mock's decodes would reach the
real PSK Reporter as live reception; the headless runs also set
`udp_enable=false` and a non-existent call (Q1TST).
Gate `dev/tests/test_pskreporter.c` (84th): 331 scenarios / 668 packets /
16 390 spots byte for byte against `PSKReporterIPFIX::buildPackets` compiled
untouched from the clone with Qt5 Core (`dev/tools/vectors/pskreporter_dump.cpp`),
the cache rule with an injected clock, the client over the loopback UDP and
TCP read by an independent IPFIX reader, failure paths; clean under ASan/UBSan.
Verified headless end to end (mock TCI with the FT8 sample, collector on
127.0.0.1:14739): the app's only PSK socket pointed at the collector; the first
datagram came 121 s after the connection with both templates, the receiver
record (call, grid, "FTX for Linux v0.1.0", the antenna text with a "×", the
TCI device) and five spots — F5RXL IN94, HA0DU KN07, EA2BFM IN83, EA3CJ JN01,
DL8YHR JO41, exactly the sample's messages with a grid or a CQ, at 14 074 000 +
ftx-decode's audio frequency, period time as the spot time; status bar
"● PSK Reporter · 5 spots".  Not verified: the three Preferences rows were
below the fold of the headless canvas (the group title and text were seen),
switching on/off from the dialog, and — the real thing — a report accepted by
pskreporter.info (Richard's call on the air; an outward action, his consent).
Headless pitfall: a CDP `mouseWheel` event kills gtk4-broadwayd (and the app
with it) and the CDP call never returns — no wheel under Broadway, and every
helper call under `timeout`.

2026-11-10 (evening): **#1 live.**  Richard switched spotting on (antenna
"HexBeam", Use TCP) and pskreporter.info accepts the reports: a query for
receiver OK1BR (`retrieve.pskreporter.info/query?receiverCallsign=OK1BR&flowStartSeconds=-3600&rronly=1&nolocator=1`)
returned 92 reception reports half an hour later (80 m FT8, one 20 m FT4),
call / frequency / SNR / period time equal to ALL.TXT line for line, and a
report captured on the wire (`sudo tcpdump … port 4739`, decoded by
`dev/tools/pskreporter_collector.py`) reads as built: both templates, the
receiver record with rigInformation "SDR-for-Linux", 36 spots.
**Bug found live, fixed:** on TCP the client dropped its own connection 20 s
after every connect and logged "the collector closed the connection" —
`g_socket_client_set_timeout()` stays on the socket GSocketClient hands over,
and a GSocket source fires after that long without traffic, so the read watch
took 20 quiet seconds for a close (the capture shows our FIN first; an idle
connection of our own to the collector stayed open 150 s).  A report went out
only when the 121 s timer fell into those 20 s (the first one after an hour).
Fix: `g_socket_set_timeout(s, 0)` once connected; the watch now tells a close
from an error.  Gate `/pskreporter/tcp-idle` waits out the connect timeout
(22 s; it fails on the old code with the live log line).  The collector's
author on wsjt-devel (2020-06-07): the TCP listener takes the same messages
and closes the connection when one is invalid.  Not verified live: UDP
against the real collector (Richard runs TCP).
