# Port map — WSJT-X v3.0.2 (git ccdfaf3) → libftx

Reference tree: `~/.local/opt/wsjtx` pinned to tag v3.0.2 (dev only).
One line per module; a module is **done** only when its bit-exact gate
against WSJT-X's own output passes (the gates, the vector generators and the
vectors are development material and are not part of this repository).

| libftx | WSJT-X source | status |
|---|---|---|
| `ftx_hash` | `lib/77bit/packjt77.f90` (`ihashcall`, `ihashcall_from_n8`) | done |
| `ftx_crc14` | `lib/crc14.cpp` (Boost `augmented_crc<14,0x2757>`), `lib/ft8/get_crc14.f90` | done |
| `ftx_pack77` | `lib/77bit/packjt77.f90` (`pack77`, `unpack77`, all `pack77_*`, `pack28`/`unpack28`, hash tables, free text), `lib/chkcall.f90`, `lib/fmtmsg.f90` | done |
| `ftx_ldpc` | `lib/ft8/encode174_91.f90`, `encode174_91_nocrc.f90`, `decode174_91.f90` (hybrid BP/OSD), `osd174_91.f90`, `ldpc_174_91_c_generator/parity.f90`, `lib/indexx.f90`, `lib/platanh.f90` | done |
| `ftx_fft` | `lib/four2a.f90` (FFTW3 plans, here planned once per context) | done |
| `ftx_ft8_sync` | `lib/ft8/sync8.f90`, `sync8d.f90`, `ft8_downsample.f90`, `twkfreq1.f90`, `get_spectrum_baseline.f90`, `lib/baseline.f90`, `nuttal_window.f90`, `pctile.f90`, `shell.f90`, `polyfit.f90`, `determ.f90` | done |
| `ftx_ft8_demod` | `lib/ft8/ft8b.f90` (ft8b, normalizebmet), `ft8apset.f90`, `genft8.f90` (tones), `gen_ft8wave.f90`, `subtractft8.f90`, `lib/peakup.f90`, `ft2/gfsk_pulse.f90`, stdcall | done |
| `ftx_ft8_decode` | `lib/ft8_decode.f90` (multi-pass driver, early decodes at nzhsym 41/47/50, subtraction, duplicates, jt9 line format), `lib/ft8/ft8_a7.f90` (`ft8_a7_save` table bookkeeping) | done |
| `ftx_ft8_a7a8` | `lib/ft8/ft8_a7.f90` (`ft8_a7d`, `getmsg`), `ft8_a8d.f90`, `genft8.f90`, `lib/smo121.f90`, `lib/db.f90` | done |
| `ftx_ft4` | `lib/ft4_decode.f90`, `lib/ft4/getcandidates4.f90`, `ft4_baseline.f90`, `ft4_downsample.f90`, `sync4d.f90`, `get_ft4_bitmetrics.f90`, `genft4.f90`, `gen_ft4wave.f90`, `subtractft4.f90` | done |

| transmit | `lib/ft8/genft8.f90`, `gen_ft8wave.f90` (real branch, as mainwindow.cpp transmits: 48 kHz, nsps 7680), `lib/ft4/genft4.f90`, `gen_ft4wave.f90` (48 kHz, nsps 2304) | done |

The app layer (`src/app/`, `src/ui/`) ports the QSO logic of the WSJT-X GUI
(C++/Qt, no bit-exact gate possible — verified by unit tests on the derived
behaviour and in practice):

| app | WSJT-X source | status |
|---|---|---|
| `app/messages` | `widgets/mainwindow.cpp` (`genStdMsgs`, `genCQMsg`, `stdCall`), `Radio.cpp` (`base_callsign`, `is_callsign`), `Decoder/decodedtext.cpp` (tokenizer `tokens_re`, `deCallAndGrid`, `report`), `lib/stdmsg.f90` | done (FT8/FT4 only, no contest exchanges) |
| `app/rx_engine` | `lib/decoder.f90` call sequence (nzhsym 41/47/50, `ltry_a8`) as `jt9` drives it | done |
| `ui/window` | `widgets/mainwindow.cpp` `processMessage` (SpecOp::NONE, FT8/FT4), the Rx-frequency pane rule of `readFromStdout`, `auto_sequence`, `write_all` (ALL.TXT) | RX side done; the TX gate of the sequencer comes with TX over TCI |

Not ported on purpose: `bpdecode174_91.f90` (no caller outside the ft8var
decoder in v3.0.2; `decode174_91` carries its own BP) and
`ldpc_174_91_c_colorder.f90` (only the old (174,87) encoder uses it).

State model: `packjt77.f90` carries module `save` arrays (hash tables 10/12/22
bits, recent calls, my/dx call) and a second `*var` copy of every routine
indexed by thread. We port the non-`var` set once and keep all of that
state in an explicit context struct passed by the caller.
