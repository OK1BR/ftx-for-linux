/* ftx_ft8.h — the FT8 decoder: candidate search, downsampling, demodulation,
 * a-priori decoding, signal subtraction and the multi-pass driver.
 *
 * Ported by hand to C from WSJT-X v3.0.2 (git ccdfaf3),
 *   lib/ft8_decode.f90, lib/ft8/sync8.f90, sync8d.f90, ft8_downsample.f90,
 *   twkfreq1.f90, get_spectrum_baseline.f90, ft8b.f90, ft8apset.f90,
 *   genft8.f90, gen_ft8wave.f90, subtractft8.f90, ft8_params.f90,
 *   lib/baseline.f90, nuttal_window.f90, pctile.f90, shell.f90, polyfit.f90,
 *   determ.f90, peakup.f90, ft2/gfsk_pulse.f90, qra/q65/q65_set_list.f90 (stdcall).
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
 * Everything WSJT-X keeps in module/`save` variables lives in ftx_ft8_ctx;
 * one context is one independent decoder (about 15 MB, heap allocated with
 * ftx_ft8_ctx_new).  Sample format throughout: 12000 S/s, a period is
 * 15 s = 180000 samples.
 */
#ifndef FTX_FT8_H
#define FTX_FT8_H

#include <stdbool.h>
#include <stdint.h>

#include "ftx_fft.h"
#include "ftx_ldpc.h"
#include "ftx_pack77.h"

/* ft8_params.f90 */
#define FT8_KK    91                 /* information bits (77 + CRC14)        */
#define FT8_ND    58                 /* data symbols                         */
#define FT8_NS    21                 /* sync symbols (3 Costas 7x7)          */
#define FT8_NN    79                 /* channel symbols                      */
#define FT8_NSPS  1920               /* samples per symbol at 12000 S/s      */
#define FT8_NZ    (FT8_NSPS * FT8_NN) /* samples in the 79-symbol waveform   */
#define FT8_NMAX  (15 * 12000)       /* samples in one period                */
#define FT8_NFFT1 (2 * FT8_NSPS)     /* FFT length for symbol spectra        */
#define FT8_NH1   (FT8_NFFT1 / 2)
#define FT8_NSTEP (FT8_NSPS / 4)     /* rough time-sync step                 */
#define FT8_NHSYM (FT8_NMAX / FT8_NSTEP - 3)  /* number of symbol spectra    */
#define FT8_NDOWN 60                 /* downsample factor                    */
#define FT8_NP2   2812               /* downsampled samples of one waveform  */
#define FT8_NDS   3200               /* downsampled buffer length            */
#define FT8_MAXCAND 1000
#define FT8_MAX_EARLY 200

typedef struct ftx_ft8_candidate {
  float freq;   /* Hz          */
  float dt;     /* s (sync8 convention: nominal start at 0.5 s is dt = 0) */
  float sync;
} ftx_ft8_candidate;

typedef struct ftx_ft8_ctx ftx_ft8_ctx;

ftx_ft8_ctx *ftx_ft8_ctx_new(void);
void ftx_ft8_ctx_free(ftx_ft8_ctx *ctx);

/* --- building blocks (sync8.f90 and friends), exposed for reuse and gating --- */

/* get_spectrum_baseline: average symbol spectrum of dd, fitted baseline in
 * dB per 3.125 Hz bin (sbase[0] is bin 1, as the Fortran sbase(1:NH1)).
 * nfa/nfb are clamped in place as the Fortran does. */
void ftx_ft8_spectrum_baseline(ftx_ft8_ctx *ctx, const float *dd, int *nfa, int *nfb, float *sbase);

/* sync8: coarse candidate search over nfa..nfb Hz.  Returns ncand; sbase
 * (FT8_NH1 values) is filled too. */
int ftx_ft8_sync8(ftx_ft8_ctx *ctx, const float *dd, int *nfa, int *nfb, float syncmin, int nfqso,
                  int maxcand, ftx_ft8_candidate *cand, float *sbase);

/* ft8_downsample: mix f0 to baseband, 200 S/s complex, FT8_NDS samples.
 * *newdat tells it to recompute the long spectrum of dd first; it is
 * cleared afterwards (caller semantics as in the Fortran). */
void ftx_ft8_downsample(ftx_ft8_ctx *ctx, const float *dd, bool *newdat, float f0, fftwf_complex *c1);

