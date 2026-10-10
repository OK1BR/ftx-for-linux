/* ftx_ft8_demod.c — FT8 per-candidate demodulation and decoding.
 *
 * Ported by hand to C from WSJT-X v3.0.2 (git ccdfaf3), lib/ft8/ft8b.f90
 * (ft8b, normalizebmet), ft8apset.f90, genft8.f90 (get_ft8_tones_from_77bits),
 * gen_ft8wave.f90, subtractft8.f90, lib/peakup.f90, ft2/gfsk_pulse.f90 and
 * stdcall from qra/q65/q65_set_list.f90.  See ftx_ft8.h for the WSJT-X
 * copyright and the GPL-3.0-or-later notice.
 * This port: Copyright (C) 2026 Richard Fakenberg, OK1BR.
 */
#include <complex.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ftx_ft8_internal.h"
#include "ftx_internal.h"

static const int icos7[7] = { 3, 1, 4, 0, 6, 5, 2 };
static const int graymap[8] = { 0, 1, 3, 2, 5, 6, 4, 7 };

/* ------------------------------------------------------------------------
 * Context part: gen_ft8wave tables, subtractft8 filter and buffers
 * ---------------------------------------------------------------------- */
bool ftx_ft8_demod_init(ftx_ft8_ctx *ctx)
{
  const int NFFT = FT8_NMAX, NFILT = FT8_NFILT;
  ctx->gw_fchk0 = 0.0f;
  {                                                   /* gen_ft8wave phase table */
    const float twopi = 8.0f * atanf(1.0f);
    for (int i = 0; i < FT8_NTAB; i++) {
      float phi = i * twopi / FT8_NTAB;
      ctx->gw_ctab[i] = FTX_CMPLXF(cosf(phi), sinf(phi));
    }
  }
  ctx->gw_dphi = calloc((size_t)(FT8_NN + 2) * 4 * FT8_NSPS, sizeof(float));   /* up to nsps 7680 (48 kHz TX) */
  ftx_fft_plan_c2c(&ctx->fft_sub_fwd, NFFT, -1);
  ftx_fft_plan_c2c(&ctx->fft_sub_bwd, NFFT, +1);
  ftx_fft_plan_r2c(&ctx->fft_sub_r2c, NFFT);
  ftx_fft_plan_c2c(&ctx->fft_a8, FT8_NDS, -1);
  ctx->sub_cw = fftwf_malloc((size_t)NFFT * sizeof(float complex));
  ctx->sub_cref = calloc((size_t)1920 * 79, sizeof(float complex));
  ctx->sub_camp = calloc((size_t)NFFT, sizeof(float complex));
  ctx->sub_dd = calloc((size_t)NFFT, sizeof(float));
  if (ctx->gw_dphi == NULL || ctx->sub_cw == NULL || ctx->sub_cref == NULL || ctx->sub_camp == NULL ||
      ctx->sub_dd == NULL)
    return false;

  /* subtractft8 "first" block: the normalised filter and its spectrum */
  {
    const float pi = 4.0f * atanf(1.0f);
    float fac = 1.0f / (float)NFFT;
    float window[FT8_NFILT + 1];                      /* window(-NFILT/2:NFILT/2) */
    float sumw = 0.0f;
    for (int j = -NFILT / 2; j <= NFILT / 2; j++) {
      float c = cosf(pi * j / NFILT);
      window[j + NFILT / 2] = c * c;
      sumw = sumw + window[j + NFILT / 2];
    }
    float complex *cw = (float complex *)ctx->fft_sub_fwd.c;   /* plan buffer as scratch */
    for (int i = 0; i < NFFT; i++) cw[i] = 0.0f;
    for (int i = 0; i <= NFILT; i++) cw[i] = window[i] / sumw;
    {                                                 /* cw=cshift(cw,NFILT/2+1) */
      float complex *tmp = ctx->sub_cw;
      int shift = NFILT / 2 + 1;
      for (int m = 0; m < NFFT; m++) tmp[m] = cw[(m + shift) % NFFT];
      memcpy(cw, tmp, (size_t)NFFT * sizeof(float complex));
    }
    ftx_fft_exec(&ctx->fft_sub_fwd);
    for (int i = 0; i < NFFT; i++) ctx->sub_cw[i] = cw[i] * fac;
    for (int j = 1; j <= NFILT / 2 + 1; j++) {
      float s = 0.0f;
      for (int q = j - 1; q <= NFILT / 2; q++) s += window[q + NFILT / 2];
      ctx->sub_endcorrection[j - 1] = 1.0f / (1.0f - s / sumw);
    }
  }
  return true;
}

void ftx_ft8_demod_free(ftx_ft8_ctx *ctx)
{
  free(ctx->gw_dphi);
  ftx_fft_free(&ctx->fft_sub_fwd);
  ftx_fft_free(&ctx->fft_sub_bwd);
  ftx_fft_free(&ctx->fft_sub_r2c);
  ftx_fft_free(&ctx->fft_a8);
  fftwf_free(ctx->sub_cw);
  free(ctx->sub_cref);
  free(ctx->sub_camp);
  free(ctx->sub_dd);
}

/* ------------------------------------------------------------------------
 * stdcall, ft8apset
 * ---------------------------------------------------------------------- */
