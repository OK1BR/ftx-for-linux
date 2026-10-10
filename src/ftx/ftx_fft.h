/* ftx_fft.h — FFTW3 (single precision) plans for the FT8/FT4 decoders.
 *
 * Replaces WSJT-X v3.0.2 lib/four2a.f90 (a plan cache keyed by length,
 * direction, form and array address).  Here every transform the decoders
 * need is planned once, in place, on buffers owned by the context, so that
 * decoding itself never touches the FFTW planner (which is not thread safe
 * unless fftwf_make_planner_thread_safe() has been called).
 * Copyright (C) 2026 Richard Fakenberg, OK1BR.  GPL-3.0-or-later; see
 * <https://www.gnu.org/licenses/>.  No warranty.
 */
#ifndef FTX_FFT_H
#define FTX_FFT_H

#include <fftw3.h>

/* One in-place plan on one buffer.  The buffer is fftwf_malloc()ed by
 * ftx_fft_plan_* and belongs to the plan; r2c/c2r buffers hold n+2 floats
 * (n/2+1 complex values), c2c buffers n complex values. */
typedef struct ftx_fft {
  fftwf_plan plan;
  int        n;
  float     *r;         /* real view (r2c / c2r) */
  fftwf_complex *c;     /* complex view */
} ftx_fft;

/* FFTW_ESTIMATE, in place.  isign -1 = forward, +1 = backward (as four2a). */
void ftx_fft_plan_r2c(ftx_fft *f, int n);            /* four2a(x, n, 1, -1, 0) */
void ftx_fft_plan_c2r(ftx_fft *f, int n);            /* four2a(x, n, 1, +1, -1) */
void ftx_fft_plan_c2c(ftx_fft *f, int n, int isign); /* four2a(c, n, 1, isign, 1) */
void ftx_fft_exec(const ftx_fft *f);
void ftx_fft_free(ftx_fft *f);

#endif /* FTX_FFT_H */
