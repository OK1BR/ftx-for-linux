/* ftx_crc14.c — 14-bit CRC of the FT8/FT4 77-bit payload (polynomial 0x2757).
 *
 * Ported by hand to C from WSJT-X v3.0.2 (git ccdfaf3),
 *   lib/crc14.cpp (Boost augmented_crc<14, 0x2757>: crc14, crc14_check),
 *   lib/ft8/get_crc14.f90, lib/ft8/encode174_91.f90, lib/ft8/chkcrc14a.f90.
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
#include "ftx_crc14.h"

#include <string.h>

#define FTX_CRC14_POLY 0x2757u   /* x^14 + x^13 + x^10 + x^9 + x^8 + x^6 + x^4 + x^2 + x + 1 */

uint16_t ftx_crc14(const uint8_t *data, size_t len)
{
  /* Bitwise long division: shift each message bit into a 14-bit register;
   * whenever a 1 falls out of the top, subtract (xor) the polynomial.  This
   * is get_crc14.f90's loop and what Boost's augmented_crc computes with a
   * byte table. */
  uint32_t rem = 0;
  for (size_t i = 0; i < len; i++) {
    for (int bit = 7; bit >= 0; bit--) {
      rem = (rem << 1) | ((data[i] >> bit) & 1u);
      if (rem & 0x4000u)
        rem ^= 0x4000u | FTX_CRC14_POLY;
    }
  }
  return (uint16_t)rem;
}

bool ftx_crc14_check(const uint8_t *data, size_t len)
{
  return ftx_crc14(data, len) == 0;
}

/* Pack n 0/1 bits MSB-first into bytes (zero-filled beyond n). */
static void ftx_pack_bits(const uint8_t *bits, size_t n, uint8_t *bytes, size_t nbytes)
{
  memset(bytes, 0, nbytes);
  for (size_t i = 0; i < n; i++)
    if (bits[i])
      bytes[i / 8] |= (uint8_t)(0x80u >> (i % 8));
}

uint16_t ftx_crc14_message77(const uint8_t bits[77])
{
  uint8_t bytes[12];
  ftx_pack_bits(bits, 77, bytes, sizeof bytes);  /* 77 bits + 19 zeros = 96 */
  return ftx_crc14(bytes, sizeof bytes);
}

bool ftx_crc14_check91(const uint8_t bits[91])
{
  uint8_t m96[96];
  memset(m96, 0, sizeof m96);
  memcpy(m96, bits, 77);              /* m96(1:77)  = message       */
  memcpy(m96 + 82, bits + 77, 14);    /* m96(83:96) = received CRC  */
  uint8_t bytes[12];
  ftx_pack_bits(m96, 96, bytes, sizeof bytes);
  return ftx_crc14_check(bytes, sizeof bytes);
}
