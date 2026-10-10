/* ftx_ft8_sync.c — FT8 candidate search and downsampling.
 *
 * Ported by hand to C from WSJT-X v3.0.2 (git ccdfaf3), lib/ft8/sync8.f90,
 * sync8d.f90, ft8_downsample.f90, twkfreq1.f90, get_spectrum_baseline.f90,
 * lib/baseline.f90, nuttal_window.f90, pctile.f90, shell.f90, polyfit.f90,
 * determ.f90.  See ftx_ft8.h for the WSJT-X copyright and the GPL-3.0-or-later
 * license notice.  This port: Copyright (C) 2026 Richard Fakenberg, OK1BR.
 *
 * Porting notes: single precision throughout except baseline/polyfit/determ
 * (REAL*8 in the Fortran); Fortran sum()/maxloc() semantics reproduced
 * (sequential sums, first maximum, NaN skipped).
 */
#include <complex.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ftx_ft8_internal.h"
#include "ftx_internal.h"

#define NH1   FT8_NH1
#define NHSYM FT8_NHSYM
#define JZ    FT8_JZ

static const int icos7[7] = { 3, 1, 4, 0, 6, 5, 2 };   /* Costas 7x7 tone pattern */

/* ------------------------------------------------------------------------
 * Context
 * ---------------------------------------------------------------------- */
ftx_ft8_ctx *ftx_ft8_ctx_new(void)
{
  ftx_ft8_ctx *ctx = calloc(1, sizeof *ctx);
  if (ctx == NULL) return NULL;
  ftx_pack77_init(&ctx->pack77);
  ftx_ldpc_init(&ctx->ldpc);

  ftx_fft_plan_r2c(&ctx->fft_sym, FT8_NFFT1);
  ftx_fft_plan_r2c(&ctx->fft_base, FT8_NFFT1);
  ftx_fft_plan_r2c(&ctx->fft_long, FT8_NFFT_DS);
  ftx_fft_plan_c2c(&ctx->fft_ds, FT8_NDS, +1);
  ftx_fft_plan_c2c(&ctx->fft_sym32, 32, -1);

  ctx->s = calloc((size_t)NH1 * NHSYM, sizeof(float));
  ctx->sync2d = calloc((size_t)NH1 * (2 * JZ + 1), sizeof(float));
  if (ctx->s == NULL || ctx->sync2d == NULL || !ftx_ft8_demod_init(ctx)) {
    ftx_ft8_ctx_free(ctx);
    return NULL;
  }

  /* get_spectrum_baseline: window=nuttal; window=window/sum(window)*NSPS*2/300.0 */
  {
    const float pi = 4.0f * atanf(1.0f);
    const float a0 = 0.3635819f, a1 = -0.4891775f, a2 = 0.1365995f, a3 = -0.0106411f;
    int n = FT8_NFFT1;
    float sum = 0.0f;
    for (int i = 1; i <= n; i++) {
      float w = a0 + a1 * cosf(2 * pi * (i - 1) / (float)n) + a2 * cosf(4 * pi * (i - 1) / (float)n) +
                a3 * cosf(6 * pi * (i - 1) / (float)n);
      ctx->gsb_window[i - 1] = w;
    }
    for (int i = 0; i < n; i++) sum += ctx->gsb_window[i];
    for (int i = 0; i < n; i++) ctx->gsb_window[i] = ctx->gsb_window[i] / sum * FT8_NSPS * 2 / 300.0f;
  }

  /* ft8_downsample: taper(i)=0.5*(1.0+cos(i*pi/100)) */
  {
    const float pi = 4.0f * atanf(1.0f);
    for (int i = 0; i <= 100; i++) ctx->taper[i] = 0.5f * (1.0f + cosf(i * pi / 100));
  }

  /* sync8d: csync(i,j)=cmplx(cos(phi),sin(phi)), phi advanced by twopi*icos7(i)/32 */
  {
    const float twopi = 8.0f * atanf(1.0f);
    for (int i = 0; i < 7; i++) {
      float phi = 0.0f;
      float dphi = twopi * icos7[i] / 32.0f;
      for (int j = 0; j < 32; j++) {
        ctx->csync[i][j] = FTX_CMPLXF(cosf(phi), sinf(phi));
        phi = fmodf(phi + dphi, twopi);
      }
    }
  }
  return ctx;
}

