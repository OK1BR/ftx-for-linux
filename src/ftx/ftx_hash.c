/* ftx_hash.c — callsign hashes (10/12/22 bits) of the FT8/FT4 77-bit protocol.
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
#include "ftx_hash.h"

#include <string.h>

static const char ftx_hash_alphabet[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/";

int ftx_ihashcall(const char *call, int m)
{
  size_t len = strlen(call);
  int64_t n8 = 0;
  for (int i = 0; i < 11; i++) {
    char ch = (size_t)i < len ? call[i] : ' ';
    const char *p = ch != '\0' ? strchr(ftx_hash_alphabet, ch) : NULL;
    int j = p != NULL ? (int)(p - ftx_hash_alphabet) : -1; /* index()-1 */
    n8 = 38 * n8 + j;
  }
  return ftx_ihashcall_from_n8(n8, m);
}

int ftx_ihashcall_from_n8(int64_t n8, int m)
{
  /* Return the top m bits of the low 64 bits of 47055833459*n8.  The
   * Fortran does the product in 16-bit limbs (2419 + 62655*2^16 + 10*2^32
   * = 47055833459) to stay within signed 64-bit arithmetic; the limb
   * arithmetic is kept verbatim so that the result is identical for every
   * input, including a negative n8 from a character outside the alphabet
   * (Fortran mod() and C % both truncate toward zero). */
  static const int64_t hash_factor[4] = { 2419, 62655, 10, 0 };
  int64_t b[4], p[4];
  int64_t x = n8;
  for (int i = 0; i < 4; i++) {
    b[i] = x % 65536;
    x /= 65536;
  }
  int64_t carry = 0;
  for (int i = 0; i < 4; i++) {
    int64_t term = carry;
    for (int j = 0; j <= i; j++)
      term += hash_factor[j] * b[i - j];
    p[i] = term % 65536;
    carry = term / 65536;
  }
  if (m <= 16)
    return (int)(p[3] / ((int64_t)1 << (16 - m)));
  return (int)(p[3] * ((int64_t)1 << (m - 16)) + p[2] / ((int64_t)1 << (32 - m)));
}
