/* ftx_ft4.h — the FT4 decoder.
 *
 * Ported by hand to C from WSJT-X v3.0.2 (git ccdfaf3), lib/ft4_decode.f90,
 * lib/ft4/getcandidates4.f90, ft4_baseline.f90, ft4_downsample.f90,
 * sync4d.f90, get_ft4_bitmetrics.f90, genft4.f90, gen_ft4wave.f90,
 * subtractft4.f90, ft4_params.f90 (the shared helpers come from the FT8 port).
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
 * One ftx_ft4_ctx is one independent decoder (heap, a few MB).  A period is
 * 7.5 s; the decoder takes FT4_NMAX = 72576 samples at 12000 S/s (6.048 s),
 * exactly what jt9 feeds it.
 */
#ifndef FTX_FT4_H
#define FTX_FT4_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ftx_fft.h"

/* ft4_params.f90 */
#define FT4_KK    91
#define FT4_ND    87                 /* data symbols                        */
#define FT4_NS    16                 /* sync symbols (4 Costas 4x4)         */
#define FT4_NN    103                /* sync + data symbols                 */
#define FT4_NN2   105                /* with ramp up/down                   */
#define FT4_NSPS  576                /* samples per symbol at 12000 S/s     */
#define FT4_NZ    (FT4_NSPS * FT4_NN)
#define FT4_NZ2   (FT4_NSPS * FT4_NN2)
#define FT4_NMAX  (21 * 3456)        /* 72576 samples                       */
#define FT4_NFFT1 2304
#define FT4_NH1   (FT4_NFFT1 / 2)
#define FT4_NSTEP FT4_NSPS
#define FT4_NHSYM ((FT4_NMAX - FT4_NFFT1) / FT4_NSTEP)
#define FT4_NDOWN 18
#define FT4_NSS   (FT4_NSPS / FT4_NDOWN)    /* 32 samples per symbol after downsampling */
#define FT4_NDMAX (FT4_NMAX / FT4_NDOWN)    /* 4032 */
#define FT4_MAXCAND 200

typedef struct ftx_ft4_ctx ftx_ft4_ctx;

ftx_ft4_ctx *ftx_ft4_ctx_new(void);
void ftx_ft4_ctx_free(ftx_ft4_ctx *ctx);

typedef struct ftx_ft4_candidate {
  float freq;   /* Hz   */
  float snr;    /* candidate strength (relative to the baseline) */
} ftx_ft4_candidate;

/* getcandidates4: candidates between fa and fb Hz (sync >= syncmin), the
 * ones within 20 Hz of nfqso first.  savg/sbase receive the average
 * spectrum (dB inside the fitted range) and the linear baseline, FT4_NH1
 * values each. */
int ftx_ft4_candidates(ftx_ft4_ctx *ctx, const float *dd, float fa, float fb, float syncmin, int nfqso,
                       int maxcand, float *savg, ftx_ft4_candidate *cand, float *sbase);

/* ft4_downsample: mix f0 to baseband at 666.67 S/s, FT4_NDMAX samples.
 * *newdata true recomputes the spectrum of dd first (cleared by the caller,
 * as in the Fortran). */
void ftx_ft4_downsample(ftx_ft4_ctx *ctx, const float *dd, bool newdata, float f0, fftwf_complex *c);

/* sync4d: sync power of a downsampled signal starting at i0, with the
 * frequency tweak ctwk (2*FT4_NSS values). */
float ftx_ft4_sync4d(ftx_ft4_ctx *ctx, const fftwf_complex *cd0, int i0, const fftwf_complex *ctwk);

/* Decoder settings (WSJT-X names). */
typedef struct ftx_ft4_params {
  int  nQSOProgress;
  int  nfqso;
  int  nfa, nfb;
  int  ndepth;         /* 1: one pass, no subtraction; 2: 3 passes BP; 3: 3 passes BP+OSD */
  int  ncontest;
  bool lapcqonly;
  char mycall[13];
  char hiscall[13];
} ftx_ft4_params;

typedef void (*ftx_ft4_decode_cb)(void *user, float sync, int snr, float dt, float freq,
                                  const char *decoded, int nap, float qual);

/* ftx_ft4_decode: decode one period (FT4_NMAX int16 samples). */
void ftx_ft4_decode(ftx_ft4_ctx *ctx, const int16_t *iwave, const ftx_ft4_params *p,
                    ftx_ft4_decode_cb cb, void *user);

/* --- transmit --- */

/* Tones for a message (genft4, ichk = 0); msgsent as it will be decoded.
 * Returns false for an unpackable message. */
bool ftx_ft4_tx_tones(ftx_ft4_ctx *ctx, const char *msg, char msgsent[38], int i4tone[103]);

/* The transmit waveform (gen_ft4wave, real output): nwave = 105*nsps
 * samples (103 symbols plus ramp up/down) at fsample, nsps samples per
 * symbol (WSJT-X: 48000 and 2304), carrier f0 Hz. */
void ftx_ft4_tx_wave(ftx_ft4_ctx *ctx, const int i4tone[103], int nsps, float fsample, float f0,
                     float *wave, int nwave);

/* jt9's FT4 line: like FT8's but with " + " as the mode mark. */
void ftx_ft4_format_line(char *buf, size_t len, int nutc, int snr, float dt, float freq,
                         const char *decoded, int nap, float qual);

#endif /* FTX_FT4_H */