void ftx_ft8_ctx_free(ftx_ft8_ctx *ctx)
{
  if (ctx == NULL) return;
  ftx_fft_free(&ctx->fft_sym);
  ftx_fft_free(&ctx->fft_base);
  ftx_fft_free(&ctx->fft_long);
  ftx_fft_free(&ctx->fft_ds);
  ftx_fft_free(&ctx->fft_sym32);
  free(ctx->s);
  free(ctx->sync2d);
  ftx_ft8_demod_free(ctx);
  free(ctx);
}

/* ------------------------------------------------------------------------
 * baseline.f90 and its helpers (REAL*8 unless noted)
 * ---------------------------------------------------------------------- */
static void shell_sort(int n, float *a)                /* shell.f90 */
{
  int inc = 1;
  do inc = 3 * inc + 1; while (inc <= n);
  do {
    inc /= 3;
    for (int i = inc + 1; i <= n; i++) {
      float v = a[i - 1];
      int j = i;
      while (a[j - inc - 1] > v) {
        a[j - 1] = a[j - inc - 1];
        j -= inc;
        if (j <= inc) break;
      }
      a[j - 1] = v;
    }
  } while (inc > 1);
}

static void pctile(const float *x, int npts, int npct, float *xpct)   /* pctile.f90 */
{
  if (npts < 0 || npct < 0 || npct > 100) {
    *xpct = 1.0f;
    return;
  }
  float *tmp = malloc((size_t)(npts > 0 ? npts : 1) * sizeof(float));
  memcpy(tmp, x, (size_t)npts * sizeof(float));
  shell_sort(npts, tmp);
  int j = (int)lroundf((float)npts * 0.01f * (float)npct);
  if (j < 1) j = 1;
  if (j > npts) j = npts;
  *xpct = tmp[j - 1];
  free(tmp);
}

static double determ(double array[10][10], int norder)     /* determ.f90 (array is 1-based (k,j) → [k-1][j-1]) */
{
  double det = 1.0;
  for (int k = 1; k <= norder; k++) {
    if (array[k - 1][k - 1] == 0) {
      int j;
      for (j = k; j <= norder; j++)
        if (array[k - 1][j - 1] != 0) break;
      if (j > norder) return 0.0;
      for (int i = k; i <= norder; i++) {
        double s8 = array[i - 1][j - 1];
        array[i - 1][j - 1] = array[i - 1][k - 1];
        array[i - 1][k - 1] = s8;
      }
      det = -1.0 * det;
    }
    det = det * array[k - 1][k - 1];
    if (k < norder) {
      for (int i = k + 1; i <= norder; i++)
        for (int j = k + 1; j <= norder; j++)
          array[i - 1][j - 1] = array[i - 1][j - 1] - array[i - 1][k - 1] * array[k - 1][j - 1] / array[k - 1][k - 1];
    }
  }
  return det;
}

static void polyfit(const double *x, const double *y, int npts, int nterms, double *a)   /* polyfit.f90, mode 0 */
{
  double sumx[10] = { 0 }, sumy[10] = { 0 }, array[10][10];
  int nmax = 2 * nterms - 1;
  for (int i = 0; i < npts; i++) {
    double xi = x[i], yi = y[i];
    double weight = 1;
    double xterm = weight;
    for (int n = 1; n <= nmax; n++) {
      sumx[n - 1] += xterm;
      xterm *= xi;
    }
    double yterm = weight * yi;
    for (int n = 1; n <= nterms; n++) {
      sumy[n - 1] += yterm;
      yterm *= xi;
    }
  }
  for (int j = 1; j <= nterms; j++)
    for (int k = 1; k <= nterms; k++) array[j - 1][k - 1] = sumx[j + k - 2];
  double delta = determ(array, nterms);
  if (delta == 0) {
    for (int l = 0; l < nterms; l++) a[l] = 0.0;
    return;
  }
  for (int l = 1; l <= nterms; l++) {
    for (int j = 1; j <= nterms; j++) {
      for (int k = 1; k <= nterms; k++) array[j - 1][k - 1] = sumx[j + k - 2];
      array[j - 1][l - 1] = sumy[j - 1];
    }
    a[l - 1] = determ(array, nterms) / delta;
  }
}

