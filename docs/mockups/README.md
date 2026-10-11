# UI design mockups — FTX for Linux

Throwaway **design references** for the GUI: standalone libadwaita programs
with synthetic data (no decoder, no radio), plus the PNGs rendered from them.
They are **not part of the meson build**.  The layout they show is
implemented in `src/ui/`; the live program replaces the synthetic data with
the real decoder, TCI audio and the WSJT-X QSO logic.

| File | What it shows |
|---|---|
| `ftx_main_window_v2.c` → `ftx_main_window_v2.png` | the main window (second layout pass, 2026-10-11), 1519×1364, mid-QSO with our Tx3 on the air, under the author's GTK stylesheet |
| `ftx_main_window_v2_idle.png` | the same window while receiving, no QSO |
| `ftx_main_window_v2_stock.png` | the same window in stock Adwaita dark |
| `ftx_preferences.c` → `ftx_preferences*.png` | the preferences dialog: Station, Radio, Decoder, Logging, Display |

Everything in the pictures is made up: the band, the calls, the times
(the mockup lives at 13:34:47 UTC so the decode rows, the period tags on the
waterfall and the status clock agree).  The first-pass mockup (three
columns, the Tx list in view) is gone; it survives in git history before
2026-10-11 if ever needed.

## The second layout pass (2026-10-11)

