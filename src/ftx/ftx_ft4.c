/* ftx_ft4.c — the FT4 decoder (see ftx_ft4.h for provenance and license).
 *
 * Ported by hand to C from WSJT-X v3.0.2 (git ccdfaf3), lib/ft4_decode.f90
 * and the lib/ft4/ sources.  This port: Copyright (C) 2026 Richard Fakenberg, OK1BR.
 * GPL-3.0-or-later.
 */
#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ftx_ft4.h"
#include "ftx_ft8_internal.h"      /* FTX_CMPLXF, shared helpers from the FT8 port */
#include "ftx_internal.h"
#include "ftx_ldpc.h"
#include "ftx_pack77.h"

#define NN    FT4_NN
#define ND    FT4_ND
#define NSS   FT4_NSS
#define NDMAX FT4_NDMAX
#define NH1   FT4_NH1
#define NMAX  FT4_NMAX
#define NFILT4 1400
#define NFRAME4 ((FT4_NN + 2) * FT4_NSPS)   /* 60480 */

static const int icos4a[4] = { 0, 1, 3, 2 };
static const int icos4b[4] = { 1, 0, 2, 3 };
static const int icos4c[4] = { 2, 3, 1, 0 };
static const int icos4d[4] = { 3, 2, 0, 1 };
static const int graymap4[4] = { 0, 1, 3, 2 };
static const uint8_t rvec[77] = { 0,1,0,0,1,0,1,0,0,1,0,1,1,1,1,0,1,0,0,0,1,0,0,1,1,0,1,1,0,
                                  1,0,0,1,0,1,1,0,0,0,0,1,0,0,0,1,0,1,0,0,1,1,1,1,0,0,1,0,1,
                                  0,1,0,1,0,1,1,0,1,1,1,1,1,0,0,0,1,0,1 };
/* AP message templates (0/1 as in the Fortran DATA; scrambled with rvec at init) */
static const int mcq0[29]     = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0 };
static const int mcqru0[29]   = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1,1,1,1,0,0,1,1,0,0 };
static const int mcqfd0[29]   = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,1,0,0,0,1,0 };
static const int mcqtest0[29] = { 0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,1,0,1,0,1,1,1,1,1,1,0,0,1,0 };
static const int mcqww0[29]   = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,1,1,1,1,0 };
static const int nappasses[6] = { 2, 2, 2, 2, 2, 3 };
static const int naptypes[6][4] = { { 1, 2, 0, 0 }, { 2, 3, 0, 0 }, { 2, 3, 0, 0 },
                                    { 3, 6, 0, 0 }, { 3, 6, 0, 0 }, { 3, 1, 2, 0 } };

struct ftx_ft4_ctx {
  ftx_pack77_ctx pack77;
  ftx_ldpc_ctx   ldpc;
  ftx_fft fft_sym;        /* r2c 2304   getcandidates4           */
  ftx_fft fft_long;       /* r2c 72576  ft4_downsample (cached)  */
  ftx_fft fft_ds;         /* c2c 4032 bwd                        */
  ftx_fft fft_sym32;      /* c2c 32 fwd get_ft4_bitmetrics       */
  ftx_fft fft_sub_fwd;    /* c2c 72576 fwd  subtractft4          */
  ftx_fft fft_sub_bwd;    /* c2c 72576 bwd                       */
  float gc_window[FT4_NFFT1];               /* Nuttall window              */
  float *s;                                 /* s(NH1,NHSYM)                */
  float ds_window[NDMAX];                   /* ft4_downsample window       */
  float complex csync[4][2 * NSS];          /* sync4d: the four Costas arrays, half rate */
  float complex ctwk2[33][2 * NSS];         /* decode: frequency tweaks for idf -16..16  */
  int mcq[29], mcqru[29], mcqfd[29], mcqtest[29], mcqww[29];   /* scrambled, +1/-1 */
  float gw_pulse[3 * FT4_NSPS];             /* gen_ft4wave pulse           */
  float *gw_dphi;                           /* (NN+2)*NSPS                 */
  float complex *sub_cw;                    /* subtractft4 filter spectrum */
  float complex *sub_cref;                  /* NFRAME4                     */
  float complex *sub_camp;                  /* NMAX                        */
  /* decode `save` state keyed by my/his call */
  char mycall0[13], hiscall0[13];
  int  apbits[2 * ND], apmy_ru[28], aphis_fd[28];
  float *dd;                                /* NMAX working waveform       */
};

/* ------------------------------------------------------------------------
 * Context
 * ---------------------------------------------------------------------- */