static void baseline(float *s, int nfa, int nfb, float *sbase)   /* baseline.f90; s is modified (dB) */
{
  const int nseg = 10, npct = 10;
  double x[1000], y[1000], a[5];
  double df = 12000.0 / 3840.0;
  int ia = (int)lround(nfa / df);
  if (ia < 1) ia = 1;
  int ib = (int)lround(nfb / df);
  for (int i = ia; i <= ib; i++) s[i - 1] = 10.0f * log10f(s[i - 1]);

  int nterms = 5;
  int nlen = (ib - ia + 1) / nseg;
  int i0 = (ib - ia + 1) / 2;
  int k = 0;
  for (int n = 1; n <= nseg; n++) {
    int ja = ia + (n - 1) * nlen;
    int jb = ja + nlen - 1;
    float base;
    pctile(&s[ja - 1], nlen, npct, &base);
    for (int i = ja; i <= jb; i++) {
      if (s[i - 1] <= base) {
        if (k < 1000) k++;
        x[k - 1] = i - i0;
        y[k - 1] = s[i - 1];
      }
    }
  }
  for (int i = 0; i < 5; i++) a[i] = 0.0;
  polyfit(x, y, k, nterms, a);
  for (int i = 0; i < NH1; i++) sbase[i] = 0.0f;
  for (int i = ia; i <= ib; i++) {
    double t = i - i0;
    sbase[i - 1] = (float)(a[0] + t * (a[1] + t * (a[2] + t * (a[3] + t * (a[4])))) + (double)0.65f);
  }
}

void ftx_ft8_spectrum_baseline(ftx_ft8_ctx *ctx, const float *dd, int *nfa, int *nfb, float *sbase)
{
  const int NST = FT8_NFFT1 / 2, NF = 93;
  float savg[NH1];
  float *x = ctx->fft_base.r;
  float complex *cx = (float complex *)ctx->fft_base.c;

  for (int i = 0; i < NH1; i++) savg[i] = 0.0f;
  for (int j = 1; j <= NF; j++) {
    int ia = (j - 1) * NST + 1;
    int ib = ia + FT8_NFFT1 - 1;
    if (ib > FT8_NMAX) break;
    for (int i = 0; i < FT8_NFFT1; i++) x[i] = dd[ia - 1 + i] * ctx->gsb_window[i];
    x[FT8_NFFT1] = x[FT8_NFFT1 + 1] = 0.0f;
    ftx_fft_exec(&ctx->fft_base);
    for (int i = 1; i <= NH1; i++) {
      float m = cabsf(cx[i]);
      savg[i - 1] += m * m;                    /* s(i,j)=abs(cx(i))**2; savg=savg+s */
    }
  }

  int nwin = *nfb - *nfa;
  if (*nfa < 100) {
    *nfa = 100;
    if (nwin < 100) *nfb = *nfa + nwin;
  }
  if (*nfb > 4910) {
    *nfb = 4910;
    if (nwin < 100) *nfa = *nfb - nwin;
  }
  baseline(savg, *nfa, *nfb, sbase);
}

/* ------------------------------------------------------------------------
 * sync8
 * ---------------------------------------------------------------------- */
/* Fortran maxloc over v[0..n-1]: first maximum, NaNs skipped; 0-based result. */
static int fmaxloc(const float *v, int n)
{
  int best = -1;
  for (int i = 0; i < n; i++) {
    if (isnan(v[i])) continue;
    if (best < 0 || v[i] > v[best]) best = i;
  }
  return best < 0 ? 0 : best;
}