bool ftx_ft8_stdcall(const char *callsign)           /* stdcall: callsign up to 12 chars, NUL terminated */
{
  int n = (int)strlen(callsign);
  while (n > 0 && callsign[n - 1] == ' ') n--;
  int i;
  for (i = n; i >= 2; i--)
    if (callsign[i - 1] >= '0' && callsign[i - 1] <= '9') break;
  int iarea = i;
  int npdig = 0, nplet = 0, nslet = 0;
  for (i = 1; i <= iarea - 1; i++) {
    char c = callsign[i - 1];
    if (c >= '0' && c <= '9') npdig++;
    if (c >= 'A' && c <= 'Z') nplet++;
  }
  for (i = iarea + 1; i <= n; i++)
    if (callsign[i - 1] >= 'A' && callsign[i - 1] <= 'Z') nslet++;
  return !(iarea < 2 || iarea > 3 || nplet == 0 || npdig >= iarea - 1 || nslet > 3);
}

static void trim12(char *dst, const char *src)        /* dst = trim(src(1:12)) */
{
  int n = 0;
  while (n < 12 && src[n] != '\0') n++;
  while (n > 0 && src[n - 1] == ' ') n--;
  memcpy(dst, src, (size_t)n);
  dst[n] = '\0';
}

void ftx_ft8apset(ftx_ft8_ctx *ctx, const char *mycall12, const char *hiscall12, int ncontest,
                  int apsym[58], int aph10[10])
{
  char mycall[13], hiscall[13], msg[64], c77[78], msgchk[38];
  trim12(mycall, mycall12);
  trim12(hiscall, hiscall12);

  for (int i = 0; i < 58; i++) apsym[i] = 0;
  apsym[0] = 99;
  apsym[29] = 99;
  for (int i = 0; i < 10; i++) aph10[i] = 0;
  aph10[0] = 99;
  if (strlen(mycall) < 3) return;

  bool nohiscall = false;
  if (strlen(hiscall) < 3) {
    strcpy(hiscall, "KA1ABC");                        /* dummy hiscall */
    nohiscall = true;
  } else {
    char hc13[14];
    snprintf(hc13, sizeof hc13, "%-13.13s", hiscall);
    int n10 = 0, n12 = 0, n22 = 0;
    ftx_pack77_save_hash_call(&ctx->pack77, hc13, &n10, &n12, &n22);
    for (int i = 0; i < 10; i++) aph10[i] = 2 * ((n10 >> (9 - i)) & 1) - 1;
  }

  /* encode a dummy standard message: i3=1 */
  snprintf(msg, sizeof msg, "%s %s RRR", mycall, hiscall);
  if (!ftx_ft8_stdcall(mycall)) snprintf(msg, sizeof msg, "<%s> %s RRR", mycall, hiscall);
  int i3 = 0, n3 = 0;
  memset(c77, '0', 77);
  c77[77] = '\0';
  ftx_pack77(&ctx->pack77, msg, &i3, &n3, c77);
  bool ok = ftx_unpack77(&ctx->pack77, c77, 1, msgchk);
  char msg37[38];
  snprintf(msg37, sizeof msg37, "%-37.37s", msg);
  {
    int n = 37;
    while (n > 0 && msg37[n - 1] == ' ') n--;
    msg37[n] = '\0';
  }
  if (ncontest == 7 && (i3 != 1 || !ok)) return;
  if (ncontest <= 5 && (i3 != 1 || strcmp(msg37, msgchk) != 0 || !ok)) return;

  for (int i = 0; i < 58; i++) {
    if (c77[i] != '0' && c77[i] != '1') {             /* read err → apsym reset */
      for (int k = 0; k < 58; k++) apsym[k] = 0;
      apsym[0] = 99;
      apsym[29] = 99;
      return;
    }
    apsym[i] = 2 * (c77[i] == '1') - 1;
  }
  if (nohiscall) {
    apsym[29] = 99;
    aph10[0] = 99;
  }
}

/* ------------------------------------------------------------------------
 * genft8: tones from 77 bits
 * ---------------------------------------------------------------------- */
void ftx_ft8_tones_from_77bits(const uint8_t message77[77], int itone[79])
{
  uint8_t codeword[174];
  ftx_ldpc_encode174_91(message77, codeword);
  for (int i = 0; i < 7; i++) {                       /* S7 D29 S7 D29 S7 */
    itone[i] = icos7[i];
    itone[36 + i] = icos7[i];
    itone[FT8_NN - 7 + i] = icos7[i];
  }
  int k = 7;
  for (int j = 1; j <= FT8_ND; j++) {
    int i = 3 * j - 2;
    k++;
    if (j == 30) k += 7;
    int indx = codeword[i - 1] * 4 + codeword[i] * 2 + codeword[i + 1];
    itone[k - 1] = graymap[indx];
  }
}

/* ------------------------------------------------------------------------
 * gen_ft8wave (complex output)
 * ---------------------------------------------------------------------- */
static float gfsk_pulse(float b, float t)
{
  float pi = 4.0f * atanf(1.0f);
  float c = pi * sqrtf(2.0f / logf(2.0f));
  return 0.5f * (erff(c * b * (t + 0.5f)) - erff(c * b * (t - 0.5f)));
}

