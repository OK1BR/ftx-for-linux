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
  WSJT-X source file, the tag, and the original copyright (Joe Taylor K1JT,
  Steve Franke K9AN, et al.); About acknowledges WSJT-X. (The Decodium
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

2026-10-10: project founded (decisions above). Nothing built yet. Next:
project scaffold from the family template, WSJT-X v3.0.2 clone to
`~/.local/opt/wsjtx`, then packjt77 → CRC14 → LDPC → sync8/ft8b as the
first ported modules, each with bit-exact vectors. Creating the public
GitHub repo and installing AUR `wsjtx` both need Richard's explicit "ano".
