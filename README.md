# FTX for Linux

**A native GTK4/libadwaita FT8/FT4 program for Linux** — the digital-modes
member of the OK1BR family ([sdr-for-linux](https://github.com/OK1BR/sdr-for-linux),
[skimmer-for-linux](https://github.com/OK1BR/skimmer-for-linux),
[log-for-linux](https://github.com/OK1BR/log-for-linux),
[icom-for-linux](https://github.com/OK1BR/icom-for-linux)). The radio link is
TCI only.

> **Status: decoders done, GUI in progress (RX only).** The FT8 and FT4
> decoders (candidate search, demodulation, LDPC/OSD, a-priori decoding,
> multi-pass subtraction, WSJT-X's a7/a8 decodes) are ported from WSJT-X and
> reproduce its decodes bit for bit on the reference recordings. The
> `ftx-for-linux` window works a TCI radio: labelled waterfall, Band activity
> and Rx frequency lists, a QSO panel with the WSJT-X auto-sequencer, and
> transmitting over TCI (the FT8/FT4 waveform exactly as WSJT-X sends it,
> streamed as TX audio with WSJT-X's timing; Tune tone). CLI tools:
> `ftx-decode` (WAV files), `ftx-rx` (live, text), `ftx-record`. Not yet:
> logging to log-for-linux. Follow [docs/PORT-MAP.md](docs/PORT-MAP.md) for
> the module list.

## Provenance

The decoder core is a C port of the FT8/FT4 code in
[WSJT-X](https://github.com/WSJTX/wsjtx) **v3.0.2** (git `ccdfaf3`),
Copyright (C) 2001-2026 by Joe Taylor, K1JT, Bill Somerville, G4WJS,
Steve Franke, K9AN, Nico Palermo, IV3NWV, Uwe Risse, DG2YCB, Brian Moran,
N9ADG, Roger Rehr, W3SZ, John Nelson, G4KLA, Charlie Suckling, DL3WDG, and
Terrell Deppe, KJ5HST.
Every ported file names its WSJT-X source file in its header. WSJT-X and
this program are licensed under the GNU GPL v3 (or later); see
[LICENSE](LICENSE).

## Layout

- `src/ftx/` — `libftx`, the decoder core: C99 + FFTW3 only, no GLib,
  explicit context structs so several decoders can run in one process.
- `src/ftx-decode.c` — command-line decoder for 15 s WAV files (12000 S/s,
  16-bit mono), output in the format of WSJT-X's `jt9`:

  ```sh
  ftx-decode -d 3 210703_133430.wav
  133430  16  0.3 2571 ~  W1FC F5BZB -08
  ```
  `-5` selects FT4 (7.5 s files), `-c MYCALL -x HISCALL` enable a-priori
  decoding, `-d 1..3` sets the depth.
- `src/app/` — the GLib layer above the core (no GTK): TCI client (RX audio
  from sdr-for-linux / icom-for-linux, commands and TX audio to it),
  UTC-aligned period assembly, the live RX engine (decoder thread, waterfall
  spectrum), the TX engine (keying with `trx:0,true,tci`, the 48 kHz
  waveform paced in real time, Tune), the standard messages and
  decoded-text helpers ported from WSJT-X's `mainwindow.cpp`/`decodedtext.cpp`,
  settings (`~/.config/ftx-for-linux/config.ini`).
- `src/ui/` — the GTK4/libadwaita application `ftx-for-linux`: header bar
  with the radio's dial and mode, the waterfall (callsigns on the traces,
  period tags, Rx/Tx brackets; click = Rx, right click = Tx), the two decode
  panes as in WSJT-X, the QSO panel (partner, the transmission card, Tx1–Tx6,
  auto-sequencer), the status bar (UTC, period, clock health from the mean
  DT, TCI link). Double-click a decode to answer it, as in WSJT-X.
  Optionally writes a WSJT-X compatible `ALL.TXT`
  (`~/.local/share/ftx-for-linux/`). Design notes in
  [docs/mockups/README.md](docs/mockups/README.md).
- `src/ftx-record.c` — records UTC-aligned 15 s (or 7.5 s) periods from a
  TCI radio as WSJT-X-style `YYMMDD_HHMMSS.wav` files (12000 S/s, 16-bit):

  ```sh
  ftx-record -H 192.168.1.18 -p 40001 -n 10 ~/ft8-recordings
  ```
- `src/ftx-rx.c` — live decoding from a TCI radio: decodes on stdout in
  jt9's format, a status line per period (decode time, level, clock drift,
  mean DT) on stderr. `-5` for FT4, `-c/-x/-G` for a-priori decoding.

## Build

```sh
meson setup build
meson compile -C build
```

Dependencies: meson, a C99 compiler, FFTW3 (`fftw3`, `fftw3f`); the app layer
adds GLib and libwebsockets; the GUI adds GTK4 (>= 4.12) and libadwaita
(>= 1.5). `meson install` adds the launcher, icon and AppStream metadata.