int ftx_ft8_sync8(ftx_ft8_ctx *ctx, const float *dd, int *nfa, int *nfb, float syncmin, int nfqso,
                  int maxcand, ftx_ft8_candidate *cand, float *sbase)
{
  const int MAXPRECAND = 1000;
  float *s = ctx->s, *sync2d = ctx->sync2d;
  float *x = ctx->fft_sym.r;
  float complex *cx = (float complex *)ctx->fft_sym.c;
  float red[NH1], red2[NH1];
  int jpeak[NH1], jpeak2[NH1];
  int indx[NH1 + 1], indx2[NH1 + 1];
  float *c0 = calloc((size_t)3 * MAXPRECAND, sizeof(float));   /* candidate0(3,MAXPRECAND) */
  int *cindx = malloc((size_t)(MAXPRECAND + 1) * sizeof(int));
#define S(i, j)   s[(size_t)((j) - 1) * NH1 + ((i) - 1)]
#define SY(i, j)  sync2d[(size_t)((i) - 1) * (2 * JZ + 1) + ((j) + JZ)]   /* j contiguous: maxloc over lags */
#define C0(r, k)  c0[3 * ((k) - 1) + ((r) - 1)]

  /* symbol spectra, stepping by NSTEP */
  float tstep = FT8_NSTEP / 12000.0f;
  float df = 12000.0f / FT8_NFFT1;
  float fac = 1.0f / 300.0f;
  for (int j = 1; j <= NHSYM; j++) {
    int ia = (j - 1) * FT8_NSTEP + 1;
    for (int i = 0; i < FT8_NSPS; i++) x[i] = fac * dd[ia - 1 + i];
    for (int i = FT8_NSPS; i < FT8_NFFT1 + 2; i++) x[i] = 0.0f;
    ftx_fft_exec(&ctx->fft_sym);
    for (int i = 1; i <= NH1; i++) {
      float re = crealf(cx[i]), im = cimagf(cx[i]);
      S(i, j) = re * re + im * im;
    }
  }
  ftx_ft8_spectrum_baseline(ctx, dd, nfa, nfb, sbase);

  int ia = (int)lroundf(*nfa / df);
  if (ia < 1) ia = 1;
  int ib = (int)lroundf(*nfb / df);
  int nssy = FT8_NSPS / FT8_NSTEP;              /* 4 steps per symbol        */
  int nfos = FT8_NFFT1 / FT8_NSPS;              /* 2x frequency oversampling */
  int jstrt = (int)(0.5f / tstep);
  int k = 0;

  for (int i = ia; i <= ib; i++) {
    for (int j = -JZ; j <= JZ; j++) {
      float ta = 0.0f, tb = 0.0f, tc = 0.0f, t0a = 0.0f, t0b = 0.0f, t0c = 0.0f;
      for (int n = 0; n <= 6; n++) {
        int m = j + jstrt + nssy * n;
        if (m >= 1 && m <= NHSYM) {
          ta += S(i + nfos * icos7[n], m);
          float t = 0.0f;
          for (int q = 0; q <= 6; q++) t += S(i + nfos * q, m);
          t0a += t;
        }
        {
          int mb = m + nssy * 36;
          tb += S(i + nfos * icos7[n], mb);
          float t = 0.0f;
          for (int q = 0; q <= 6; q++) t += S(i + nfos * q, mb);
          t0b += t;
        }
        if (m + nssy * 72 <= NHSYM) {
          int mc = m + nssy * 72;
          tc += S(i + nfos * icos7[n], mc);
          float t = 0.0f;
          for (int q = 0; q <= 6; q++) t += S(i + nfos * q, mc);
          t0c += t;
        }
      }
      float t = ta + tb + tc;
      float t0 = t0a + t0b + t0c;
      t0 = (t0 - t) / 6.0f;
      float sync_abc = t / t0;
      t = tb + tc;
      t0 = t0b + t0c;
      t0 = (t0 - t) / 6.0f;
      float sync_bc = t / t0;
      SY(i, j) = fmaxf(sync_abc, sync_bc);     /* max(); with one NaN operand both gfortran and fmaxf return the other */
    }
  }

  for (int i = 0; i < NH1; i++) { red[i] = 0.0f; red2[i] = 0.0f; jpeak[i] = 0; jpeak2[i] = 0; }
  int mlag = 13, mlag2 = JZ;
  for (int i = ia; i <= ib; i++) {
    jpeak[i - 1] = fmaxloc(&SY(i, -mlag), 2 * mlag + 1) - mlag;
    red[i - 1] = SY(i, jpeak[i - 1]);
    jpeak2[i - 1] = fmaxloc(&SY(i, -mlag2), 2 * mlag2 + 1) - mlag2;
    red2[i - 1] = SY(i, jpeak2[i - 1]);
  }
  int iz = ib - ia + 1;
  ftx_indexx(&red[ia - 2], iz, indx);            /* red(ia:ib) as a 1-based array */
  int npctile = (int)lroundf(0.40f * iz);
  if (npctile < 1) {
    free(c0);
    free(cindx);
    return 0;
  }
  int ibase = indx[npctile] - 1 + ia;
  if (ibase < 1) ibase = 1;
  if (ibase > NH1) ibase = NH1;
  float base = red[ibase - 1];
  for (int i = 0; i < NH1; i++) red[i] = red[i] / base;
  ftx_indexx(&red2[ia - 2], iz, indx2);
  int ibase2 = indx2[npctile] - 1 + ia;
  if (ibase2 < 1) ibase2 = 1;
  if (ibase2 > NH1) ibase2 = NH1;
  float base2 = red2[ibase2 - 1];
  for (int i = 0; i < NH1; i++) red2[i] = red2[i] / base2;

  int lim = iz < MAXPRECAND ? iz : MAXPRECAND;
  for (int i = 1; i <= lim; i++) {
    int n = ia + indx[iz + 1 - i] - 1;
    if (k >= MAXPRECAND) break;
    if (red[n - 1] >= syncmin && !isnan(red[n - 1])) {
      k++;
      C0(1, k) = n * df;
      C0(2, k) = (jpeak[n - 1] - 0.5f) * tstep;
      C0(3, k) = red[n - 1];
    }
    if (abs(jpeak2[n - 1] - jpeak[n - 1]) == 0) continue;
    if (k >= MAXPRECAND) break;
    if (red2[n - 1] >= syncmin && !isnan(red2[n - 1])) {
      k++;
      C0(1, k) = n * df;
      C0(2, k) = (jpeak2[n - 1] - 0.5f) * tstep;
      C0(3, k) = red2[n - 1];
    }
  }
  int ncand = k;

  /* keep only the best of near-duplicate frequencies */
  for (int i = 2; i <= ncand; i++) {
    for (int j = 1; j <= i - 1; j++) {
      float fdiff = fabsf(C0(1, i)) - fabsf(C0(1, j));
      float tdiff = fabsf(C0(2, i) - C0(2, j));
      if (fabsf(fdiff) < 4.0f && tdiff < 0.04f) {
        if (C0(3, i) >= C0(3, j)) C0(3, j) = 0.0f;
        if (C0(3, i) < C0(3, j)) C0(3, i) = 0.0f;
      }
    }
  }

  /* sort by sync; candidates within 10 Hz of nfqso go first */
  {
    float *syncs = malloc((size_t)(ncand + 1) * sizeof(float));
    for (int i = 1; i <= ncand; i++) syncs[i] = C0(3, i);
    ftx_indexx(syncs, ncand, cindx);
    free(syncs);
  }
  k = 1;
  for (int i = 1; i <= ncand; i++) {
    if (fabsf(C0(1, i) - nfqso) <= 10.0f && C0(3, i) >= syncmin) {
      cand[k - 1].freq = C0(1, i);
      cand[k - 1].dt = C0(2, i);
      cand[k - 1].sync = C0(3, i);
      C0(3, i) = 0.0f;
      k++;
    }
  }
  for (int i = ncand; i >= 1; i--) {
    int j = cindx[i];
    if (C0(3, j) >= syncmin) {
      cand[k - 1].dt = C0(2, j);
      cand[k - 1].sync = C0(3, j);
      cand[k - 1].freq = fabsf(C0(1, j));
      k++;
      if (k > maxcand) break;
    }
  }
  free(c0);
  free(cindx);
  return k - 1;
#undef S
#undef SY
#undef C0
}