/* sync8d: Costas sync power of a downsampled signal starting at sample i0;
 * ctwk (32 values) is applied when itwk = 1. */
float ftx_ft8_sync8d(ftx_ft8_ctx *ctx, const fftwf_complex *cd0, int i0, const fftwf_complex *ctwk, int itwk);

/* twkfreq1: multiply by a polynomial-phase chirp, a[0..4] in Hz (and Hz per
 * normalised Legendre term); in place allowed. */
void ftx_ft8_twkfreq1(const fftwf_complex *ca, int npts, float fsample, const float a[5], fftwf_complex *cb);

/* --- demodulation and decoding of one candidate (ft8b.f90 and friends) --- */

/* ft8apset: a-priori symbols from my call and the DX call (ncontest as WSJT-X:
 * 0 none, 1 NA VHF, 2 EU VHF, 3 Field Day, 4 RTTY RU, 5 WW Digi, 6 Fox, 7 Hound,
 * 8 ARRL Digi).  apsym[58] and aph10[10] receive +1/-1 bits; apsym[0],
 * apsym[29], aph10[0] carry the sentinel 99 when unavailable.  Updates the
 * hash tables of the context's pack77 state like the Fortran. */
void ftx_ft8apset(ftx_ft8_ctx *ctx, const char *mycall, const char *hiscall, int ncontest,
                  int apsym[58], int aph10[10]);

/* Tones (79) for a 77-bit message (genft8's get_ft8_tones_from_77bits). */
void ftx_ft8_tones_from_77bits(const uint8_t message77[77], int itone[79]);

/* gen_ft8wave, complex output: GFSK waveform of nsym tones at nsps samples
 * per symbol, bandwidth-time product bt, sample rate fsample, carrier f0;
 * nwave samples written (nwave <= nsym*nsps). */
void ftx_gen_ft8wave(ftx_ft8_ctx *ctx, const int *itone, int nsym, int nsps, float bt, float fsample,
                     float f0, fftwf_complex *cwave, int nwave);

/* subtractft8: remove the decoded signal (tones itone, frequency f0, start
 * dt in the ft8b convention) from dd in place; lrefinedt refines dt first
 * and skips the subtraction when no clean minimum is found. */
void ftx_ft8_subtract(ftx_ft8_ctx *ctx, float *dd, const int itone[79], float f0, float dt, bool lrefinedt);

/* Decoder settings shared by ft8b and the pass driver (WSJT-X names). */
typedef struct ftx_ft8_params {
  int  nQSOProgress;   /* 0..5, selects the AP pass types                 */
  int  nfqso;          /* Rx frequency (Hz), AP window centre             */
  int  nftx;           /* Tx frequency (Hz)                               */
  int  ndepth;         /* 1, 2 or 3                                       */
  int  nzhsym;         /* 41, 47 or 50 (how much of the period is in)     */
  int  napwid;         /* AP window half width (Hz), WSJT-X uses 75       */
  int  ncontest;       /* see ftx_ft8apset                                */
  bool lapon;          /* AP decoding enabled                             */
  bool lapcqonly;      /* AP for CQ only                                  */
  bool nagain;         /* "decode again" at nfqso                         */
  char mycall[13];     /* blank-padded or NUL-terminated, up to 12 chars  */
  char hiscall[13];
  char hisgrid[7];
} ftx_ft8_params;

/* ft8b: demodulate and decode one candidate at (*f1, *xdt) — both refined
 * on return; xdt uses sync8's convention (nominal start = 0.5 s).  On a
 * decode nbadcrc = 0 and msg37, xsnr, nharderrors, dmin, iaptype (0 = no
 * AP, 1..6 = AP type), ipass (decoding pass) and itone are set; if
 * lsubtract the signal is subtracted from dd in place.  *newdat is the
 * caller's flag for ftx_ft8_downsample (cleared on the first candidate of
 * a pass, as in WSJT-X). */
void ftx_ft8b(ftx_ft8_ctx *ctx, float *dd, bool *newdat, const ftx_ft8_params *p, bool lsubtract,
              int imetric, float *f1, float *xdt, float xbase, const int apsym[58], const int aph10[10],
              int *nharderrors, float *dmin, int *nbadcrc, int *iaptype, int *ipass, char msg37[38],
              float *xsnr, int itone[79]);

#endif /* FTX_FT8_H */
