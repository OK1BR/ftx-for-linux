# UI design mockups — FTX for Linux

Throwaway **design references** for the GUI phase: two standalone libadwaita
programs with synthetic data (no decoder, no radio), plus the PNGs rendered from
them. They are **not part of the meson build**. Status 2026-10-11: proposed,
waiting for Richard's review before any GUI code goes into `src/`.

| File | What it shows |
|---|---|
| `ftx_main_window.c` → `ftx_main_window.png` | the main window, 1480×920, mid-QSO with our Tx3 on the air |
| `ftx_main_window_user_theme.png` | the same window under the author's own GTK stylesheet (see the last section) |
| `ftx_preferences.c` → `ftx_preferences*.png` | the preferences dialog: Station, Radio, Decoder, Logging, Display |

Everything in the pictures is made up: the band, the calls, the times
(the mockup lives at 13:34:47 UTC so the decode rows, the period tags on the
waterfall and the status clock agree).

## Design decisions

**libadwaita, dark, one window.** Same shape as the rest of the family
(sdr-for-linux decided libadwaita over plain GTK4 on 2026-07-07): an
`AdwApplicationWindow` with an `AdwHeaderBar`, custom Cairo graphics where the
instrument needs them, stock Adwaita rows and dialogs everywhere else. Forced
dark: a waterfall on a light window is unreadable. There is no second window:
the waterfall, the decode lists and the QSO panel are one layout with a paned
split, nothing to arrange on the desktop.

**Header bar = the radio.** The dial frequency and mode come from the radio
over TCI and sit where the eye lands first; click = retune. Monitor and the
FT8/FT4 mode toggles are the only other header controls. Band selection lives
in the radio (sdr-for-linux), FTX follows it — no band table of its own.

**Waterfall with meaning on it.** 0–4000 Hz, a spectrum strip above, 2 rows
per second. The things WSJT-X makes you cross-reference by hand are drawn in:
the callsign of every decoded signal sits on its trace; period boundaries carry
the UTC tag (the tag is inside the period it starts); the Rx bracket (green,
50 Hz = the FT8 occupied bandwidth) and the Tx bracket (red) show where we
listen and where we transmit. Click on a trace = set Rx there; the call goes
into the QSO panel.

**Two decode panes, as in WSJT-X, because operators know them.** *Band
activity* lists every decode of the period; *Rx frequency* lists only what is
on our Rx frequency — including our own transmissions, printed as `Tx` rows in
red, so the QSO reads top to bottom as a dialogue. Row colouring: CQ green,
addressed to me orange with a `→ me` tag, worked-before dimmed with a `B4` tag
(that information comes from log-for-linux, see below). Monospace, fixed
columns UTC / dB / DT / Freq / Message — identical to the `ftx-decode` and
WSJT-X line format, so a screenshot and a log line say the same thing.

**QSO panel: the one line that matters is the big one.** The partner's call,
grid and the report we send are one row of fields; beneath them what we know
about him (country, zones, distance and bearing from our grid, worked before
or not — from log-for-linux). Then the **transmission card**: what is on the
air (or goes out next), in which period, with how many seconds left. This card
and the `on air` tag in the Tx list are drawn by FTX itself and do not depend
on the theme colouring a toggle button, so the TX state stays unmistakable
under any GTK theme. Below it the controls that need to be hit fast: Enable
TX, Halt TX, Tune, Call CQ. The six standard messages Tx1–Tx6 follow with the
auto-sequencer switch; the radio button marks what goes next. Tx frequency
with Hold / Tx = Rx and the *Log QSO → log-for-linux* button close the panel.
Everything is visible at 1480×920; nothing scrolls away.

**Status bar = health, not decoration.** UTC clock and a period progress bar;
clock health as the mean DT of the last decodes (FT8 needs the PC within about
a second of UTC — the status turns red with `CLOCK OFF?` when the mean DT
exceeds the configured limit, as `ftx-rx` already does); the audio or TX state;
decode count and decode time of the last period; and the two links this
program lives on: TCI (host and the radio's mode — it must say DIGU before we
key) and log-for-linux (WSJT-X UDP, schema 3, port 2237).

**Preferences = five pages, nothing WSJT-X-shaped.** Station (call, grid, the
message options, watchdog), Radio (TCI host/port, TX path, mode check, split
threshold), Decoder (depth with `jt9 -d 3` as the reference, AP, a7/a8, decode
range, clock warning), Logging (log-for-linux UDP, ALL.TXT), Display (palette,
rows per second, labels, highlighting). Only settings that exist in the ported
decoder or the planned app layer are listed; nothing is a placeholder.

**Deliberately absent:** Fox/Hound, contest exchanges on TX, PSK Reporter,
wideband IQ decoding (parked in `CLAUDE.md`), any band table or rig control
beyond TCI, and a second window.

## Building and looking at them

```sh
cd docs/mockups
cc ftx_main_window.c -o ftx-mock-main $(pkg-config --cflags --libs libadwaita-1) -lm
cc ftx_preferences.c -o ftx-mock-prefs $(pkg-config --cflags --libs libadwaita-1)
./ftx-mock-main                 # waterfall scrolls, nothing else is live
MOCK_PAGE=decoder ./ftx-mock-prefs   # station | radio | decoder | logging | display
```

## How the PNGs were made

Headless, without a compositor: GTK's Broadway backend is the display,
headless Chrome is its client and takes the picture, ImageMagick trims the
canvas to the window.

```sh
gtk4-broadwayd :21 &                                   # listens on TCP 8101
XDG_CONFIG_HOME=$(mktemp -d) GDK_BACKEND=broadway BROADWAY_DISPLAY=:21 ./ftx-mock-main &
sleep 5                                                # a heavy user theme needs the time
google-chrome-stable --headless=new --disable-gpu --hide-scrollbars \
  --window-size=1520,960 --timeout=4000 --screenshot=raw.png http://127.0.0.1:8101/
magick raw.png -trim +repage ftx_main_window.png
```

The empty `XDG_CONFIG_HOME` keeps the user's `gtk.css` out, so the picture is
stock Adwaita dark; leave it off for the user-theme variant.

Preference pages the same way with `./ftx-mock-prefs`, `MOCK_PAGE=<page>` and
`--window-size=800,860`. Three things that cost time to find out: the Broadway
daemon reports a small default screen until a browser connects and GTK clamps
a window's *default* size to it, so under Broadway the mockups pin their
design size with a hard size request (`pin_size_under_broadway`); Chrome's
`--virtual-time-budget` does not wait for WebSocket traffic, so it races the
Broadway canvas and yields blank pictures — use the real-time `--timeout`;
and `GtkWidgetPaintable` self-snapshots are unreliable without a Broadway
client, because frames are painted only when the client acknowledges them.

The PNGs show stock Adwaita dark (rendered with an empty `XDG_CONFIG_HOME`,
so no user stylesheet is loaded). `ftx_main_window_user_theme.png` is the same
window rendered with the author's own GTK stylesheet, `~/.config/gtk-4.0/gtk.css`
(USER priority, above application CSS), which recolours every stock widget —
black window, white active toggles, so the red *Enable TX* becomes a white
one. That is the reason the TX state is also carried by custom-drawn elements
(transmission card, `on air` tag, status bar), which a user stylesheet does
not touch.
