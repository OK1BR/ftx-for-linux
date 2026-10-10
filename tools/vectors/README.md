# Vector generators (dev only)

These programs are compiled against the WSJT-X v3.0.2 source tree
(`~/.local/opt/wsjtx`, tag v3.0.2, git ccdfaf3) and dump the vectors kept in
`tests/vectors/`. They are never built into the product.

## crc14_dump.cpp

Links WSJT-X's own `lib/crc14.cpp` (needs Boost headers and g++):

```sh
g++ -O1 -o crc14_dump crc14_dump.cpp ~/.local/opt/wsjtx/lib/crc14.cpp
./crc14_dump > crc14_vectors.txt      # then prepend the header comment
```

Groups: `A` 77-bit messages packed as `encode174_91.f90` does (200),
`B` raw byte strings of length 1..16 (64), `C` 82 payload bits + the CRC
in the last 14 bits, checked with `crc14_check` (50).

## pack77_dump.f90

Drives WSJT-X's own `pack77`/`unpack77` (module `packjt77`) over a scenario
file (`tools/vectors/pack77/*.txt`, syntax in the program header) and writes
`tests/vectors/pack77/<name>.vec`. One process per scenario: the module's hash
tables and my/dx call state evolve across lines exactly as inside WSJT-X, so
the gate replays each file in order through one fresh `ftx_pack77_ctx`.

```sh
L=~/.local/opt/wsjtx/lib
F="gfortran -O1 -finit-local-zero -finit-character=32 -J."
$F -c $L/77bit/packjt77.f90 && $F -c $L/fmtmsg.f90 && $F -c $L/chkcall.f90
$F -c pack77_dump.f90 && gfortran -o pack77_dump pack77_dump.o packjt77.o fmtmsg.o chkcall.o
./pack77_dump pack77/edge.txt > edge.vec
```

**Why the pinned init flags.** `packjt77.f90` reads a few locals before any
assignment on garbage inputs (`nserial` in `pack77_3`, `n12`/`c11` in
`pack77_4` when no `<...>` branch is taken, `npfx` in `pack77_06` for a suffix
longer than 3, `grid4` in `unpack77` when `to_grid4` fails). In WSJT-X that is
undefined behaviour; the oracle pins it to zero / blanks and the C port
initialises the same variables the same way. Everything that reaches the air
in normal operation never touches those paths. Note also that `pack77_3` can
return with `i3=3` without writing `c77` (report word unreadable), so `c77` is
an in/out buffer in both the Fortran and the port, and the gate keeps one
buffer across a scenario just like the driver does.