/* gen_ft8wave: the smoothed frequency waveform dphi, (nsym+2)*nsps samples */
static void gen_ft8wave_dphi(ftx_ft8_ctx *ctx, const int *itone, int nsym, int nsps, float bt, float fsample,
                             float f0)
{
  const float twopi = 8.0f * atanf(1.0f);
  float dt = 1.0f / fsample;
  float hmod = 1.0f;
  float fchk = nsym + nsps + bt + fsample;
  if (fchk != ctx->gw_fchk0) {                        /* compute the frequency-smoothing pulse */
    for (int i = 1; i <= 3 * nsps; i++) {
      float tt = (i - 1.5f * nsps) / (float)nsps;
      ctx->gw_pulse[i - 1] = gfsk_pulse(bt, tt);
    }
    ctx->gw_fchk0 = fchk;
  }
  float *dphi = ctx->gw_dphi;
  int ndphi = (nsym + 2) * nsps;
  float dphi_peak = twopi * hmod / (float)nsps;
  for (int i = 0; i < ndphi; i++) dphi[i] = 0.0f;
  for (int j = 1; j <= nsym; j++) {
    int ib = (j - 1) * nsps;
    for (int i = 0; i < 3 * nsps; i++) dphi[ib + i] = dphi[ib + i] + dphi_peak * ctx->gw_pulse[i] * itone[j - 1];
  }
  /* dummy symbols at both ends with the first/last tone */
  for (int i = 0; i < 2 * nsps; i++) dphi[i] = dphi[i] + dphi_peak * itone[0] * ctx->gw_pulse[nsps + i];
  for (int i = 0; i < 2 * nsps; i++)
    dphi[nsym * nsps + i] = dphi[nsym * nsps + i] + dphi_peak * itone[nsym - 1] * ctx->gw_pulse[i];
  for (int i = 0; i < ndphi; i++) dphi[i] = dphi[i] + twopi * f0 * dt;   /* shift up by f0 */
}

void ftx_gen_ft8wave(ftx_ft8_ctx *ctx, const int *itone, int nsym, int nsps, float bt, float fsample,
                     float f0, fftwf_complex *cwavev, int nwave)
{
  float complex *cwave = (float complex *)cwavev;
  const float twopi = 8.0f * atanf(1.0f);
  gen_ft8wave_dphi(ctx, itone, nsym, nsps, bt, fsample, f0);
  const float *dphi = ctx->gw_dphi;
  float phi = 0.0f;
  for (int k = 0; k < nwave; k++) cwave[k] = 0.0f;
  int k = 0;
  for (int j = nsps; j <= nsps + nwave - 1; j++) {   /* skip the dummy symbol */
    int i = (int)(phi * (float)FT8_NTAB / twopi);
    cwave[k++] = ctx->gw_ctab[i];
    phi = fmodf(phi + dphi[j], twopi);
  }
  int nramp = (int)lroundf(nsps / 8.0f);              /* envelope shaping of the first and last symbol */
  for (int i = 0; i < nramp; i++)
    cwave[i] = cwave[i] * ((1.0f - cosf(twopi * i / (2.0f * nramp))) / 2.0f);
  int k1 = nsym * nsps - nramp + 1;
  for (int i = 0; i < nramp; i++) {
    int idx = k1 - 1 + i;
    if (idx < nwave) cwave[idx] = cwave[idx] * ((1.0f + cosf(twopi * i / (2.0f * nramp))) / 2.0f);
  }
}

void ftx_ft8_tx_wave(ftx_ft8_ctx *ctx, const int itone[79], int nsps, float fsample, float f0,
                     float *wave, int nwave)
{
  const float twopi = 8.0f * atanf(1.0f);
  gen_ft8wave_dphi(ctx, itone, FT8_NN, nsps, 2.0f, fsample, f0);
  const float *dphi = ctx->gw_dphi;
  float phi = 0.0f;
  for (int k = 0; k < nwave; k++) wave[k] = 0.0f;
  int k = 0;
  for (int j = nsps; j <= nsps + nwave - 1; j++) {
    wave[k++] = sinf(phi);
    phi = fmodf(phi + dphi[j], twopi);
  }
  int nramp = (int)lroundf(nsps / 8.0f);
  for (int i = 0; i < nramp; i++)
    wave[i] = wave[i] * ((1.0f - cosf(twopi * i / (2.0f * nramp))) / 2.0f);
  int k1 = FT8_NN * nsps - nramp + 1;
  for (int i = 0; i < nramp; i++) {
    int idx = k1 - 1 + i;
    if (idx < nwave) wave[idx] = wave[idx] * ((1.0f + cosf(twopi * i / (2.0f * nramp))) / 2.0f);
  }
}

bool ftx_ft8_tx_tones(ftx_ft8_ctx *ctx, const char *msg, char msgsent[38], int itone[79])
{
  uint8_t msgbits[77];
  int i3, n3;
  ftx_ft8_genft8(ctx, msg, &i3, &n3, msgsent, msgbits, itone);
  return strcmp(msgsent, "*** bad message ***") != 0;
}

/* ------------------------------------------------------------------------
 * subtractft8
 * ---------------------------------------------------------------------- */
#define SUB_NMAX   FT8_NMAX
#define SUB_NFRAME (1920 * 79)
#define SUB_NFFT   FT8_NMAX
#define SUB_NFILT  FT8_NFILT

static void peakup(float ym, float y0, float yp, float *dx)
{
  float b = yp - ym;
  float c = yp + ym - 2.0f * y0;
  *dx = -b / (2.0f * c);
}

/* Internal function sqf of the Fortran: subtract with time offset idt from a
 * fresh copy of dd0 into ctx->sub_dd; returns the residual power in the
 * signal's band when ldt is set. */
