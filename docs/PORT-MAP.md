# Port map — WSJT-X v3.0.2 (git ccdfaf3) → libftx

Reference tree: `~/.local/opt/wsjtx` pinned to tag v3.0.2 (dev only).
One line per module; status is **done** only when its bit-exact gate passes.

| libftx | WSJT-X source | vectors | status |
|---|---|---|---|
| `ftx_hash` | `lib/77bit/packjt77.f90` (`ihashcall`, `ihashcall_from_n8`) | `tests/unit/packjt77/test_packjt77_hash_vectors.f90` (13 calls × 10/12/22 bits) | done |
| `ftx_crc14` | `lib/crc14.cpp` (Boost `augmented_crc<14,0x2757>`), `lib/ft8/get_crc14.f90` | `tests/vectors/crc14_vectors.txt` from `tools/vectors/crc14_dump.cpp` linked with the WSJT-X `crc14.cpp` | done |
| `ftx_pack77` | `lib/77bit/packjt77.f90` (`pack77`, `unpack77`, all `pack77_*`, `pack28`/`unpack28`, hash tables, free text), `lib/chkcall.f90`, `lib/fmtmsg.f90` | `tests/vectors/pack77/*.vec`: 1 693 pack + 12 697 unpack cases replayed in order, from `tools/vectors/pack77_dump.f90` linked with the WSJT-X module (gfortran, pinned uninitialised locals) | done |
| `ftx_ldpc` | `lib/ft8/bpdecode174_91.f90`, `osd174_91.f90`, `encode174_91.f90`, `ldpc_174_91_c_*.f90` | codewords from an `encode174_91` driver (gfortran); BP/OSD on noisy LLRs vs the Fortran | — |
| `ftx_ft8_sync` | `lib/ft8/sync8.f90`, `sync8d.f90`, `ft8_downsample.f90` | — | — |
| `ftx_ft8` | `lib/ft8/ft8b.f90`, `lib/ft8_decode.f90`, `subtractft8.f90`, `ft8apset.f90` | `jt9 -8 -d 3` oracle on WAVs | — |
| `ftx_ft4` | `lib/ft4/*.f90`, `lib/ft4_decode.f90` | `jt9 -5 -d 3` oracle on WAVs | — |

State model: `packjt77.f90` carries module `save` arrays (hash tables 10/12/22
bits, recent calls, my/dx call) and a second `*var` copy of every routine
indexed by thread. We port the non-`var` set once and move all of that
state into an explicit context struct passed by the caller.
