# FTX for Linux

**A native GTK4/libadwaita FT8/FT4 program for Linux** — the digital-modes
member of the OK1BR family ([sdr-for-linux](https://github.com/OK1BR/sdr-for-linux),
[skimmer-for-linux](https://github.com/OK1BR/skimmer-for-linux),
[log-for-linux](https://github.com/OK1BR/log-for-linux),
[icom-for-linux](https://github.com/OK1BR/icom-for-linux)). The radio link is
TCI only.

> **Status: phase 1.** The FT8 decoder (candidate search, demodulation,
> LDPC/OSD, a-priori decoding, multi-pass subtraction) is ported from WSJT-X
> and reproduces its decodes bit for bit on the reference recordings; the
> `ftx-decode` command decodes WAV files. Not yet ported: WSJT-X's a7/a8
> decoders, FT4, live radio I/O, the GUI. Follow
> [docs/PORT-MAP.md](docs/PORT-MAP.md) for the module list.

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
  `-c MYCALL -x HISCALL` enable a-priori decoding, `-d 1..3` sets the depth.

## Build

```sh
meson setup build
meson compile -C build
```

Dependencies: meson, a C99 compiler, FFTW3 (`fftw3`, `fftw3f`).