static float sub_sqf(ftx_ft8_ctx *ctx, const float *dd0, float f0, float dt, int idt, bool ldt)
{
  float *dd = ctx->sub_dd;
  float complex *camp = ctx->sub_camp;
  float complex *cfilt = (float complex *)ctx->fft_sub_fwd.c;
  float complex *cref = ctx->sub_cref;
  float *x = ctx->fft_sub_r2c.r;
  float complex *cx = (float complex *)ctx->fft_sub_r2c.c;

  int nstart = (int)(dt * 12000 + 1 + idt);
  for (int i = 0; i < SUB_NMAX; i++) camp[i] = 0.0f;
  memcpy(dd, dd0, SUB_NMAX * sizeof(float));
  for (int i = 1; i <= SUB_NFRAME; i++) {
    int j = nstart - 1 + i;
    if (j >= 1 && j <= SUB_NMAX) camp[i - 1] = dd[j - 1] * conjf(cref[i - 1]);
  }
  memcpy(cfilt, camp, SUB_NFRAME * sizeof(float complex));
  for (int i = SUB_NFRAME; i < SUB_NFFT; i++) cfilt[i] = 0.0f;
  ftx_fft_exec(&ctx->fft_sub_fwd);
  for (int i = 0; i < SUB_NFFT; i++) cfilt[i] = cfilt[i] * ctx->sub_cw[i];
  memcpy(ctx->fft_sub_bwd.c, cfilt, SUB_NFFT * sizeof(float complex));
  ftx_fft_exec(&ctx->fft_sub_bwd);
  cfilt = (float complex *)ctx->fft_sub_bwd.c;
  for (int i = 0; i <= SUB_NFILT / 2; i++) cfilt[i] = cfilt[i] * ctx->sub_endcorrection[i];
  for (int m = 0; m <= SUB_NFILT / 2; m++)
    cfilt[SUB_NFRAME - 1 - m] = cfilt[SUB_NFRAME - 1 - m] * ctx->sub_endcorrection[m];
  for (int i = 0; i < SUB_NFFT + 2; i++) x[i] = 0.0f;
  for (int i = 1; i <= SUB_NFRAME; i++) {
    int j = nstart + i - 1;
    if (j >= 1 && j <= SUB_NMAX) {
      float complex z = cfilt[i - 1] * cref[i - 1];
      dd[j - 1] = dd[j - 1] - 2.0f * crealf(z);        /* subtract the reconstructed signal */
      x[i - 1] = dd[j - 1];
    }
  }
  float sqq = 0.0f;
  if (ldt) {
    ftx_fft_exec(&ctx->fft_sub_r2c);
    float df = 12000.0f / SUB_NFFT;
    int ia = (int)((f0 - 1.5f * 6.25f) / df);
    int ib = (int)((f0 + 8.5f * 6.25f) / df);
    for (int i = ia; i <= ib; i++) sqq = sqq + crealf(cx[i]) * crealf(cx[i]) + cimagf(cx[i]) * cimagf(cx[i]);
  }
  return sqq;
}

void ftx_ft8_subtract(ftx_ft8_ctx *ctx, float *dd0, const int itone[79], float f0, float dt, bool lrefinedt)
{
  ftx_gen_ft8wave(ctx, itone, 79, 1920, 2.0f, 12000.0f, f0, (fftwf_complex *)ctx->sub_cref, SUB_NFRAME);
  bool ldt = lrefinedt;
  if (ldt) {                                          /* refine DT first */
    float sqa = sub_sqf(ctx, dd0, f0, dt, -90, ldt);
    float sqb = sub_sqf(ctx, dd0, f0, dt, +90, ldt);
    float sq0 = sub_sqf(ctx, dd0, f0, dt, 0, ldt);
    float dx;
    peakup(sqa, sq0, sqb, &dx);
    if (fabsf(dx) > 1.0f) return;                     /* no acceptable minimum: do not subtract */
    int i2 = (int)lroundf(90.0f * dx);
    ldt = false;
    sub_sqf(ctx, dd0, f0, dt, i2, ldt);
  } else {
    sub_sqf(ctx, dd0, f0, dt, 0, ldt);
  }
  memcpy(dd0, ctx->sub_dd, SUB_NMAX * sizeof(float));
}

/* ------------------------------------------------------------------------
 * ft8b
 * ---------------------------------------------------------------------- */
static void normalizebmet(float *bmet, int n)
{
  float bmetav = 0.0f, bmet2av = 0.0f;
  for (int i = 0; i < n; i++) bmetav += bmet[i];
  bmetav = bmetav / (float)n;
  for (int i = 0; i < n; i++) bmet2av += bmet[i] * bmet[i];
  bmet2av = bmet2av / (float)n;
  float var = bmet2av - bmetav * bmetav;
  float bmetsig = var > 0.0f ? sqrtf(var) : sqrtf(bmet2av);
  for (int i = 0; i < n; i++) bmet[i] = bmet[i] / bmetsig;
}

/* AP message templates, already in the +1/-1 form (2*x-1) of the Fortran */
#define B(x) (2 * (x) - 1)
static const int mcq[29]     = { B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(1),B(0),B(0) };
static const int mcqru[29]   = { B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(1),B(0),B(1),B(1),B(1),B(1),B(0),B(0),B(1),B(1),B(0),B(0) };
static const int mcqfd[29]   = { B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(1),B(0),B(0),B(1),B(0),B(0),B(1),B(0),B(0),B(0),B(1),B(0) };
static const int mcqtest[29] = { B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(1),B(1),B(0),B(0),B(0),B(0),B(1),B(0),B(1),B(0),B(1),B(1),B(1),B(1),B(1),B(1),B(0),B(0),B(1),B(0) };
static const int mcqww[29]   = { B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(0),B(1),B(1),B(0),B(0),B(1),B(1),B(0),B(1),B(1),B(1),B(1),B(0) };
static const int mrrr[19]    = { B(0),B(1),B(1),B(1),B(1),B(1),B(1),B(0),B(1),B(0),B(0),B(1),B(0),B(0),B(1),B(0),B(0),B(0),B(1) };
static const int m73[19]     = { B(0),B(1),B(1),B(1),B(1),B(1),B(1),B(0),B(1),B(0),B(0),B(1),B(0),B(1),B(0),B(0),B(0),B(0),B(1) };
static const int mrr73[19]   = { B(0),B(1),B(1),B(1),B(1),B(1),B(1),B(0),B(0),B(1),B(1),B(1),B(0),B(1),B(0),B(1),B(0),B(0),B(1) };
#undef B
static const int nappasses[6] = { 2, 2, 2, 4, 4, 3 };
static const int naptypes[6][4] = { { 1, 2, 0, 0 }, { 2, 3, 0, 0 }, { 2, 3, 0, 0 },
                                    { 3, 4, 5, 6 }, { 3, 4, 5, 6 }, { 3, 1, 2, 0 } };

