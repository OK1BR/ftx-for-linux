/* ftx_ldpc.h — the (174,91) LDPC code of FT8/FT4: encoder, belief-propagation
 * decoder and ordered-statistics decoder.
 *
 * Ported by hand to C from WSJT-X v3.0.2 (git ccdfaf3),
 *   lib/ft8/encode174_91.f90, encode174_91_nocrc.f90, decode174_91.f90
 *   (hybrid BP/OSD), osd174_91.f90 (osd174_91, mrbencode91, nextpat91,
 *   boxit91, fetchit91), lib/indexx.f90, lib/platanh.f90.
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
 * Bits are one 0/1 value per uint8_t.  Soft inputs are log-likelihood ratios
 * (positive = bit 1) in single precision, as in the Fortran.  The OSD keeps
 * its generator matrices and pattern hash tables in ftx_ldpc_ctx (WSJT-X:
 * a `save`d allocatable and a COMMON block); the caller owns the context,
 * which is about 2.4 MB, so allocate it on the heap.
 */
#ifndef FTX_LDPC_H
#define FTX_LDPC_H

#include <stdbool.h>
#include <stdint.h>

#include "ftx_ldpc_tables.h"

#define FTX_OSD_MAXPAIRS 5000
#define FTX_OSD_FPSIZE   525001

typedef struct ftx_ldpc_ctx {
  /* OSD generator matrices for k = 77..91 (index k-77), built on first use */
  uint8_t gen[15][FTX_LDPC_K][FTX_LDPC_N];
  bool    gen_ready[15];
  /* boxit91 / fetchit91 state (COMMON /boxes/ and the saved lastpat, inext) */
  int32_t indexes[FTX_OSD_MAXPAIRS + 1][2];
  int32_t fp[FTX_OSD_FPSIZE];
  int32_t np[FTX_OSD_MAXPAIRS + 1];
  int32_t lastpat, inext;
  /* OSD scratch: the reduced generator matrix of the current call */
  uint8_t genmrb[FTX_LDPC_K][FTX_LDPC_N];
} ftx_ldpc_ctx;

void ftx_ldpc_init(ftx_ldpc_ctx *ctx);

/* 77 message bits -> 174-bit codeword (CRC14 appended, then LDPC parity). */
void ftx_ldpc_encode174_91(const uint8_t message77[77], uint8_t codeword[174]);
/* 91 bits (message + CRC as given) -> 174-bit codeword. */
void ftx_ldpc_encode174_91_nocrc(const uint8_t message91[91], uint8_t codeword[174]);

/* Hybrid decoder (decode174_91): belief propagation, then up to maxosd OSD
 * passes on the accumulated bit metrics.
 *   maxosd < 0  BP only;  = 0  BP then one OSD pass on the channel LLRs;
 *   maxosd > 0  BP then OSD on the metrics saved after BP iterations 1..maxosd (max 3)
 *   norder      OSD depth 0..6;  keff 77..91 = 77 + number of CRC bits used for detection
 *   apmask      1 marks a-priori bits whose LLR is taken as given
 * Returns ntype 1 (BP), 2 (OSD) or 0 (no decode, nharderror = -1); on success
 * message91/cw hold the decoded bits, nharderror the hard errors against the
 * LLR signs and dmin the soft distance. */
void ftx_ldpc_decode174_91(ftx_ldpc_ctx *ctx, const float llr[174], int keff, int maxosd, int norder,
                           const uint8_t apmask[174], uint8_t message91[91], uint8_t cw[174],
                           int *ntype, int *nharderror, float *dmin);

/* Ordered-statistics decoder alone (osd174_91).  nhardmin is negated when
 * the best codeword fails the CRC. */
void ftx_ldpc_osd174_91(ftx_ldpc_ctx *ctx, const float llr[174], int k, const uint8_t apmask[174],
                        int ndeep, uint8_t message91[91], uint8_t cw[174], int *nhardmin, float *dmin);

#endif /* FTX_LDPC_H */
