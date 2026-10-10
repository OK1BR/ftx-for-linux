/* ftx_pack77.h — the 77-bit FT8/FT4 message codec (all message types).
 *
 * Ported by hand to C from WSJT-X v3.0.2 (git ccdfaf3),
 *   lib/77bit/packjt77.f90 (module packjt77: pack77, unpack77, pack28,
 *   unpack28, split77, pack77_01/03/06/1/3/4/5, packtext77, unpacktext77,
 *   save_hash_call, hash10/12/22, add_call_to_recent_calls, to_grid4/6,
 *   to_grid, callok), lib/chkcall.f90 and lib/fmtmsg.f90.
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
 *
 * The Fortran keeps its hash tables and my/dx call in module `save` state
 * (one decoder per process).  Here all of it lives in ftx_pack77_ctx, which
 * the caller owns; several contexts may be used side by side.  Only the
 * original routine set is ported; WSJT-X's `*var` duplicates (per-thread
 * copies for its ft8var decoder) are the same algorithm with a thread index.
 */
#ifndef FTX_PACK77_H
#define FTX_PACK77_H

#include <stdbool.h>
#include <stdint.h>

#define FTX_MAXHASH   1000
#define FTX_MAXRECENT 10

/* Strings below are Fortran fixed fields: 13 characters, blank padded, plus a
 * terminating NUL for convenience.  An empty entry is 13 blanks. */
typedef struct ftx_pack77_ctx {
  char calls10[1024][14];              /* 10-bit hash table                  */
  char calls12[4096][14];              /* 12-bit hash table                  */
  char calls22[FTX_MAXHASH][14];       /* 22-bit table, most recent first    */
  int  ihash22[FTX_MAXHASH];           /* -1 = unused                        */
  int  nzhash;
  char recent_calls[FTX_MAXRECENT][14];
  char mycall13[14];                   /* own call (packjt77: mycall13)      */
  char dxcall13[14];                   /* DX call  (packjt77: dxcall13)      */
  char dxbase[7];                      /* base of the DX call, for "$DX"     */
  int  n28a, n28b;                     /* module scratch written by unpack77 */
  /* unpack77's own `save` variables */
  char mycall13_0[14], dxcall13_0[14];
  bool mycall13_set, dxcall13_set;
  int  hashmy10, hashmy12, hashmy22, hashdx10, hashdx12, hashdx22;
} ftx_pack77_ctx;

void ftx_pack77_init(ftx_pack77_ctx *ctx);
void ftx_pack77_set_mycall(ftx_pack77_ctx *ctx, const char *call);
void ftx_pack77_set_dxcall(ftx_pack77_ctx *ctx, const char *call);
void ftx_pack77_set_dxbase(ftx_pack77_ctx *ctx, const char *base);

/* fmtmsg: upper-case the message and collapse runs of blanks, in place
 * (what WSJT-X applies to every message before pack77). */
void ftx_fmtmsg(char *msg);

/* pack77: encode a message (first 37 characters are used) into 77 bits.
 * On entry *i3 and *n3 may carry a type hint (0/5 telemetry, 0/6 WSPR type 3;
 * pass -1/-1 for none); on return they hold the message type (i3=-1 never
 * happens: unknown text becomes free text, type 0.0).  c77 receives 77
 * characters '0'/'1' plus a NUL; like the Fortran it is an in/out buffer —
 * a field that overflowed its width is written as '*' and one rare path
 * (type 3 with an unreadable report) returns without rewriting it. */
void ftx_pack77(ftx_pack77_ctx *ctx, const char *msg, int *i3, int *n3, char c77[78]);

/* unpack77: decode 77 bits ('0'/'1' characters) into a message of at most
 * 37 characters (trailing blanks removed, NUL terminated; msg needs 38
 * bytes).  nrx=1 for a received message, 0 for one about to be sent.
 * Returns the Fortran unpk77_success flag. */
bool ftx_unpack77(ftx_pack77_ctx *ctx, const char *c77, int nrx, char msg[38]);

/* Convert between the character form and 0/1 bit arrays.  ftx_c77_to_bits
 * returns false if any character is not '0' or '1'. */
bool ftx_c77_to_bits(const char *c77, uint8_t bits[77]);
void ftx_bits_to_c77(const uint8_t bits[77], char c77[78]);

#endif /* FTX_PACK77_H */