ftx_ft4_ctx *ftx_ft4_ctx_new(void)
{
  ftx_ft4_ctx *ctx = calloc(1, sizeof *ctx);
  if (ctx == NULL) return NULL;
  ftx_pack77_init(&ctx->pack77);
  ftx_ldpc_init(&ctx->ldpc);
  ftx_fft_plan_r2c(&ctx->fft_sym, FT4_NFFT1);
  ftx_fft_plan_r2c(&ctx->fft_long, NMAX);
  ftx_fft_plan_c2c(&ctx->fft_ds, NDMAX, +1);
  ftx_fft_plan_c2c(&ctx->fft_sym32, NSS, -1);
  ftx_fft_plan_c2c(&ctx->fft_sub_fwd, NMAX, -1);
  ftx_fft_plan_c2c(&ctx->fft_sub_bwd, NMAX, +1);
  ctx->s = calloc((size_t)NH1 * FT4_NHSYM, sizeof(float));
  ctx->gw_dphi = calloc((size_t)(NN + 2) * FT4_NSPS, sizeof(float));
  ctx->sub_cw = fftwf_malloc((size_t)NMAX * sizeof(float complex));
  ctx->sub_cref = calloc(NFRAME4, sizeof(float complex));
  ctx->sub_camp = calloc(NMAX, sizeof(float complex));
  ctx->dd = calloc(NMAX, sizeof(float));
  if (ctx->s == NULL || ctx->gw_dphi == NULL || ctx->sub_cw == NULL || ctx->sub_cref == NULL ||
      ctx->sub_camp == NULL || ctx->dd == NULL) {
    ftx_ft4_ctx_free(ctx);
    return NULL;
  }
  const float pi = 4.0f * atanf(1.0f), twopi = 8.0f * atanf(1.0f);

  /* getcandidates4: plain Nuttall window */
  {
    const float a0 = 0.3635819f, a1 = -0.4891775f, a2 = 0.1365995f, a3 = -0.0106411f;
    int n = FT4_NFFT1;
    for (int i = 1; i <= n; i++)
      ctx->gc_window[i - 1] = a0 + a1 * cosf(2 * pi * (i - 1) / (float)n) + a2 * cosf(4 * pi * (i - 1) / (float)n) +
                              a3 * cosf(6 * pi * (i - 1) / (float)n);
  }
  /* ft4_downsample window: raised-cosine edges, flat top, shifted by one baud */
  {
    float df = 12000.0f / NMAX, baud = 12000.0f / FT4_NSPS;
    float bw_transition = 0.5f * baud, bw_flat = 4 * baud;
    int iwt = (int)(bw_transition / df), iwf = (int)(bw_flat / df);
    float w[NDMAX];
    for (int i = 0; i < NDMAX; i++) w[i] = 0.0f;
    for (int k = 0; k < iwt; k++) w[k] = 0.5f * (1 + cosf(pi * (iwt - 1 - k) / (float)iwt));
    for (int k = iwt; k < iwt + iwf; k++) w[k] = 1.0f;
    for (int k = 0; k < iwt; k++) w[iwt + iwf + k] = 0.5f * (1 + cosf(pi * k / (float)iwt));
    int iws = (int)(baud / df);
    for (int m = 0; m < NDMAX; m++) ctx->ds_window[m] = w[(m + iws) % NDMAX];   /* cshift(window,iws) */
  }
  /* sync4d: csync tables at half rate (NSS/2 samples per symbol) */
  {
    const int *ic[4] = { icos4a, icos4b, icos4c, icos4d };
    for (int t = 0; t < 4; t++) {
      float phi = 0.0f;
      int k = 0;
      for (int i = 0; i <= 3; i++) {
        float dphi = 2 * twopi * ic[t][i] / (float)NSS;
        for (int j = 1; j <= NSS / 2; j++) {
          ctx->csync[t][k++] = FTX_CMPLXF(cosf(phi), sinf(phi));
          phi = fmodf(phi + dphi, twopi);
        }
      }
    }
  }
  /* decode: frequency tweaks ctwk2(:,idf) = twkfreq1 of a unit vector */
  {
    float fs = 12000.0f / FT4_NDOWN;
    for (int idf = -16; idf <= 16; idf++) {
      float a[5] = { (float)idf, 0.0f, 0.0f, 0.0f, 0.0f };
      float complex one[2 * NSS];
      for (int i = 0; i < 2 * NSS; i++) one[i] = 1.0f;
      ftx_ft8_twkfreq1((fftwf_complex *)one, 2 * NSS, fs / 2.0f, a, (fftwf_complex *)ctx->ctwk2[idf + 16]);
    }
    for (int i = 0; i < 29; i++) {
      ctx->mcq[i] = 2 * ((mcq0[i] + rvec[i]) % 2) - 1;
      ctx->mcqru[i] = 2 * ((mcqru0[i] + rvec[i]) % 2) - 1;
      ctx->mcqfd[i] = 2 * ((mcqfd0[i] + rvec[i]) % 2) - 1;
      ctx->mcqtest[i] = 2 * ((mcqtest0[i] + rvec[i]) % 2) - 1;
      ctx->mcqww[i] = 2 * ((mcqww0[i] + rvec[i]) % 2) - 1;
    }
    memset(ctx->mycall0, ' ', 12); ctx->mycall0[12] = '\0';
    memset(ctx->hiscall0, ' ', 12); ctx->hiscall0[12] = '\0';
  }
  /* gen_ft4wave pulse (bt = 1) */
  {
    float c = pi * sqrtf(2.0f / logf(2.0f));
    for (int i = 1; i <= 3 * FT4_NSPS; i++) {
      float tt = (i - 1.5f * FT4_NSPS) / (float)FT4_NSPS;
      ctx->gw_pulse[i - 1] = 0.5f * (erff(c * 1.0f * (tt + 0.5f)) - erff(c * 1.0f * (tt - 0.5f)));
    }
  }
  /* subtractft4 filter and its spectrum */
  {
    float fac = 1.0f / (float)NMAX;
    float window[NFILT4 + 1], sum = 0.0f;
    for (int j = -NFILT4 / 2; j <= NFILT4 / 2; j++) {
      float cc = cosf(pi * j / NFILT4);
      window[j + NFILT4 / 2] = cc * cc;
      sum = sum + window[j + NFILT4 / 2];
    }
    float complex *cw = (float complex *)ctx->fft_sub_fwd.c;
    for (int i = 0; i < NMAX; i++) cw[i] = 0.0f;
    for (int i = 0; i <= NFILT4; i++) cw[i] = window[i] / sum;
    int shift = NFILT4 / 2 + 1;
    for (int m = 0; m < NMAX; m++) ctx->sub_cw[m] = cw[(m + shift) % NMAX];
    memcpy(cw, ctx->sub_cw, (size_t)NMAX * sizeof(float complex));
    ftx_fft_exec(&ctx->fft_sub_fwd);
    for (int i = 0; i < NMAX; i++) ctx->sub_cw[i] = cw[i] * fac;
  }
  return ctx;
}

void ftx_ft4_ctx_free(ftx_ft4_ctx *ctx)
{
  if (ctx == NULL) return;
  ftx_fft_free(&ctx->fft_sym);
  ftx_fft_free(&ctx->fft_long);
  ftx_fft_free(&ctx->fft_ds);
  ftx_fft_free(&ctx->fft_sym32);
  ftx_fft_free(&ctx->fft_sub_fwd);
  ftx_fft_free(&ctx->fft_sub_bwd);
  free(ctx->s);
  free(ctx->gw_dphi);
  fftwf_free(ctx->sub_cw);
  free(ctx->sub_cref);
  free(ctx->sub_camp);
  free(ctx->dd);
  free(ctx);
}