static inline bool one_bit(int i, int j) { return (i & (1 << j)) != 0; }   /* one(i,j) */

/* maxval(s2(0:nt-1), mask) with mask = one(i,bit) (want=true) or its complement */
static float masked_max(const float *s2, int nt, int bit, bool want)
{
  float m = -3.4028235e38f;
  for (int i = 0; i < nt; i++)
    if (one_bit(i, bit) == want && s2[i] > m) m = s2[i];
  return m;
}

/* Shared by ft8b and ft8_a7d: downsample at f1, refine time and frequency,
 * compute the 79 symbol spectra.  Returns ibest (start sample at 200 S/s);
 * f1 is refined in place; cs/s8 receive the spectra. */
void ftx_ft8_refine(ftx_ft8_ctx *ctx, const float *dd0, bool *newdat, float *f1p, float xdt, int *ibestp,
                    float complex cs[FT8_NN][8], float s8[FT8_NN][8], int *nsyncp)
{
  const int NP2 = FT8_NP2, NN = FT8_NN;
  float complex cd0v[FT8_NDS];
  float complex ctwk[32], csymb[32];
  float f1 = *f1p;
  float fs2 = 12000.0f / FT8_NDOWN;
  float dt2 = 1.0f / fs2;
  float twopi = 8.0f * atanf(1.0f);
  float delfbest = 0.0f;
  int ibest = 0;

  ftx_ft8_downsample(ctx, dd0, newdat, f1, (fftwf_complex *)cd0v);   /* mix f1 to baseband */
  int i0 = (int)lroundf((xdt + 0.5f) * fs2);           /* initial guess for the start */
  float smax = 0.0f;
  for (int idt = i0 - 10; idt <= i0 + 10; idt++) {    /* +/- one quarter symbol */
    float sync = ftx_ft8_sync8d(ctx, (fftwf_complex *)cd0v, idt, (fftwf_complex *)ctwk, 0);
    if (sync > smax) {
      smax = sync;
      ibest = idt;
    }
  }
  smax = 0.0f;                                        /* peak up in frequency, +/- 2.5 Hz */
  for (int ifr = -5; ifr <= 5; ifr++) {
    float delf = ifr * 0.5f;
    float dphi = twopi * delf * dt2;
    float phi = 0.0f;
    for (int i = 0; i < 32; i++) {
      ctwk[i] = FTX_CMPLXF(cosf(phi), sinf(phi));
      phi = fmodf(phi + dphi, twopi);
    }
    float sync = ftx_ft8_sync8d(ctx, (fftwf_complex *)cd0v, ibest, (fftwf_complex *)ctwk, 1);
    if (sync > smax) {
      smax = sync;
      delfbest = delf;
    }
  }
  {
    float a[5] = { -delfbest, 0.0f, 0.0f, 0.0f, 0.0f };
    ftx_ft8_twkfreq1((fftwf_complex *)cd0v, NP2, fs2, a, (fftwf_complex *)cd0v);
  }
  f1 = f1 + delfbest;                                 /* improved estimate of DF */
  {
    bool nd = false;
    ftx_ft8_downsample(ctx, dd0, &nd, f1, (fftwf_complex *)cd0v);
  }
  float ss[9];
  for (int idt = -4; idt <= 4; idt++)                 /* +/- one quarter symbol */
    ss[idt + 4] = ftx_ft8_sync8d(ctx, (fftwf_complex *)cd0v, ibest + idt, (fftwf_complex *)ctwk, 0);
  {
    int iloc = 0;
    for (int i = 1; i < 9; i++)
      if (ss[i] > ss[iloc]) iloc = i;
    ibest = iloc - 4 + ibest;
  }

  for (int k = 1; k <= NN; k++) {                     /* symbol spectra */
    int i1 = ibest + (k - 1) * 32;
    for (int i = 0; i < 32; i++) csymb[i] = 0.0f;
    if (i1 >= 0 && i1 + 31 <= NP2 - 1)
      for (int i = 0; i < 32; i++) csymb[i] = cd0v[i1 + i];
    memcpy(ctx->fft_sym32.c, csymb, sizeof csymb);
    ftx_fft_exec(&ctx->fft_sym32);
    float complex *out = (float complex *)ctx->fft_sym32.c;
    for (int t = 0; t < 8; t++) {
      cs[k - 1][t] = out[t] / 1e3f;
      s8[k - 1][t] = cabsf(out[t]);
    }
  }

  int is1 = 0, is2 = 0, is3 = 0;                      /* sync quality check */
  for (int k = 1; k <= 7; k++) {
    int ip;
    ip = 0; for (int t = 1; t < 8; t++) if (s8[k - 1][t] > s8[k - 1][ip]) ip = t;
    if (icos7[k - 1] == ip) is1++;
    ip = 0; for (int t = 1; t < 8; t++) if (s8[k + 35][t] > s8[k + 35][ip]) ip = t;
    if (icos7[k - 1] == ip) is2++;
    ip = 0; for (int t = 1; t < 8; t++) if (s8[k + 71][t] > s8[k + 71][ip]) ip = t;
    if (icos7[k - 1] == ip) is3++;
  }
  *nsyncp = is1 + is2 + is3;                          /* hard sync sum, max 21 */
  *ibestp = ibest;
  *f1p = f1;
}

