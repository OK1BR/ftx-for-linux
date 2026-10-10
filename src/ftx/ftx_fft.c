/* ftx_fft.c — FFTW3 single-precision plans for the decoders (see ftx_fft.h).
 * Copyright (C) 2026 Richard Fakenberg, OK1BR.  GPL-3.0-or-later. */
#include "ftx_fft.h"

#include <stdlib.h>
#include <string.h>

static void alloc_buf(ftx_fft *f, size_t nfloats)
{
  f->r = fftwf_malloc(nfloats * sizeof(float));
  if (f->r == NULL) abort();
  memset(f->r, 0, nfloats * sizeof(float));
  f->c = (fftwf_complex *)f->r;
}

void ftx_fft_plan_r2c(ftx_fft *f, int n)
{
  f->n = n;
  alloc_buf(f, (size_t)n + 2);
  f->plan = fftwf_plan_dft_r2c_1d(n, f->r, f->c, FFTW_ESTIMATE);
}

void ftx_fft_plan_c2r(ftx_fft *f, int n)
{
  f->n = n;
  alloc_buf(f, (size_t)n + 2);
  f->plan = fftwf_plan_dft_c2r_1d(n, f->c, f->r, FFTW_ESTIMATE);
}

void ftx_fft_plan_c2c(ftx_fft *f, int n, int isign)
{
  f->n = n;
  alloc_buf(f, 2 * (size_t)n);
  f->plan = fftwf_plan_dft_1d(n, f->c, f->c, isign < 0 ? FFTW_FORWARD : FFTW_BACKWARD, FFTW_ESTIMATE);
}

void ftx_fft_exec(const ftx_fft *f)
{
  fftwf_execute(f->plan);
}

void ftx_fft_free(ftx_fft *f)
{
  if (f->plan != NULL) fftwf_destroy_plan(f->plan);
  fftwf_free(f->r);
  f->plan = NULL;
  f->r = NULL;
  f->c = NULL;
}