/* ------------------------------------------------------------------------
 * ft4_baseline (REAL*8 fit, as baseline.f90; returns a linear baseline)
 * ---------------------------------------------------------------------- */
static void ft4_baseline(float *s, int nfa, int nfb, float *sbase)
{
  double df = 12000.0 / FT4_NFFT1;
  int ia = (int)lround(200.0 / df);
  if (nfa > ia) ia = nfa;
  int ib = nfb < NH1 ? nfb : NH1;
  ftx_baseline_fit(s, ia, ib, sbase);                 /* dB conversion, lower-envelope polynomial + 0.65 */
  for (int i = ia; i <= ib; i++) sbase[i - 1] = powf(10.0f, sbase[i - 1] / 10.0f);
}

int ftx_ft4_candidates(ftx_ft4_ctx *ctx, const float *dd, float fa, float fb, float syncmin, int nfqso,
                       int maxcand, float *savg, ftx_ft4_candidate *cand, float *sbase)
{
  float *x = ctx->fft_sym.r;
  float complex *cx = (float complex *)ctx->fft_sym.c;
  float savsm[NH1];
  ftx_ft4_candidate *candt = calloc((size_t)maxcand, sizeof *candt);
  float df = 12000.0f / FT4_NFFT1;
  float fac = 1.0f / 300.0f;
#define S(i, j) ctx->s[(size_t)((j) - 1) * NH1 + ((i) - 1)]

  for (int i = 0; i < NH1; i++) savg[i] = 0.0f;
  for (int j = 1; j <= FT4_NHSYM; j++) {
    int ia = (j - 1) * FT4_NSTEP + 1;
    int ib = ia + FT4_NFFT1 - 1;
    if (ib > NMAX) break;
    for (int i = 0; i < FT4_NFFT1; i++) x[i] = fac * dd[ia - 1 + i] * ctx->gc_window[i];
    x[FT4_NFFT1] = x[FT4_NFFT1 + 1] = 0.0f;
    ftx_fft_exec(&ctx->fft_sym);
    for (int i = 1; i <= NH1; i++) {
      float m = cabsf(cx[i]);
      S(i, j) = m * m;
      savg[i - 1] += S(i, j);
    }
  }
  for (int i = 0; i < NH1; i++) savg[i] = savg[i] / FT4_NHSYM;
  for (int i = 0; i < NH1; i++) savsm[i] = 0.0f;
  for (int i = 8; i <= NH1 - 7; i++) {
    float t = 0.0f;
    for (int k = i - 7; k <= i + 7; k++) t += savg[k - 1];
    savsm[i - 1] = t / 15.0f;
  }

  int nfa = (int)(fa / df);
  if (nfa < (int)lroundf(200.0f / df)) nfa = (int)lroundf(200.0f / df);
  int nfb = (int)(fb / df);
  if (nfb > (int)lroundf(4910.0f / df)) nfb = (int)lroundf(4910.0f / df);
  for (int i = 0; i < NH1; i++) sbase[i] = 0.0f;
  ft4_baseline(savg, nfa, nfb, sbase);
  for (int i = nfa; i <= nfb; i++)
    if (sbase[i - 1] <= 0) { free(candt); return 0; }
  for (int i = nfa; i <= nfb; i++) savsm[i - 1] = savsm[i - 1] / sbase[i - 1];
  float f_offset = -1.5f * 12000.0f / FT4_NSPS;
  int ncand = 0;
  for (int i = nfa + 1; i <= nfb - 1; i++) {
    float sm = savsm[i - 1], sl = savsm[i - 2], sr = savsm[i];
    if (sm >= sl && sm >= sr && sm >= syncmin) {
      float den = sl - 2 * sm + sr;
      float del = 0.0f;
      if (den != 0.0f) del = 0.5f * (sl - sr) / den;
      float fpeak = (i + del) * df + f_offset;
      if (fpeak < 200.0f || fpeak > 4910.0f) continue;
      float speak = sm - 0.25f * (sl - sr) * del;
      candt[ncand].freq = fpeak;
      candt[ncand].snr = speak;
      ncand++;
      if (ncand == maxcand) break;
    }
  }
  int nq = 0;
  for (int i = 0; i < ncand; i++)
    if (fabsf(candt[i].freq - nfqso) <= 20.0f) nq++;
  int n1 = 0, n2 = nq;
  for (int i = 0; i < maxcand; i++) { cand[i].freq = 0.0f; cand[i].snr = 0.0f; }
  for (int i = 0; i < ncand; i++) {
    if (fabsf(candt[i].freq - nfqso) <= 20.0f) cand[n1++] = candt[i];
    else cand[n2++] = candt[i];
  }
  free(candt);
  return ncand;
#undef S
}

/* ------------------------------------------------------------------------
 * ft4_downsample
 * ---------------------------------------------------------------------- */
void ftx_ft4_downsample(ftx_ft4_ctx *ctx, const float *dd, bool newdata, float f0, fftwf_complex *cv)
{
  const int NFFT2 = NDMAX;
  float *x = ctx->fft_long.r;
  float complex *cx = (float complex *)ctx->fft_long.c;
  float complex *c1 = (float complex *)ctx->fft_ds.c;
  float df = 12000.0f / NMAX;
  if (newdata) {
    memcpy(x, dd, (size_t)NMAX * sizeof(float));
    x[NMAX] = x[NMAX + 1] = 0.0f;
    ftx_fft_exec(&ctx->fft_long);
  }
  int i0 = (int)lroundf(f0 / df);
  for (int i = 0; i < NFFT2; i++) c1[i] = 0.0f;
  if (i0 >= 0 && i0 <= NMAX / 2) c1[0] = cx[i0];
  for (int i = 1; i <= NFFT2 / 2; i++) {
    if (i0 + i >= 0 && i0 + i <= NMAX / 2) c1[i] = cx[i0 + i];
    if (i0 - i >= 0 && i0 - i <= NMAX / 2) c1[NFFT2 - i] = cx[i0 - i];
  }
  for (int i = 0; i < NFFT2; i++) c1[i] = c1[i] * ctx->ds_window[i] / (float)NFFT2;
  ftx_fft_exec(&ctx->fft_ds);
  memcpy(cv, c1, (size_t)NDMAX * sizeof(float complex));
}

