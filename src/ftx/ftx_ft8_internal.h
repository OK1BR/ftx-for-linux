/* ftx_ft8_internal.h — the FT8 decoder context and private helpers.
 * See ftx_ft8.h for provenance and license (GPL-3.0-or-later). */
#ifndef FTX_FT8_INTERNAL_H
#define FTX_FT8_INTERNAL_H

#include <complex.h>

#include "ftx_ft8.h"

#define FT8_JZ 62                         /* sync8: max lag +/- in 1/4 symbols */
#define FT8_NFFT_DS 192000                /* ft8_downsample long FFT            */
#define FT8_NFILT 4000                    /* subtractft8 filter length          */
#define FT8_NTAB 65536                    /* gen_ft8wave phase table            */

/* cmplx(a,b) without C11's CMPLXF: exact, no arithmetic on the parts */
#define FTX_CMPLXF(a, b) __builtin_complex((float)(a), (float)(b))

struct ftx_ft8_ctx {
  ftx_pack77_ctx pack77;
  ftx_ldpc_ctx   ldpc;

  /* FFT plans with their in-place buffers (four2a plans keyed by array) */
  ftx_fft fft_sym;      /* r2c 3840     sync8 symbol spectra                 */
  ftx_fft fft_base;     /* r2c 3840     get_spectrum_baseline                */
  ftx_fft fft_long;     /* r2c 192000   ft8_downsample, cached spectrum of dd */
  ftx_fft fft_ds;       /* c2c 3200 bwd ft8_downsample output                */
  ftx_fft fft_sym32;    /* c2c 32 fwd   ft8b symbol spectra                  */

  /* get_spectrum_baseline: Nuttall window, normalised */
  float gsb_window[FT8_NFFT1];

  /* sync8 work arrays (heap: 2.9 MB + 1 MB) */
  float *s;             /* s(i,j), i = 1..NH1 bin, j = 1..NHSYM step: s[(j-1)*NH1 + (i-1)] */
  float *sync2d;        /* sync2d(i,j), j = -JZ..JZ, stored lag-contiguous: [(i-1)*(2JZ+1) + j+JZ] */

  /* ft8_downsample taper */
  float taper[101];

  /* sync8d: the 7 Costas tone waveforms */
  float complex csync[7][32];
};

#endif /* FTX_FT8_INTERNAL_H */
