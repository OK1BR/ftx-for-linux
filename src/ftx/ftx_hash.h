/* ftx_hash.h — callsign hashes (10/12/22 bits) of the FT8/FT4 77-bit protocol.
 *
 * Ported by hand to C from WSJT-X v3.0.2 (git ccdfaf3),
 *   lib/77bit/packjt77.f90: ihashcall, ihashcall_from_n8.
 * WSJT-X is Copyright (C) 2001-2026 by Joe Taylor, K1JT, Bill Somerville,
 * G4WJS, Steve Franke, K9AN, Nico Palermo, IV3NWV, Uwe Risse, DG2YCB,
 * Brian Moran, N9ADG, Roger Rehr, W3SZ, John Nelson, G4KLA, Charlie
 * Suckling, DL3WDG, and Terrell Deppe, KJ5HST (widgets/about.cpp).
 * https://github.com/WSJTX/wsjtx
 * This port: Copyright (C) 2026 Richard Fakenberg, OK1BR.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.  It is distributed WITHOUT ANY WARRANTY; see the GNU
 * General Public License for details: <https://www.gnu.org/licenses/>.
 */
#ifndef FTX_HASH_H
#define FTX_HASH_H

#include <stdint.h>

/* Hash of a callsign to its top m bits (m = 10, 12 or 22).
 *
 * `call` is a NUL-terminated string; only its first 11 characters count and
 * a shorter string is blank-padded, exactly like the Fortran character*13
 * field.  Alphabet " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/" (base 38); a
 * character outside it contributes -1, as Fortran index()-1 does.  Returns
 * the top m bits of the low 64 bits of 47055833459 * n (n = base-38 value).
 */
int ftx_ihashcall(const char *call, int m);

/* Same, from the base-38 value directly (the Fortran ihashcall_from_n8). */
int ftx_ihashcall_from_n8(int64_t n8, int m);

#endif /* FTX_HASH_H */