/* Shared by ft8b and ft8_a7d: the bit metrics for 1, 2 and 3 symbols
 * (bmeta/b/c) and the normalised single-symbol metric (bmetd); imetric 2
 * squares the symbol magnitudes.  Not normalised here. */
void ftx_ft8_bitmetrics(const float complex cs[FT8_NN][8], int imetric, float *bmeta, float *bmetb,
                        float *bmetc, float *bmetd)
{
  float s2[512];
  for (int i = 0; i < 174; i++) { bmeta[i] = bmetb[i] = bmetc[i] = bmetd[i] = 0.0f; }
  for (int nsym = 1; nsym <= 3; nsym++) {
    int nt = 1 << (3 * nsym);
    for (int ihalf = 1; ihalf <= 2; ihalf++) {
      for (int k = 1; k <= 29; k += nsym) {
        int ks = ihalf == 1 ? k + 7 : k + 43;
        for (int i = 0; i < nt; i++) {
          int i1 = i / 64, i2 = (i & 63) / 8, i3 = i & 7;
          if (nsym == 1) s2[i] = cabsf(cs[ks - 1][graymap[i3]]);
          else if (nsym == 2) s2[i] = cabsf(cs[ks - 1][graymap[i2]] + cs[ks][graymap[i3]]);
          else s2[i] = cabsf(cs[ks - 1][graymap[i1]] + cs[ks][graymap[i2]] + cs[ks + 1][graymap[i3]]);
        }
        if (imetric == 2)
          for (int i = 0; i < nt; i++) s2[i] = s2[i] * s2[i];
        int i32 = 1 + (k - 1) * 3 + (ihalf - 1) * 87;
        int ibmax = nsym == 1 ? 2 : (nsym == 2 ? 5 : 8);
        for (int ib = 0; ib <= ibmax; ib++) {
          float mx1 = masked_max(s2, nt, ibmax - ib, true);
          float mx0 = masked_max(s2, nt, ibmax - ib, false);
          float bm = mx1 - mx0;
          if (i32 + ib > 174) continue;
          if (nsym == 1) {
            bmeta[i32 + ib - 1] = bm;
            float den = fmaxf(mx1, mx0);
            float cm = den > 0.0f ? bm / den : 0.0f;  /* erase when den == 0 */
            bmetd[i32 + ib - 1] = cm;
          } else if (nsym == 2) {
            bmetb[i32 + ib - 1] = bm;
          } else {
            bmetc[i32 + ib - 1] = bm;
          }
        }
      }
    }
  }
}

void ftx_ft8_normalizebmet(float *bmet, int n)
{
  normalizebmet(bmet, n);
}

