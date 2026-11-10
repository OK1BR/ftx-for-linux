# FTX for Linux

**A native GTK4/libadwaita FT8/FT4 program for Linux** — the digital-modes
member of the OK1BR family ([sdr-for-linux](https://github.com/OK1BR/sdr-for-linux),
[skimmer-for-linux](https://github.com/OK1BR/skimmer-for-linux),
[log-for-linux](https://github.com/OK1BR/log-for-linux),
[icom-for-linux](https://github.com/OK1BR/icom-for-linux)). The radio link is
TCI only.

> **Status: on the air.** The FT8 and FT4
> decoders (candidate search, demodulation, LDPC/OSD, a-priori decoding,
> multi-pass subtraction, WSJT-X's a7/a8 decodes) are ported from WSJT-X and
> reproduce its decodes bit for bit on the reference recordings. The
> `ftx-for-linux` window works a TCI radio: labelled waterfall, Band activity
> and Rx frequency lists, a QSO panel with the WSJT-X auto-sequencer, and
> transmitting over TCI (the FT8/FT4 waveform exactly as WSJT-X sends it,
> streamed as TX audio with WSJT-X's timing; Tune tone), a band switcher
> that tunes the radio to the working frequency in DIGU, logging to
> log-for-linux over the WSJT-X UDP protocol (the QSO goes out as QSO Logged
> + ADIF when our 73 has been sent), and optional spotting to
> [PSK Reporter](https://pskreporter.info) as WSJT-X does it. First QSO with
> it: 2026-11-09, 14.080 FT4. CLI tools: `ftx-decode` (WAV files), `ftx-rx` (live, text),
> `ftx-record`. Follow [docs/PORT-MAP.md](docs/PORT-MAP.md) for the module
> list.

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
The country of a station comes from Jim Reisert AD1C's Amateur Radio
Country Files (`data/cty.dat`, MIT, see `data/cty.dat.LICENSE`).

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
  settings (`~/.config/ftx-for-linux/config.ini`), the band table with the
  WSJT-X default working frequencies (`bands.c`), and the WSJT-X UDP client
  (`wsjtx_udp.c`: Heartbeat, Status, Decode, Clear, QSO Logged, Logged ADIF,
  Close; Highlight Callsign and Halt Tx back) that log-for-linux listens to,
  and the PSK Reporter client (`pskreporter.c`: WSJT-X's IPFIX reception
  reports to report.pskreporter.info, UDP or TCP).
- `src/ui/` — the GTK4/libadwaita application `ftx-for-linux`: header bar
  with the radio's dial and mode, the band switcher (tunes the radio over
  TCI to the mode's working frequency — WSJT-X's defaults, editable in
  Preferences › Frequencies — and puts it into DIGU; FT8 ↔ FT4 moves to the
  other mode's frequency on the band; a band or mode change clears the decode
  panes and the waterfall), the waterfall (callsigns on the traces,
  period tags, Rx/Tx brackets; click = Rx, right click = Tx), the two decode
  panes as in WSJT-X, the QSO panel (partner, the transmission card, Tx1–Tx6,
  auto-sequencer), the status bar (UTC, period, clock health from the mean
  DT, TCI link, logger). Double-click a decode to answer it, as in WSJT-X.
  Log QSO sends the QSO to log-for-linux (WSJT-X UDP, 127.0.0.1:2237 by
  default), automatically once our 73 or RR73 has gone out; the logger's
  worked-before answers mark B4.
  Spotting to PSK Reporter is off until you switch it on in Preferences ›
  Logging; then the stations you decode (those sending a grid or a CQ) are
  reported with your callsign, grid and antenna description, batched every
  two minutes, each call at most once in five.
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

## Install

Pick whichever fits your distribution — the
[Releases page](https://github.com/OK1BR/ftx-for-linux/releases) carries
prebuilt packages for every release:

- **AppImage** (any current distro, glibc 2.43+): download, `chmod +x FTX_for_Linux-*.AppImage`,
  run. Everything bundled, nothing to install.
- **Ubuntu 26.04+**: `sudo apt install ./ftx-for-linux_*.deb`
- **Fedora 44+**: `sudo dnf install ./ftx-for-linux-*.rpm`
- **Arch Linux (AUR)** — [`ftx-for-linux`](https://aur.archlinux.org/packages/ftx-for-linux):
  `paru -S ftx-for-linux` (or `yay -S ftx-for-linux`). Builds the tagged
  release from source through the [PKGBUILD](packaging/PKGBUILD); without a
  helper: `git clone https://aur.archlinux.org/ftx-for-linux.git && cd
  ftx-for-linux && makepkg -si`

Both distro packages are install-tested in clean containers before they are
attached to a release.

## Build from source

C99 compiler, `meson`/`ninja`, and development files for **FFTW3** (single
and double precision), **GLib/GIO**, **libwebsockets**, **GTK 4** (>= 4.12)
and **libadwaita** (>= 1.5).

Arch: `pacman -S --needed gcc meson fftw glib2 libwebsockets gtk4 libadwaita`
(equivalent `-dev`/`-devel` packages on other distributions).

```sh
meson setup build
meson compile -C build
./build/ftx-for-linux
```

`meson install` adds the launcher, icons, AppStream metadata, `cty.dat` and
the licence text; with `--prefix ~/.local` it installs for the user alone.