/* ------------------------------------------------------------------------
 * sync4d
 * ---------------------------------------------------------------------- */
static float p4(float complex z, float fac)
{
  float complex zf = z * fac;
  return powf(crealf(zf) * crealf(zf) + cimagf(zf) * cimagf(zf), 0.5f);
}

float ftx_ft4_sync4d(ftx_ft4_ctx *ctx, const fftwf_complex *cd0v, int i0, const fftwf_complex *ctwkv)
{
  const int NP = NDMAX;
  const float complex *cd0 = (const float complex *)cd0v;
  const float complex *ctwk = (const float complex *)ctwkv;
  float fac = 1.0f / (2.0f * NSS);
  float complex csync2[2 * NSS];
  float complex z1 = 0.0f, z2 = 0.0f, z3 = 0.0f, z4 = 0.0f;
  int i1 = i0, i2 = i0 + 33 * NSS, i3 = i0 + 66 * NSS, i4 = i0 + 99 * NSS;

  for (int j = 0; j < 2 * NSS; j++) csync2[j] = ctwk[j] * ctx->csync[0][j];
  if (i1 >= 0 && i1 + 4 * NSS - 1 <= NP - 1) {
    for (int j = 0; j < 2 * NSS; j++) z1 += cd0[i1 + 2 * j] * conjf(csync2[j]);
  } else if (i1 < 0) {
    int npts = (i1 + 4 * NSS - 1) / 2;
    if (npts > 16) {                                  /* cd0(0:i1+4*NSS-1:2) * conjg(csync2(2*NSS-npts:)) */
      int n = (i1 + 4 * NSS - 1) / 2 + 1;
      for (int j = 0; j < n; j++) z1 += cd0[2 * j] * conjf(csync2[2 * NSS - npts - 1 + j]);
    }
  }
  for (int j = 0; j < 2 * NSS; j++) csync2[j] = ctwk[j] * ctx->csync[1][j];
  if (i2 >= 0 && i2 + 4 * NSS - 1 <= NP - 1)
    for (int j = 0; j < 2 * NSS; j++) z2 += cd0[i2 + 2 * j] * conjf(csync2[j]);
  for (int j = 0; j < 2 * NSS; j++) csync2[j] = ctwk[j] * ctx->csync[2][j];
  if (i3 >= 0 && i3 + 4 * NSS - 1 <= NP - 1)
    for (int j = 0; j < 2 * NSS; j++) z3 += cd0[i3 + 2 * j] * conjf(csync2[j]);
  for (int j = 0; j < 2 * NSS; j++) csync2[j] = ctwk[j] * ctx->csync[3][j];
  if (i4 >= 0 && i4 + 4 * NSS - 1 <= NP - 1) {
    for (int j = 0; j < 2 * NSS; j++) z4 += cd0[i4 + 2 * j] * conjf(csync2[j]);
  } else if (i4 + 4 * NSS - 1 > NP - 1) {
    int npts = (NP - 1 - i4 + 1) / 2;
    if (npts > 16)
      for (int j = 0; j < npts; j++) z4 += cd0[i4 + 2 * j] * conjf(csync2[j]);
  }
  return p4(z1, fac) + p4(z2, fac) + p4(z3, fac) + p4(z4, fac);
}

/* ------------------------------------------------------------------------
 * get_ft4_bitmetrics
 * ---------------------------------------------------------------------- */
static inline bool one8(int i, int j) { return (i & (1 << j)) != 0; }

static float masked_max4(const float *s2, int nt, int bit, bool want)
{
  float m = -3.4028235e38f;
  for (int i = 0; i < nt; i++)
    if (one8(i, bit) == want && s2[i] > m) m = s2[i];
  return m;
}

