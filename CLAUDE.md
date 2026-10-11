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
same stops, same names, selectable in Preferences › Display).
Headless testing note: the app is a unique GApplication — run test
instances under `dbus-run-session`, or they just activate the live one.
The live instance is relaunched from `/tmp/ftx-live` (log `app.log`) via
`gapplication action cz.ok1br.ftx_for_linux quit` + start.