void ftx_ft8b(ftx_ft8_ctx *ctx, float *dd0, bool *newdat, const ftx_ft8_params *p, bool lsubtract,
              int imetric, float *f1p, float *xdtp, float xbase, const int apsym[58], const int aph10[10],
              int *nharderrors, float *dmin, int *nbadcrc, int *iaptype, int *ipassp, char msg37[38],
              float *xsnr, int itone[79])
{
  float complex cs[FT8_NN][8];
  float s8[FT8_NN][8];
  float bmeta[174], bmetb[174], bmetc[174], bmetd[174], bmete[174];
  float llra[174], llrb[174], llrc[174], llrd[174], llre[174], llrz[174];
  uint8_t message77[77], message91[91], apmask[174], cw[174];
  float f1 = *f1p, xdt = *xdtp;
  char c77[78];
  int ibest = 0, nsync = 0;

  msg37[0] = '\0';
  *iaptype = 0;
  *ipassp = 0;

  ftx_pack77_set_dxcall(&ctx->pack77, p->hiscall);     /* dxcall13=hiscall12; mycall13=mycall12 */
  ftx_pack77_set_mycall(&ctx->pack77, p->mycall);

  *nharderrors = -1;
  float fs2 = 12000.0f / FT8_NDOWN;
  float dt2 = 1.0f / fs2;

  ftx_ft8_refine(ctx, dd0, newdat, &f1, xdt, &ibest, cs, s8, &nsync);
  xdt = (ibest - 1) * dt2;

  int syncmin = 6;
  if (imetric == 2) syncmin = 7;
  if (p->ndepth <= 2) syncmin = 8;
  if (nsync <= syncmin) {                             /* bail out */
    *nbadcrc = 1;
    *f1p = f1;
    *xdtp = xdt;
    return;
  }

  ftx_ft8_bitmetrics(cs, imetric, bmeta, bmetb, bmetc, bmetd);
  for (int i = 0; i < 174; i++) {                     /* bmete: the largest magnitude of a, b, c */
    float temp[3] = { bmeta[i], bmetb[i], bmetc[i] };
    int ip = 0;
    for (int t = 1; t < 3; t++)
      if (fabsf(temp[t]) > fabsf(temp[ip])) ip = t;
    bmete[i] = temp[ip];
  }
  normalizebmet(bmeta, 174);
  normalizebmet(bmetb, 174);
  normalizebmet(bmetc, 174);
  normalizebmet(bmetd, 174);
  normalizebmet(bmete, 174);
  float scalefac = 2.83f;
  for (int i = 0; i < 174; i++) {
    llra[i] = scalefac * bmeta[i];
    llrb[i] = scalefac * bmetb[i];
    llrc[i] = scalefac * bmetc[i];
    llrd[i] = scalefac * bmetd[i];
    llre[i] = scalefac * bmete[i];
  }

  int npasses;
  if (p->lapon || p->ncontest == 7) {                 /* Hounds always use AP */
    if (!p->lapcqonly) npasses = 5 + 2 * nappasses[p->nQSOProgress];
    else npasses = 7;
  } else {
    npasses = 5;
  }
  if (p->nzhsym < 50) npasses = 5;

  int ipass;
  for (ipass = 1; ipass <= npasses; ipass++) {
    memcpy(llrz, llra, sizeof llrz);
    if (ipass == 2) memcpy(llrz, llrb, sizeof llrz);
    if (ipass == 3) memcpy(llrz, llrc, sizeof llrz);
    if (ipass == 4) memcpy(llrz, llrd, sizeof llrz);
    if (ipass == 5) memcpy(llrz, llre, sizeof llrz);
    if (ipass <= 5) {
      memset(apmask, 0, sizeof apmask);
      *iaptype = 0;
    }
    if (ipass > 5) {
      memcpy(llrz, llra, sizeof llrz);
      if ((ipass - 5) % 2 == 1) memcpy(llrz, llra, sizeof llrz);
      if ((ipass - 5) % 2 == 0) memcpy(llrz, llrc, sizeof llrz);
      float apmag = 0.0f;
      for (int i = 0; i < 174; i++) if (fabsf(llrz[i]) > apmag) apmag = fabsf(llrz[i]);
      apmag = apmag * 1.1f;
      if (!p->lapcqonly) *iaptype = naptypes[p->nQSOProgress][(ipass - 4) / 2 - 1];
      else *iaptype = 1;
      int nc = p->ncontest;
      /* conditions that cause us to bail out of AP decoding */
      if (nc <= 5 && *iaptype >= 3 && (fabsf(f1 - p->nfqso) > p->napwid && fabsf(f1 - p->nftx) > p->napwid)) continue;
      if (nc == 6) continue;                          /* no AP for Foxes */
      if (nc == 7 && f1 > 950.0f) continue;           /* Hounds use AP only below 950 Hz */
      if (*iaptype >= 2 && apsym[0] > 1) continue;    /* no, or nonstandard, mycall */
      if (nc == 7 && *iaptype >= 2 && aph10[0] > 1) continue;
      if (*iaptype >= 3 && apsym[29] > 1) continue;   /* no, or nonstandard, dxcall */

      if (*iaptype == 1) {                            /* CQ or CQ RU or CQ TEST or CQ FD */
        memset(apmask, 0, sizeof apmask);
        for (int i = 0; i < 29; i++) apmask[i] = 1;
        const int *m = NULL;
        if (nc == 0) m = mcq;
        if (nc == 1) m = mcqtest;
        if (nc == 2) m = mcqtest;
        if (nc == 3) m = mcqfd;
        if (nc == 4) m = mcqru;
        if (nc == 5) m = mcqww;
        if (nc == 7) m = mcq;
        if (nc == 8) m = mcqtest;
        if (m != NULL)
          for (int i = 0; i < 29; i++) llrz[i] = apmag * m[i];
        apmask[74] = apmask[75] = apmask[76] = 1;
        llrz[74] = apmag * (-1);
        llrz[75] = apmag * (-1);
        llrz[76] = apmag * (+1);
      }
      if (*iaptype == 2) {                            /* MyCall ??? ??? */
        memset(apmask, 0, sizeof apmask);
        if (nc == 0 || nc == 1 || nc == 5 || nc == 8) {
          for (int i = 0; i < 29; i++) { apmask[i] = 1; llrz[i] = apmag * apsym[i]; }
          apmask[74] = apmask[75] = apmask[76] = 1;
          llrz[74] = apmag * (-1);
          llrz[75] = apmag * (-1);
          llrz[76] = apmag * (+1);
        } else if (nc == 2) {
          for (int i = 0; i < 28; i++) { apmask[i] = 1; llrz[i] = apmag * apsym[i]; }
          apmask[71] = apmask[72] = apmask[73] = 1;
          llrz[71] = apmag * (-1);
          llrz[72] = apmag * (+1);
          llrz[73] = apmag * (-1);
          apmask[74] = apmask[75] = apmask[76] = 1;
          llrz[74] = llrz[75] = llrz[76] = apmag * (-1);
        } else if (nc == 3) {
          for (int i = 0; i < 28; i++) { apmask[i] = 1; llrz[i] = apmag * apsym[i]; }
          apmask[74] = apmask[75] = apmask[76] = 1;
          llrz[74] = llrz[75] = llrz[76] = apmag * (-1);
        } else if (nc == 4) {
          for (int i = 1; i < 29; i++) { apmask[i] = 1; llrz[i] = apmag * apsym[i - 1]; }
          apmask[74] = apmask[75] = apmask[76] = 1;
          llrz[74] = apmag * (-1);
          llrz[75] = llrz[76] = apmag * (+1);
        } else if (nc == 7) {                         /* ??? RR73; MyCall <Fox hash10> ??? */
          for (int i = 28; i < 56; i++) { apmask[i] = 1; llrz[i] = apmag * apsym[i - 28]; }
          for (int i = 56; i < 66; i++) { apmask[i] = 1; llrz[i] = apmag * aph10[i - 56]; }
          for (int i = 71; i < 77; i++) apmask[i] = 1;
          llrz[71] = llrz[72] = apmag * (-1);
          llrz[73] = apmag * (+1);
          llrz[74] = llrz[75] = llrz[76] = apmag * (-1);
        }
      }
      if (*iaptype == 3) {                            /* MyCall DxCall ??? */
        memset(apmask, 0, sizeof apmask);
        if (nc == 0 || nc == 1 || nc == 2 || nc == 5 || nc == 7 || nc == 8) {
          for (int i = 0; i < 58; i++) { apmask[i] = 1; llrz[i] = apmag * apsym[i]; }
          apmask[74] = apmask[75] = apmask[76] = 1;
          llrz[74] = apmag * (-1);
          llrz[75] = apmag * (-1);
          llrz[76] = apmag * (+1);
        } else if (nc == 3) {                         /* Field Day */
          for (int i = 0; i < 56; i++) apmask[i] = 1;
          for (int i = 0; i < 28; i++) llrz[i] = apmag * apsym[i];
          for (int i = 28; i < 56; i++) llrz[i] = apmag * apsym[i + 1];
          apmask[71] = apmask[72] = apmask[73] = 1;
          apmask[74] = apmask[75] = apmask[76] = 1;
          llrz[74] = llrz[75] = llrz[76] = apmag * (-1);
        } else if (nc == 4) {
          for (int i = 1; i < 57; i++) apmask[i] = 1;
          for (int i = 1; i < 29; i++) llrz[i] = apmag * apsym[i - 1];
          for (int i = 29; i < 57; i++) llrz[i] = apmag * apsym[i];
          apmask[74] = apmask[75] = apmask[76] = 1;
          llrz[74] = apmag * (-1);
          llrz[75] = llrz[76] = apmag * (+1);
        }
      }
      if (*iaptype == 5 && nc == 7) continue;         /* Hound */
      if (*iaptype == 4 || *iaptype == 5 || *iaptype == 6) {
        memset(apmask, 0, sizeof apmask);
        if (nc <= 5 || (nc == 7 && *iaptype == 6) || nc == 8) {
          for (int i = 0; i < 77; i++) apmask[i] = 1;   /* mycall, hiscall, RRR|73|RR73 */
          for (int i = 0; i < 58; i++) llrz[i] = apmag * apsym[i];
          const int *m = *iaptype == 4 ? mrrr : (*iaptype == 5 ? m73 : mrr73);
          for (int i = 0; i < 19; i++) llrz[58 + i] = apmag * m[i];
        } else if (nc == 7 && *iaptype == 4) {        /* Hound listens for MyCall RR73;... */
          for (int i = 0; i < 28; i++) { apmask[i] = 1; llrz[i] = apmag * apsym[i]; }
          for (int i = 56; i < 66; i++) { apmask[i] = 1; llrz[i] = apmag * aph10[i - 56]; }
          for (int i = 71; i < 77; i++) apmask[i] = 1;
          llrz[71] = llrz[72] = apmag * (-1);
          llrz[73] = apmag * (1);
          llrz[74] = llrz[75] = llrz[76] = apmag * (-1);
        }
      }
    }

    memset(cw, 0, sizeof cw);
    *dmin = 0.0f;
    int norder = 2;
    int maxosd = 2;
    if (p->ndepth == 1) maxosd = -1;                  /* BP only */
    if (p->ndepth == 3 && (fabsf(p->nfqso - f1) <= p->napwid || fabsf(p->nftx - f1) <= p->napwid || p->ncontest == 7))
      maxosd = 2;
    int keff = 91, ntype = 0;
    ftx_ldpc_decode174_91(&ctx->ldpc, llrz, keff, maxosd, norder, apmask, message91, cw, &ntype, nharderrors, dmin);
    if (*nharderrors >= 0) memcpy(message77, message91, 77);

    msg37[0] = '\0';
    *nbadcrc = 1;
    if (*nharderrors < 0 || *nharderrors > 36) continue;
    {
      int nz = 0;
      for (int i = 0; i < 174; i++) if (cw[i] == 0) nz++;
      if (nz == 174) continue;                        /* reject the all-zero codeword */
    }
    ftx_bits_to_c77(message77, c77);
    int n3 = (c77[71] - '0') * 4 + (c77[72] - '0') * 2 + (c77[73] - '0');
    int i3 = (c77[74] - '0') * 4 + (c77[75] - '0') * 2 + (c77[76] - '0');
    if (i3 > 5 || (i3 == 0 && n3 > 6)) continue;
    if (i3 == 0 && n3 == 2) continue;
    bool unpk77_success = ftx_unpack77(&ctx->pack77, c77, 1, msg37);
    if (!unpk77_success || strstr(msg37, "/R") != NULL || strncmp(msg37, "TU; ", 4) == 0) {
      if (i3 >= 1 && i3 <= 3 && p->ncontest == 0) continue;
    }
    if (!unpk77_success) continue;
    /* valid codeword, valid (i3,n3), nonquirky message */
    *nbadcrc = 0;
    ftx_ft8_tones_from_77bits(message77, itone);
    if (lsubtract) ftx_ft8_subtract(ctx, dd0, itone, f1, xdt, false);
    float xsig = 0.0f, xnoi = 0.0f;
    for (int i = 0; i < 79; i++) {
      xsig = xsig + s8[i][itone[i]] * s8[i][itone[i]];
      int ios = (itone[i] + 4) % 7;
      xnoi = xnoi + s8[i][ios] * s8[i][ios];
    }
    float xs = 0.001f, xs2 = 0.001f;
    float arg = xsig / xnoi - 1.0f;
    if (arg > 0.1f) xs = arg;
    arg = xsig / xbase / 3.0e6f - 1.0f;
    if (arg > 0.1f) xs2 = arg;
    xs = 10.0f * log10f(xs) - 27.0f;
    xs2 = 10.0f * log10f(xs2) - 27.0f;
    if (!p->nagain) xs = xs2;
    if (nsync <= 10 && xs < -25.0f) {                 /* likely false decode */
      *nbadcrc = 1;
      *xsnr = xs;
      *ipassp = ipass;
      *f1p = f1;
      *xdtp = xdt;
      return;
    }
    if (xs < -25.0f) xs = -25.0f;
    *xsnr = xs;
    *ipassp = ipass;
    *f1p = f1;
    *xdtp = xdt;
    return;
  }
  *ipassp = ipass;
  *f1p = f1;
  *xdtp = xdt;
}