static void get_ft4_bitmetrics(ftx_ft4_ctx *ctx, const float complex *cd, float bitmetrics[3][2 * NN], bool *badsync)
{
  float complex cs[NN][4];
  float s4[NN][4], s2[256];
  for (int k = 1; k <= NN; k++) {
    int i1 = (k - 1) * NSS;
    memcpy(ctx->fft_sym32.c, cd + i1, NSS * sizeof(float complex));
    ftx_fft_exec(&ctx->fft_sym32);
    float complex *out = (float complex *)ctx->fft_sym32.c;
    for (int t = 0; t < 4; t++) {
      cs[k - 1][t] = out[t];
      s4[k - 1][t] = cabsf(out[t]);
    }
  }
  int is1 = 0, is2 = 0, is3 = 0, is4 = 0;             /* sync quality check */
  for (int k = 1; k <= 4; k++) {
    int ip;
    ip = 0; for (int t = 1; t < 4; t++) if (s4[k - 1][t] > s4[k - 1][ip]) ip = t;
    if (icos4a[k - 1] == ip) is1++;
    ip = 0; for (int t = 1; t < 4; t++) if (s4[k + 32][t] > s4[k + 32][ip]) ip = t;
    if (icos4b[k - 1] == ip) is2++;
    ip = 0; for (int t = 1; t < 4; t++) if (s4[k + 65][t] > s4[k + 65][ip]) ip = t;
    if (icos4c[k - 1] == ip) is3++;
    ip = 0; for (int t = 1; t < 4; t++) if (s4[k + 98][t] > s4[k + 98][ip]) ip = t;
    if (icos4d[k - 1] == ip) is4++;
  }
  int nsync = is1 + is2 + is3 + is4;
  *badsync = false;
  if (nsync < 8) {
    *badsync = true;
    return;
  }
  for (int nseq = 1; nseq <= 3; nseq++) {             /* coherent sequences of 1, 2 and 4 symbols */
    int nsym = nseq == 1 ? 1 : (nseq == 2 ? 2 : 4);
    int nt = 1 << (2 * nsym);
    for (int ks = 1; ks <= NN - nsym + 1; ks += nsym) {
      for (int i = 0; i < nt; i++) {
        int i1 = i / 64, i2 = (i & 63) / 16, i3 = (i & 15) / 4, i4 = i & 3;
        if (nsym == 1) s2[i] = cabsf(cs[ks - 1][graymap4[i4]]);
        else if (nsym == 2) s2[i] = cabsf(cs[ks - 1][graymap4[i3]] + cs[ks][graymap4[i4]]);
        else s2[i] = cabsf(cs[ks - 1][graymap4[i1]] + cs[ks][graymap4[i2]] + cs[ks + 1][graymap4[i3]] + cs[ks + 2][graymap4[i4]]);
      }
      int ipt = 1 + (ks - 1) * 2;
      int ibmax = nsym == 1 ? 1 : (nsym == 2 ? 3 : 7);
      for (int ib = 0; ib <= ibmax; ib++) {
        float bm = masked_max4(s2, nt, ibmax - ib, true) - masked_max4(s2, nt, ibmax - ib, false);
        if (ipt + ib > 2 * NN) continue;
        bitmetrics[nseq - 1][ipt + ib - 1] = bm;
      }
    }
  }
  bitmetrics[1][204] = bitmetrics[0][204]; bitmetrics[1][205] = bitmetrics[0][205];
  for (int i = 200; i < 204; i++) bitmetrics[2][i] = bitmetrics[1][i];
  bitmetrics[2][204] = bitmetrics[0][204]; bitmetrics[2][205] = bitmetrics[0][205];
  ftx_ft8_normalizebmet(bitmetrics[0], 2 * NN);
  ftx_ft8_normalizebmet(bitmetrics[1], 2 * NN);
  ftx_ft8_normalizebmet(bitmetrics[2], 2 * NN);
}

/* ------------------------------------------------------------------------
 * genft4 (tones), gen_ft4wave, subtractft4
 * ---------------------------------------------------------------------- */
static void ft4_tones_from_77bits(const uint8_t *msgbits77, int i4tone[NN])
{
  uint8_t bits[77], codeword[174];
  int itmp[ND];
  for (int i = 0; i < 77; i++) bits[i] = (uint8_t)((msgbits77[i] + rvec[i]) % 2);
  ftx_ldpc_encode174_91(bits, codeword);
  for (int i = 1; i <= ND; i++) {                     /* gray mapping 00→0 01→1 11→2 10→3 */
    int is = codeword[2 * i - 1] + 2 * codeword[2 * i - 2];
    if (is <= 1) itmp[i - 1] = is;
    if (is == 2) itmp[i - 1] = 3;
    if (is == 3) itmp[i - 1] = 2;
  }
  for (int i = 0; i < 4; i++) i4tone[i] = icos4a[i];
  for (int i = 0; i < 29; i++) i4tone[4 + i] = itmp[i];
  for (int i = 0; i < 4; i++) i4tone[33 + i] = icos4b[i];
  for (int i = 0; i < 29; i++) i4tone[37 + i] = itmp[29 + i];
  for (int i = 0; i < 4; i++) i4tone[66 + i] = icos4c[i];
  for (int i = 0; i < 29; i++) i4tone[70 + i] = itmp[58 + i];
  for (int i = 0; i < 4; i++) i4tone[99 + i] = icos4d[i];
}

static void gen_ft4wave(ftx_ft4_ctx *ctx, const int *itone, int nsym, int nsps, float fsample, float f0,
                        float complex *cwave, int nwave)
{
  const float twopi = 8.0f * atanf(1.0f);
  float dt = 1.0f / fsample, hmod = 1.0f;
  float *dphi = ctx->gw_dphi;
  int ndphi = (nsym + 2) * nsps;
  float dphi_peak = twopi * hmod / (float)nsps;
  for (int i = 0; i < ndphi; i++) dphi[i] = 0.0f;
  for (int j = 1; j <= nsym; j++) {
    int ib = (j - 1) * nsps;
    for (int i = 0; i < 3 * nsps; i++) dphi[ib + i] = dphi[ib + i] + dphi_peak * ctx->gw_pulse[i] * itone[j - 1];
  }
  float phi = 0.0f;
  for (int i = 0; i < ndphi; i++) dphi[i] = dphi[i] + twopi * f0 * dt;
  for (int k = 0; k < nwave; k++) cwave[k] = 0.0f;
  int k = 0;
  for (int j = 0; j <= ndphi - 1 && k < nwave; j++) {
    cwave[k++] = FTX_CMPLXF(cosf(phi), sinf(phi));
    phi = fmodf(phi + dphi[j], twopi);
  }
  for (int i = 0; i < nsps; i++)                      /* ramp-up and ramp-down symbols */
    cwave[i] = cwave[i] * ((1.0f - cosf(twopi * i / (2.0f * nsps))) / 2.0f);
  int k1 = (nsym + 1) * nsps + 1;
  for (int i = 0; i < nsps; i++) {
    int idx = k1 - 1 + i;
    if (idx < nwave) cwave[idx] = cwave[idx] * ((1.0f + cosf(twopi * i / (2.0f * nsps))) / 2.0f);
  }
}

