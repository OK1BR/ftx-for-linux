/* ftx_crc14.h — 14-bit CRC of the FT8/FT4 77-bit payload (polynomial 0x2757).
 *
 * Ported by hand to C from WSJT-X v3.0.2 (git ccdfaf3),
 *   lib/crc14.cpp (Boost augmented_crc<14, 0x2757>: crc14, crc14_check),
 *   lib/ft8/get_crc14.f90 (the same division written out bitwise, used by
 *   decode174_91/osd174_91), and the byte packing of lib/ft8/encode174_91.f90
 *   and lib/ft8/chkcrc14a.f90.
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
#ifndef FTX_CRC14_H
#define FTX_CRC14_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* "Augmented" CRC-14, polynomial 0x2757, initial remainder 0, no final XOR,
 * bits consumed MSB first: the plain polynomial-division remainder of the
 * whole byte stream.  To compute a CRC the caller leaves the 14 CRC bit
 * positions zero; to check one the caller supplies the stream with the CRC
 * in place and expects a zero remainder.  Identical to WSJT-X crc14(). */
uint16_t ftx_crc14(const uint8_t *data, size_t len);

/* True when the remainder of the whole stream is zero (WSJT-X crc14_check). */
bool ftx_crc14_check(const uint8_t *data, size_t len);

/* FT8/FT4 message layout.  bits[] hold one 0/1 value per element.
 *
 * CRC of a 77-bit message: the 77 bits followed by 19 zero bits make a
 * 96-bit (12-byte) stream — exactly what encode174_91.f90 feeds crc14(). */
uint16_t ftx_crc14_message77(const uint8_t bits[77]);

/* Check a decoded 91-bit word (77 message bits + 14 CRC bits).  Returns true
 * when consistent — the test decode174_91/osd174_91 do with get_crc14 on
 * m96 = [bits 0..76][5 zeros][bits 77..90]. */
bool ftx_crc14_check91(const uint8_t bits[91]);

#endif /* FTX_CRC14_H */
