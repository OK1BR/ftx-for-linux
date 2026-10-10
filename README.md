# FTX for Linux

**A native GTK4/libadwaita FT8/FT4 program for Linux** — the digital-modes
member of the OK1BR family ([sdr-for-linux](https://github.com/OK1BR/sdr-for-linux),
[skimmer-for-linux](https://github.com/OK1BR/skimmer-for-linux),
[log-for-linux](https://github.com/OK1BR/log-for-linux),
[icom-for-linux](https://github.com/OK1BR/icom-for-linux)). The radio link is
TCI only.

> **Status: phase 1, nothing usable yet.** The FT8/FT4 decoders are being
> ported by hand from WSJT-X (Fortran 90) to C, module by module, each
> verified bit-exactly against WSJT-X itself. Follow
> [docs/PORT-MAP.md](docs/PORT-MAP.md) for what exists.

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

## Build

```sh
meson setup build
meson compile -C build
```

Dependencies: meson, a C99 compiler, FFTW3 (`fftw3`, `fftw3f`).