/* ------------------------------------------------------------------------
 * ft8_downsample
 * ---------------------------------------------------------------------- */
void ftx_ft8_downsample(ftx_ft8_ctx *ctx, const float *dd, bool *newdat, float f0, fftwf_complex *c1v)
{
  const int NFFT1 = FT8_NFFT_DS, NFFT2 = FT8_NDS;
  float *x = ctx->fft_long.r;
  float complex *cx = (float complex *)ctx->fft_long.c;
  float complex *c1 = (float complex *)ctx->fft_ds.c;

  if (*newdat) {                                 /* data in dd changed: recompute the long FFT */
    memcpy(x, dd, (size_t)FT8_NMAX * sizeof(float));
    for (int i = FT8_NMAX; i < NFFT1 + 2; i++) x[i] = 0.0f;
    ftx_fft_exec(&ctx->fft_long);
    *newdat = false;
  }
  float df = 12000.0f / NFFT1;
  float baud = 12000.0f / FT8_NSPS;
  int i0 = (int)lroundf(f0 / df);
  float ft = f0 + 8.5f * baud;
  int it = (int)lroundf(ft / df);
  if (it > NFFT1 / 2) it = NFFT1 / 2;
  float fb = f0 - 1.5f * baud;
  int ib = (int)lroundf(fb / df);
  if (ib < 1) ib = 1;
  int k = 0;
  for (int i = 0; i < NFFT2; i++) c1[i] = 0.0f;
  for (int i = ib; i <= it; i++) c1[k++] = cx[i];
  for (int i = 0; i <= 100; i++) c1[i] = c1[i] * ctx->taper[100 - i];
  for (int i = 0; i <= 100; i++) c1[k - 1 - 100 + i] = c1[k - 1 - 100 + i] * ctx->taper[i];
  {                                              /* c1=cshift(c1,i0-ib) */
    int shift = i0 - ib;
    float complex tmp[FT8_NDS];
    for (int m = 0; m < NFFT2; m++) {
      int src = (m + shift) % NFFT2;
      if (src < 0) src += NFFT2;
      tmp[m] = c1[src];
    }
    memcpy(c1, tmp, sizeof tmp);
  }
  ftx_fft_exec(&ctx->fft_ds);                    /* c2c back to the time domain */
  float fac = 1.0f / sqrtf((float)NFFT1 * NFFT2);
  for (int i = 0; i < NFFT2; i++) c1[i] = fac * c1[i];
  memcpy(c1v, c1, (size_t)NFFT2 * sizeof(float complex));
}