After the first live afternoon Richard asked for a tidier arrangement ("zatím
se mi to zdá takové moc nedodělané … připrav mi nejdřív návrhy").  The
window on his desk is tiled by Hyprland next to sdr-for-linux at
**1519×1364** on a 5120×1440 screen, palette Mono white, his own GTK
stylesheet — the mockup is rendered in exactly that geometry and theme.  What
looked unfinished: the controls strip was loose widgets without grouping or a
shared baseline; the two decode panes split 50/50 left Rx frequency mostly
empty with the `→ me` tags floating far from the text; the period progress
was a small bar in the status line; waterfall labels overlapped and the Rx/Tx
readout box floated over the newest rows.  Richard on the rendered proposal:
"ok, zkus to, mockupy vypadají dobře".  The changes, all implemented:

1. **The console** replaces the strip: two bounded groups of equal height.
   *QSO* — the partner's call in large type, grid · km · °, the report to
   send, the report received, Log QSO.  *Transmit* — a state badge drawn by
   the program (RX grey, ARMED amber, ON AIR / TUNE red; the group border
   follows), the message on the air or next in large monospace, the
   `TxN · Hz` button, a state line and four equal buttons Call CQ, Enable TX,
   Halt TX, Tune.
2. **Tx frequency, Hold and Tx = Rx moved into the popover** under
   `TxN · Hz`, beside Tx1–Tx6 with Auto sequence and the DX call/grid by
   hand.  Hold is on permanently in practice and the Tx frequency is set by a
   right click on the waterfall; on the strip they only took room.
3. **The period line**: full width, 8 px, under the waterfall, fills over
   the period; green while receiving, red in our slot with TX enabled or on
   the air.  It replaces the progress bar of the status line and reads from
   the corner of the eye.
4. **Waterfall**: the Rx (green) and Tx (red) markers get flags in the axis
   strip; the floating readout box is gone (the numbers live in the Rx
   frequency pane header and on the `TxN · Hz` button).  Labels are laid out
   in up to three lanes by overlap, sorted by frequency per period, instead of
   by arrival order.
5. **Decode panes**: a draggable divider with a persisted position (Band
   activity keeps its width, default 820 px, Rx frequency takes the rest);
   tags right after the text; the pane header carries the decode count and
   period, or the Rx frequency; the column header is a subtle strip.  Both
   panes stay wider than a jt9 line — the country, azimuth and B4 columns
   that come with the log-for-linux link will fill the width.
6. **Status line**: bold UTC clock, no progress bar, the rest unchanged.
7. **Header**: the band beside the dial (`20 m · DIGU`), the subtitle is the
   station only.

Unchanged as decided before: one column, no spectrum strip, Tx1–Tx6 in the
popover, the partner as text, the family palettes, labels without the `CQ`
prefix.

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

**Waterfall with meaning on it.** 0–4000 Hz, 2 rows per second (the spectrum
strip of the mockup went away after the first live run — Richard
2026-10-11: "spektrum dej pryč úplně, nech tam jen vodopád"). The things WSJT-X makes you cross-reference by hand are drawn in:
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
Everything is visible at 1519×1364; nothing scrolls away.

*Changed after the first live run (Richard, 2026-10-11):* three columns were
not ergonomic and made the window too wide — the layout is now **one
column**: the controls strip directly under the waterfall (row A: the DX
station as text — the program keeps his call and grid itself, typing them
by hand for a sked lives in the card's popover —, report, received, Log QSO;
row B: the
transmission card, Tx frequency with Hold / Tx = Rx, Enable TX, Halt TX,
Tune, Call CQ), and the two decode panes side by side beneath it; decode
lines and status items ellipsize so the window can be narrow (fits 1024 px).
Also: the six standard messages are not something to look at all the time — the sequencer picks
them ("málokdy jsem kroky přeskakoval, u WSJT-X jsem to skoro nepoužíval").
The Tx1–Tx6 list and the Auto sequence switch moved into a popover under the
card's `TxN · Hz` button; the panel keeps the partner row, the card, the
four TX buttons and the Tx-frequency row.

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
cc ftx_main_window_v2.c -o ftx-mock-v2 $(pkg-config --cflags --libs libadwaita-1) -lm
cc ftx_preferences.c -o ftx-mock-prefs $(pkg-config --cflags --libs libadwaita-1)
./ftx-mock-v2                   # waterfall scrolls, nothing else is live
MOCK_STATE=idle ./ftx-mock-v2   # receiving, no QSO
MOCK_PAGE=decoder ./ftx-mock-prefs   # station | radio | decoder | logging | display
```

## How the PNGs were made

Headless, without a compositor: GTK's Broadway backend is the display,
headless Chrome is its client and takes the picture, ImageMagick trims the
canvas to the window.

```sh
gtk4-broadwayd :21 &                                   # listens on TCP 8101
XDG_CONFIG_HOME=$(mktemp -d) GDK_BACKEND=broadway BROADWAY_DISPLAY=:21 ./ftx-mock-v2 &
sleep 5                                                # a heavy user theme needs the time
google-chrome-stable --headless=new --disable-gpu --hide-scrollbars \
  --window-size=1519,1364 --timeout=4000 --screenshot=ftx_main_window_v2_stock.png http://127.0.0.1:8101/
```

The empty `XDG_CONFIG_HOME` keeps the user's `gtk.css` out, so the picture is
stock Adwaita dark; leave it off for the user-theme variant.

Preference pages the same way with `./ftx-mock-prefs`, `MOCK_PAGE=<page>` and
`--window-size=800,860`. Three things that cost time to find out: the Broadway
daemon reports a small default screen until a browser connects and GTK clamps
a window's *default* size to it, so under Broadway the v2 mockup maximizes
itself into the browser canvas (the preferences mockup pins its size with a
hard size request, `pin_size_under_broadway`); Chrome's
`--virtual-time-budget` does not wait for WebSocket traffic, so it races the
Broadway canvas and yields blank pictures — use the real-time `--timeout`;
and `GtkWidgetPaintable` self-snapshots are unreliable without a Broadway
client, because frames are painted only when the client acknowledges them.

`ftx_main_window_v2_stock.png` shows stock Adwaita dark (rendered with an
empty `XDG_CONFIG_HOME`, so no user stylesheet is loaded).  The other two
PNGs are rendered with the author's own GTK stylesheet,
`~/.config/gtk-4.0/gtk.css` (USER priority, above application CSS), which
recolours every stock widget — black window, white active toggles, so the
red *Enable TX* becomes a white one.  That is the reason the TX state is
also carried by custom-drawn elements (the badge, the group border, the
period line, the `on air` tag), which a user stylesheet does not touch.