static void subtractft4(ftx_ft4_ctx *ctx, float *dd, const int itone[NN], float f0, float dt)
{
  float complex *cref = ctx->sub_cref, *camp = ctx->sub_camp;
  float complex *cfilt = (float complex *)ctx->fft_sub_fwd.c;
  int nstart = (int)(dt * 12000 + 1 - FT4_NSPS);
  gen_ft4wave(ctx, itone, NN, FT4_NSPS, 12000.0f, f0, cref, NFRAME4);
  for (int i = 0; i < NMAX; i++) camp[i] = 0.0f;
  for (int i = 1; i <= NFRAME4; i++) {
    int id = nstart - 1 + i;
    if (id >= 1 && id <= NMAX) camp[i - 1] = dd[id - 1] * conjf(cref[i - 1]);
  }
  for (int i = 0; i < NMAX; i++) cfilt[i] = 0.0f;
  memcpy(cfilt, camp, NFRAME4 * sizeof(float complex));
  ftx_fft_exec(&ctx->fft_sub_fwd);
  for (int i = 0; i < NMAX; i++) cfilt[i] = cfilt[i] * ctx->sub_cw[i];
  memcpy(ctx->fft_sub_bwd.c, cfilt, (size_t)NMAX * sizeof(float complex));
  ftx_fft_exec(&ctx->fft_sub_bwd);
  cfilt = (float complex *)ctx->fft_sub_bwd.c;
  for (int i = 1; i <= NFRAME4; i++) {                /* subtract the reconstructed signal */
    int j = nstart + i - 1;
    if (j >= 1 && j <= NMAX) dd[j - 1] = dd[j - 1] - 2 * crealf(cfilt[i - 1] * cref[i - 1]);
  }
}

/* ------------------------------------------------------------------------
 * decode
 * ---------------------------------------------------------------------- */
static void field12(char *dst, const char *src)       /* 12-char blank padded field + NUL */
{
  snprintf(dst, 13, "%-12.12s", src);
}