/* ------------------------------------------------------------------------
 * sync8d
 * ---------------------------------------------------------------------- */
float ftx_ft8_sync8d(ftx_ft8_ctx *ctx, const fftwf_complex *cd0v, int i0, const fftwf_complex *ctwkv, int itwk)
{
  const float complex *cd0 = (const float complex *)cd0v;
  const float complex *ctwk = (const float complex *)ctwkv;
  float sync = 0;
  for (int i = 0; i <= 6; i++) {                 /* 7 Costas tones, 3 Costas arrays */
    int i1 = i0 + i * 32;
    int i2 = i1 + 36 * 32;
    int i3 = i1 + 72 * 32;
    float complex csync2[32];
    for (int j = 0; j < 32; j++) csync2[j] = itwk == 1 ? ctwk[j] * ctx->csync[i][j] : ctx->csync[i][j];
    float complex z1 = 0.0f, z2 = 0.0f, z3 = 0.0f;
    if (i1 >= 0 && i1 + 31 <= FT8_NP2 - 1)
      for (int j = 0; j < 32; j++) z1 += cd0[i1 + j] * conjf(csync2[j]);
    if (i2 >= 0 && i2 + 31 <= FT8_NP2 - 1)
      for (int j = 0; j < 32; j++) z2 += cd0[i2 + j] * conjf(csync2[j]);
    if (i3 >= 0 && i3 + 31 <= FT8_NP2 - 1)
      for (int j = 0; j < 32; j++) z3 += cd0[i3 + j] * conjf(csync2[j]);
    float p1 = crealf(z1) * crealf(z1) + cimagf(z1) * cimagf(z1);
    float p2 = crealf(z2) * crealf(z2) + cimagf(z2) * cimagf(z2);
    float p3 = crealf(z3) * crealf(z3) + cimagf(z3) * cimagf(z3);
    sync = sync + p1 + p2 + p3;
  }
  return sync;
}

/* ------------------------------------------------------------------------
 * twkfreq1
 * ---------------------------------------------------------------------- */
void ftx_ft8_twkfreq1(const fftwf_complex *cav, int npts, float fsample, const float a[5], fftwf_complex *cbv)
{
  const float complex *ca = (const float complex *)cav;
  float complex *cb = (float complex *)cbv;
  const float twopi = 6.283185307f;
  float complex w = 1.0f, wstep = 1.0f;
  float x0 = 0.5f * (npts + 1);
  float s = 2.0f / npts;
  for (int i = 1; i <= npts; i++) {
    float x = s * (i - x0);
    float x2 = x * x;
    float p2 = 1.5f * x * x - 0.5f;
    float p3 = 2.5f * (x2 * x) - 1.5f * x;
    float p4 = 4.375f * (x2 * x2) - 3.75f * x2 + 0.375f;
    float dphi = (a[0] + x * a[1] + p2 * a[2] + p3 * a[3] + p4 * a[4]) * (twopi / fsample);
    wstep = FTX_CMPLXF(cosf(dphi), sinf(dphi));
    w = w * wstep;
    cb[i - 1] = w * ca[i - 1];
  }
}