void ftx_ft4_decode(ftx_ft4_ctx *ctx, const int16_t *iwave, const ftx_ft4_params *p,
                    ftx_ft4_decode_cb cb, void *user)
{
  float *dd = ctx->dd;
  float savg[NH1], sbase[NH1];
  ftx_ft4_candidate candidate[FT4_MAXCAND];
  float complex cd2[NDMAX], cb_[NDMAX], cd[NN * NSS];
  float bitmetrics[3][2 * NN];
  float llr[2 * ND], llra[2 * ND], llrb[2 * ND], llrc[2 * ND], llrd[2 * ND];
  uint8_t message77[77], apmask[2 * ND], cw[2 * ND], message91[91], hbits[2 * NN];
  int i4tone[NN];
  char decodes[100][38], message[38], c77[78], mycall[13], hiscall[13];
  const float fs = 12000.0f / FT4_NDOWN;
  (void)fs;

  field12(mycall, p->mycall);
  field12(hiscall, p->hiscall);
  ftx_pack77_set_dxcall(&ctx->pack77, hiscall);       /* dxcall13=hiscall; mycall13=mycall */
  ftx_pack77_set_mycall(&ctx->pack77, mycall);

  float smax1 = 0.0f;
  int nd1 = 0;
  if (strcmp(mycall, ctx->mycall0) != 0 || strcmp(hiscall, ctx->hiscall0) != 0) {
    for (int i = 0; i < 2 * ND; i++) ctx->apbits[i] = 0;
    ctx->apbits[0] = 99;
    ctx->apbits[29] = 99;
    memset(ctx->apmy_ru, 0, sizeof ctx->apmy_ru);
    memset(ctx->aphis_fd, 0, sizeof ctx->aphis_fd);
    char my[13], his[13];
    int lm = 12, lh = 12;
    memcpy(my, mycall, 13); memcpy(his, hiscall, 13);
    while (lm > 0 && my[lm - 1] == ' ') lm--;
    while (lh > 0 && his[lh - 1] == ' ') lh--;
    my[lm] = '\0';
    his[lh] = '\0';
    if (lm >= 3) {
      bool nohiscall = false;
      char his0[13];
      strcpy(his0, his);
      if (lh < 3) {
        strcpy(his0, my);                             /* dummy hiscall: mycall won't be hashed */
        nohiscall = true;
      }
      char msg[64], msgsent[38], m37[38];
      snprintf(msg, sizeof msg, "%s %s RR73", my, his0);
      snprintf(m37, sizeof m37, "%-37.37s", msg);
      int i3 = -1, n3 = -1;
      memset(c77, '0', 77);
      c77[77] = '\0';
      ftx_pack77(&ctx->pack77, m37, &i3, &n3, c77);
      bool ok = ftx_unpack77(&ctx->pack77, c77, 1, msgsent);
      char sent37[38];
      snprintf(sent37, sizeof sent37, "%-37.37s", msgsent);
      if (i3 == 1 && memcmp(m37, sent37, 37) == 0 && ok && ftx_c77_to_bits(c77, message77)) {
        for (int i = 0; i < 28; i++) ctx->apmy_ru[i] = 2 * ((message77[i] + rvec[i + 1]) % 2) - 1;
        for (int i = 0; i < 28; i++) ctx->aphis_fd[i] = 2 * ((message77[29 + i] + rvec[28 + i]) % 2) - 1;
        for (int i = 0; i < 77; i++) message77[i] = (uint8_t)((message77[i] + rvec[i]) % 2);
        ftx_ldpc_encode174_91(message77, cw);
        for (int i = 0; i < 2 * ND; i++) ctx->apbits[i] = 2 * cw[i] - 1;
        if (nohiscall) ctx->apbits[29] = 99;
      }
    }
    memcpy(ctx->mycall0, mycall, 13);
    memcpy(ctx->hiscall0, hiscall, 13);
  }
  int ndecodes = 0;
  float fa = (float)p->nfa, fb = (float)p->nfb;
  for (int i = 0; i < NMAX; i++) dd[i] = iwave[i];

  float syncmin = 1.18f;
  bool dosubtract = true, doosd = true;
  int nsp = 3;
  if (p->ndepth == 2) doosd = false;
  if (p->ndepth == 1) {
    nsp = 1;
    dosubtract = false;
    doosd = false;
  }
  for (int isp = 1; isp <= nsp; isp++) {
    if (isp == 2) {
      if (ndecodes == 0) break;
      nd1 = ndecodes;
    } else if (isp == 3) {
      int nd2 = ndecodes - nd1;
      if (nd2 == 0) break;
    }
    int ncand = ftx_ft4_candidates(ctx, dd, fa, fb, syncmin, p->nfqso, FT4_MAXCAND, savg, candidate, sbase);
    bool dobigfft = true;
    for (int icand = 0; icand < ncand; icand++) {
      float f0 = candidate[icand].freq;
      float snr = candidate[icand].snr - 1.0f;
      ftx_ft4_downsample(ctx, dd, dobigfft, f0, (fftwf_complex *)cd2);   /* downsample to 32 samples/symbol */
      if (dobigfft) dobigfft = false;
      {
        float complex acc = 0.0f;
        for (int i = 0; i < NDMAX; i++) acc += cd2[i] * conjf(cd2[i]);
        float sum2 = crealf(acc / ((float)NMAX / (float)FT4_NDOWN));
        /* the Fortran sums the complex products and divides; the imaginary part is 0 */
        if (sum2 > 0.0f) {
          float r = sqrtf(sum2);
          for (int i = 0; i < NDMAX; i++) cd2[i] = cd2[i] / r;
        }
      }
      int ibest = -1, idfbest = 0, nharderror = -1;
      float smax = -99.0f;
      for (int iseg = 1; iseg <= 3; iseg++) {         /* DT search over 3 segments */
        for (int isync = 1; isync <= 2; isync++) {
          int idfmin, idfmax, idfstp, ibmin, ibmax, ibstp;
          if (isync == 1) {
            idfmin = -12; idfmax = 12; idfstp = 3;
            ibmin = -344; ibmax = 1012;
            if (iseg == 1) { ibmin = 108; ibmax = 560; }
            else if (iseg == 2) { smax1 = smax; ibmin = 560; ibmax = 1012; }
            else { ibmin = -344; ibmax = 108; }
            ibstp = 4;
          } else {
            idfmin = idfbest - 4; idfmax = idfbest + 4; idfstp = 1;
            ibmin = ibest - 5; ibmax = ibest + 5; ibstp = 1;
          }
          ibest = -1;
          idfbest = 0;
          smax = -99.0f;
          for (int idf = idfmin; idf <= idfmax; idf += idfstp) {
            for (int istart = ibmin; istart <= ibmax; istart += ibstp) {
              float sync = ftx_ft4_sync4d(ctx, (fftwf_complex *)cd2, istart, (fftwf_complex *)ctx->ctwk2[idf + 16]);
              if (sync > smax) {
                smax = sync;
                ibest = istart;
                idfbest = idf;
              }
            }
          }
        }
        if (iseg == 1) smax1 = smax;
        if (smax < 1.2f) continue;
        if (iseg > 1 && smax < smax1) continue;
        float f1 = f0 + (float)idfbest;
        if (f1 <= 10.0f || f1 >= 4990.0f) continue;
        ftx_ft4_downsample(ctx, dd, dobigfft, f1, (fftwf_complex *)cb_);   /* final downsample, corrected f0 */
        {
          float sum2 = 0.0f;
          for (int i = 0; i < NDMAX; i++) {
            float m = cabsf(cb_[i]);
            sum2 += m * m;
          }
          sum2 = sum2 / ((float)NSS * NN);
          if (sum2 > 0.0f) {
            float r = sqrtf(sum2);
            for (int i = 0; i < NDMAX; i++) cb_[i] = cb_[i] / r;
          }
        }
        for (int i = 0; i < NN * NSS; i++) cd[i] = 0.0f;
        if (ibest >= 0) {
          int it = ibest + NN * NSS - 1;
          if (it > NDMAX - 1) it = NDMAX - 1;
          int np = it - ibest + 1;
          for (int i = 0; i < np; i++) cd[i] = cb_[ibest + i];
        } else {
          for (int i = 0; i < NN * NSS + 2 * ibest; i++) cd[-ibest + i] = cb_[i];
        }
        bool badsync;
        get_ft4_bitmetrics(ctx, cd, bitmetrics, &badsync);
        if (badsync) continue;
        for (int i = 0; i < 2 * NN; i++) hbits[i] = bitmetrics[0][i] >= 0 ? 1 : 0;
        static const uint8_t p1[8] = { 0,0,0,1,1,0,1,1 }, p2[8] = { 0,1,0,0,1,1,1,0 },
                             p3[8] = { 1,1,1,0,0,1,0,0 }, p4[8] = { 1,0,1,1,0,0,0,1 };
        int nsq = 0;
        for (int i = 0; i < 8; i++) {
          nsq += hbits[i] == p1[i];
          nsq += hbits[66 + i] == p2[i];
          nsq += hbits[132 + i] == p3[i];
          nsq += hbits[198 + i] == p4[i];
        }
        if (nsq < 20) continue;

        float scalefac = 2.83f;
        for (int m = 0; m < 3; m++) {
          float *l = m == 0 ? llra : (m == 1 ? llrb : llrc);
          for (int i = 0; i < 58; i++) l[i] = bitmetrics[m][8 + i];
          for (int i = 0; i < 58; i++) l[58 + i] = bitmetrics[m][74 + i];
          for (int i = 0; i < 58; i++) l[116 + i] = bitmetrics[m][140 + i];
          for (int i = 0; i < 2 * ND; i++) l[i] = scalefac * l[i];
        }
        float apmag = 0.0f;
        for (int i = 0; i < 2 * ND; i++) if (fabsf(llra[i]) > apmag) apmag = fabsf(llra[i]);
        apmag = apmag * 1.1f;
        int npasses = 3 + nappasses[p->nQSOProgress];
        if (p->lapcqonly) npasses = 4;
        if (p->ndepth == 1) npasses = 3;
        if (p->ncontest >= 6) npasses = 3;              /* no Fox and Hound */
        int iaptype = 0;
        for (int ipass = 1; ipass <= npasses; ipass++) {
          if (ipass == 1) memcpy(llr, llra, sizeof llr);
          if (ipass == 2) memcpy(llr, llrb, sizeof llr);
          if (ipass == 3) memcpy(llr, llrc, sizeof llr);
          if (ipass <= 3) {
            memset(apmask, 0, sizeof apmask);
            iaptype = 0;
          }
          if (ipass > 3) {
            memcpy(llrd, llrc, sizeof llrd);
            iaptype = naptypes[p->nQSOProgress][ipass - 4];
            if (p->lapcqonly) iaptype = 1;
            int nc = p->ncontest;
            int napwid = 50;
            if (nc <= 5 && iaptype >= 3 && fabsf(f1 - p->nfqso) > napwid) continue;
            if (iaptype >= 2 && ctx->apbits[0] > 1) continue;
            if (iaptype >= 3 && ctx->apbits[29] > 1) continue;
            if (iaptype == 1) {
              memset(apmask, 0, sizeof apmask);
              for (int i = 0; i < 29; i++) apmask[i] = 1;
              const int *m = NULL;
              if (nc == 0) m = ctx->mcq;
              if (nc == 1 || nc == 2) m = ctx->mcqtest;
              if (nc == 3) m = ctx->mcqfd;
              if (nc == 4) m = ctx->mcqru;
              if (nc == 5) m = ctx->mcqww;
              if (m != NULL) for (int i = 0; i < 29; i++) llrd[i] = apmag * m[i];
            }
            if (iaptype == 2) {
              memset(apmask, 0, sizeof apmask);
              if (nc == 0 || nc == 1 || nc == 5) {
                for (int i = 0; i < 29; i++) { apmask[i] = 1; llrd[i] = apmag * ctx->apbits[i]; }
              } else if (nc == 2 || nc == 3) {
                for (int i = 0; i < 28; i++) { apmask[i] = 1; llrd[i] = apmag * ctx->apbits[i]; }
              } else if (nc == 4) {
                for (int i = 1; i < 29; i++) { apmask[i] = 1; llrd[i] = apmag * ctx->apmy_ru[i - 1]; }
              }
            }
            if (iaptype == 3) {
              memset(apmask, 0, sizeof apmask);
              if (nc == 0 || nc == 1 || nc == 2 || nc == 5) {
                for (int i = 0; i < 58; i++) { apmask[i] = 1; llrd[i] = apmag * ctx->apbits[i]; }
              } else if (nc == 3) {
                for (int i = 0; i < 56; i++) apmask[i] = 1;
                for (int i = 0; i < 28; i++) llrd[i] = apmag * ctx->apbits[i];
                for (int i = 0; i < 28; i++) llrd[28 + i] = apmag * ctx->aphis_fd[i];
              } else if (nc == 4) {
                for (int i = 1; i < 57; i++) apmask[i] = 1;
                for (int i = 1; i < 29; i++) llrd[i] = apmag * ctx->apmy_ru[i - 1];
                for (int i = 29; i < 57; i++) llrd[i] = apmag * ctx->apbits[i];
              }
            }
            if (iaptype == 4 || iaptype == 5 || iaptype == 6) {
              memset(apmask, 0, sizeof apmask);
              if (nc <= 5) {
                for (int i = 0; i < 77; i++) apmask[i] = 1;   /* mycall, hiscall, RRR|73|RR73 */
                if (iaptype == 6) for (int i = 0; i < 77; i++) llrd[i] = apmag * ctx->apbits[i];
              }
            }
            memcpy(llr, llrd, sizeof llr);
          }
          memset(message77, 0, sizeof message77);
          float dmin = 0.0f;
          int ndeep = 2, maxosd = 2;
          if (fabsf(p->nfqso - f1) <= 50) {
            ndeep = 2;
            maxosd = 3;
          }
          if (!doosd) maxosd = -1;
          int keff = 91, ntype = 0;
          ftx_ldpc_decode174_91(&ctx->ldpc, llr, keff, maxosd, ndeep, apmask, message91, cw, &ntype, &nharderror, &dmin);
          memcpy(message77, message91, 77);
          {
            int sum = 0;
            for (int i = 0; i < 77; i++) sum += message77[i];
            if (sum == 0) continue;
          }
          if (nharderror >= 0) {
            for (int i = 0; i < 77; i++) message77[i] = (uint8_t)((message77[i] + rvec[i]) % 2);   /* unscramble */
            ftx_bits_to_c77(message77, c77);
            bool ok = ftx_unpack77(&ctx->pack77, c77, 1, message);
            if (!ok) break;
            if (dosubtract) {
              ft4_tones_from_77bits(message77, i4tone);
              float dt = (float)ibest / 666.67f;
              subtractft4(ctx, dd, i4tone, f1, dt);
            }
            char m37[38];
            snprintf(m37, sizeof m37, "%-37.37s", message);
            int idupe = 0;
            for (int i = 0; i < ndecodes; i++)
              if (memcmp(decodes[i], m37, 37) == 0) idupe = 1;
            if (idupe == 1) break;
            if (ndecodes < 100) memcpy(decodes[ndecodes], m37, 38);
            ndecodes++;
            float xsnr = snr > 0.0f ? 10 * log10f(snr) - 14.8f : -21.0f;
            int nsnr = (int)lroundf(fmaxf(-21.0f, xsnr));
            float xdt = ibest / 666.67f - 0.5f;
            float qual = 1.0f - (nharderror + dmin) / 60.0f;
            if (cb != NULL) cb(user, smax, nsnr, xdt, f1, message, iaptype, qual);
            break;
          }
        }                                             /* sequence estimation */
        if (nharderror >= 0) break;
      }                                               /* 3 DT segments */
    }                                                 /* candidate list */
  }                                                   /* subtraction loop */
}

void ftx_ft4_format_line(char *buf, size_t len, int nutc, int snr, float dt, float freq,
                         const char *decoded, int nap, float qual)
{
  char d37[38], annot[3] = "  ";
  snprintf(d37, sizeof d37, "%-37.37s", decoded);
  if (nap != 0) {
    snprintf(annot, sizeof annot, "a%u", (unsigned)nap % 10u);
    if (qual < 0.17f) d37[36] = '?';
  }
  snprintf(buf, len, "%06d%4d%5.1f%5d +  %s %s", nutc, snr, dt, (int)lroundf(freq), d37, annot);
}
