/* ftx_ft8_a7a8.c — the FT8 "a7" and "a8" decoders: decoding against the
 * messages a known station is expected to send.
 *
 * Ported by hand to C from WSJT-X v3.0.2 (git ccdfaf3), lib/ft8/ft8_a7.f90
 * (ft8_a7d, getmsg), lib/ft8/ft8_a8d.f90, lib/ft8/genft8.f90, lib/smo121.f90,
 * lib/db.f90.  See ftx_ft8.h for the WSJT-X copyright and the
 * GPL-3.0-or-later notice.  This port: Copyright (C) 2026 Richard Fakenberg, OK1BR.
 */
#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ftx_ft8_internal.h"

static float db(float x)
{
  float d = -99.0f;
  if (x > 1.259e-10f) d = 10.0f * log10f(x);
  return d;
}

static void smo121(float *x, int nz)
{
  float x0 = x[0];
  for (int i = 2; i <= nz - 1; i++) {
    float x1 = x[i - 1];
    x[i - 1] = 0.5f * x[i - 1] + 0.25f * (x0 + x[i]);
    x0 = x1;
  }
}

/* Fortran Iw.m of v into a small buffer (w <= 8) */
static void fmt_i(char *dst, int w, int m, int v)
{
  char digits[16];
  int a = v < 0 ? -v : v;
  int n = snprintf(digits, sizeof digits, "%0*d", m, a);
  int total = n + (v < 0 ? 1 : 0);
  if (total > w) { memset(dst, '*', (size_t)w); dst[w] = '\0'; return; }
  memset(dst, ' ', (size_t)(w - total));
  int p = w - total;
  if (v < 0) dst[p++] = '-';
  memcpy(dst + p, digits, (size_t)n);
  dst[w] = '\0';
}

/* The 206 candidate messages of the a7/a8 decoders for one station pair.
 * call_1 may be "CQ" (a7 after a CQ) and grid4 may be "RR73"/blank; the
 * a7 flavour (cq_quirk) inserts the QU1RK placeholder and a8 (getmsg)
 * blanks reports beyond +/-30 dB. */
static void build_msg(int i, const char *call_1, const char *call_2, const char *grid4, bool std_1, bool std_2,
                      bool a7, char msg[38])
{
  char b[96];
  bool cq = a7 && strncmp(call_1, "CQ", 2) == 0 && (call_1[2] == '\0' || call_1[2] == ' ');
  snprintf(b, sizeof b, "%s %s", call_1, call_2);
  if (cq && i != 5) snprintf(b, sizeof b, "QU1RK %s", call_2);
  if (!std_1) {
    if (i == 1 || i >= 6) snprintf(b, sizeof b, "<%s> %s", call_1, call_2);
    if (i >= 2 && i <= 4) snprintf(b, sizeof b, "%s <%s>", call_1, call_2);
  } else if (!std_2) {
    if (i <= 4 || i == 6) snprintf(b, sizeof b, "<%s> %s", call_1, call_2);
    if (i >= 7) snprintf(b, sizeof b, "%s <%s>", call_1, call_2);
  }
  size_t j0 = strlen(b);                              /* Fortran j0 = len_trim+2 → one blank then text */
  int isnr = 0;
  if (i == 2) snprintf(b + j0, sizeof b - j0, " RRR");
  if (i == 3) snprintf(b + j0, sizeof b - j0, " RR73");
  if (i == 4) snprintf(b + j0, sizeof b - j0, " 73");
  if (i == 5) {
    if (std_2) {
      snprintf(b, sizeof b, "CQ %s", call_2);
      if (a7) {
        if (strlen(call_1) >= 3 && call_1[2] == '_') snprintf(b, sizeof b, "%s %s", call_1, call_2);
        if (strncmp(grid4, "RR73", 4) != 0) {
          size_t l = strlen(b);
          snprintf(b + l, sizeof b - l, " %.4s", grid4);
        }
      } else {
        size_t l = strlen(b);
        snprintf(b + l, sizeof b - l, " %.4s", grid4);
      }
    }
    if (!std_2) snprintf(b, sizeof b, "CQ %s", call_2);
  }
  if (i == 6 && std_2) snprintf(b + j0, sizeof b - j0, " %.4s", grid4);
  if (i >= 7 && i <= 206) {
    isnr = -50 + (i - 7) / 2;
    char r[8];
    fmt_i(r, 3, 2, isnr);
    if (i % 2 == 1) {
      if (r[0] == ' ') r[0] = '+';
      snprintf(b + j0, sizeof b - j0, " %s", r);
    } else {
      if (r[0] == ' ') r[0] = '+';
      snprintf(b + j0, sizeof b - j0, " R%s", r);
    }
  }
  if (!a7 && (isnr > 30 || isnr < -30)) b[0] = '\0';   /* getmsg: msg='' */
  snprintf(msg, 38, "%-37.37s", b);
}

/* genft8: pack a message, get its tones; msgsent is the unpacked text */
void ftx_ft8_genft8(ftx_ft8_ctx *ctx, const char *msg, int *i3, int *n3, char msgsent[38],
                    uint8_t msgbits[77], int itone[79])
{
  char c77[78], m37[38];
  snprintf(m37, sizeof m37, "%-37.37s", msg);
  *i3 = -1;
  *n3 = -1;
  memset(c77, '0', 77);
  c77[77] = '\0';
  ftx_pack77(&ctx->pack77, m37, i3, n3, c77);
  bool ok = ftx_unpack77(&ctx->pack77, c77, 0, msgsent);
  if (!ftx_c77_to_bits(c77, msgbits) || !ok) {
    memset(msgbits, 0, 77);
    memset(itone, 0, 79 * sizeof(int));
    snprintf(msgsent, 38, "%s", "*** bad message ***");
    return;
  }
  ftx_ft8_tones_from_77bits(msgbits, itone);
}

/* ------------------------------------------------------------------------
 * ft8_a7d: try the 206 messages a station is expected to send next
 * ---------------------------------------------------------------------- */
void ftx_ft8_a7d(ftx_ft8_ctx *ctx, const float *dd0, bool *newdat, const char *call_1, const char *call_2,
                 const char *grid4, float *xdtp, float *f1p, float xbase, int *nharderrors, float *dmin,
                 char msg37[38], float *xsnr)
{
  const int NN = FT8_NN, MAXMSG = 206;
  float complex cs[FT8_NN][8];
  float s8[FT8_NN][8];
  float bmeta[174], bmetb[174], bmetc[174], bmetd[174];
  float llra[174], llrb[174], llrc[174], llrd[174];
  float dmm[206];
  uint8_t cw[174], msgbits[77];
  int itone[79], ibest = 0, nsync = 0;
  char msg[38], msgsent[38], msgbest[38] = "";
  float f1 = *f1p, xdt = *xdtp;
  float fs2 = 12000.0f / FT8_NDOWN, dt2 = 1.0f / fs2;

  bool std_1 = ftx_ft8_stdcall(call_1);
  if (strcmp(call_1, "CQ") == 0) std_1 = true;
  bool std_2 = ftx_ft8_stdcall(call_2);

  ftx_ft8_refine(ctx, dd0, newdat, &f1, xdt, &ibest, cs, s8, &nsync);
  xdt = (ibest - 1) * dt2 - 0.5f;
  (void)nsync;                                        /* the bail-out on nsync is commented out in WSJT-X */

  ftx_ft8_bitmetrics(cs, 1, bmeta, bmetb, bmetc, bmetd);
  ftx_ft8_normalizebmet(bmeta, 174);
  ftx_ft8_normalizebmet(bmetb, 174);
  ftx_ft8_normalizebmet(bmetc, 174);
  ftx_ft8_normalizebmet(bmetd, 174);
  float scalefac = 2.83f;
  for (int i = 0; i < 174; i++) {
    llra[i] = scalefac * bmeta[i];
    llrb[i] = scalefac * bmetb[i];
    llrc[i] = scalefac * bmetc[i];
    llrd[i] = scalefac * bmetd[i];
  }

  float pbest = 0.0f;
  *dmin = 1.e30f;
  *nharderrors = -1;
  for (int imsg = 1; imsg <= MAXMSG; imsg++) {
    build_msg(imsg, call_1, call_2, grid4, std_1, std_2, true, msg);
    int i3, n3;
    ftx_ft8_genft8(ctx, msg, &i3, &n3, msgsent, msgbits, itone);   /* source-encode this message */
    ftx_ldpc_encode174_91(msgbits, cw);                            /* codeword for this message */
    float pow = 0.0f;
    for (int i = 0; i < NN; i++) pow = pow + s8[i][itone[i]] * s8[i][itone[i]];
    float d[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const float *llr[4] = { llra, llrb, llrc, llrd };
    for (int m = 0; m < 4; m++)
      for (int i = 0; i < 174; i++) {
        uint8_t hdec = llr[m][i] >= 0.0f ? 1 : 0;
        d[m] = d[m] + (float)(hdec ^ cw[i]) * fabsf(llr[m][i]);
      }
    float dm = fminf(fminf(fminf(d[0], d[1]), d[2]), d[3]);
    dmm[imsg - 1] = dm;
    if (dm < *dmin) {
      *dmin = dm;
      memcpy(msgbest, msgsent, 38);
      pbest = pow;
      for (int m = 0; m < 4; m++) {
        if (dm == d[m]) {
          int nh = 0;
          for (int i = 0; i < 174; i++)
            if (cw[i] ? llr[m][i] < 0.0f : llr[m][i] > 0.0f) nh++;
          *nharderrors = nh;
          break;
        }
      }
    }
  }

  int iloc = 0;
  for (int i = 1; i < MAXMSG; i++) if (dmm[i] < dmm[iloc]) iloc = i;
  dmm[iloc] = 1.e30f;
  iloc = 0;
  for (int i = 1; i < MAXMSG; i++) if (dmm[i] < dmm[iloc]) iloc = i;
  float dmin2 = dmm[iloc];
  *xsnr = -25.0f;
  float arg = pbest / xbase / 3.0e6f - 1.0f;
  if (arg > 0.0f) *xsnr = fmaxf(-25.0f, db(arg) - 27.0f);
  if (*dmin > 100.0f || dmin2 / *dmin < 1.3f) *nharderrors = -1;
  memcpy(msg37, msgbest, 38);
  if (strncmp(msg37, "CQ ", 3) == 0 && std_2 && strncmp(grid4, "    ", 4) == 0) *nharderrors = -1;
  if (strncmp(msg37, "QU1RK ", 6) == 0) *nharderrors = -1;
  {
    int n = (int)strlen(msg37);
    while (n > 0 && msg37[n - 1] == ' ') msg37[--n] = '\0';
  }
  *f1p = f1;
  *xdtp = xdt;
}

/* ------------------------------------------------------------------------
 * ft8_a8d: list decoding at the Rx frequency for a known DX call and grid
 * ---------------------------------------------------------------------- */
void ftx_ft8_a8d(ftx_ft8_ctx *ctx, const float *dd, const char *mycall, const char *dxcall,
                 const char *dxgrid, float f1a, float *xdt, float *fbest, float *xsnr, float *plog,
                 char msgbest[38])
{
  const int NN = FT8_NN, NWAVE = FT8_NN * FT8_NSPS / FT8_NDOWN, NZZ = FT8_NDS, NFFT = FT8_NDS, NH = NFFT / 2;
  const int NMSGS = 206;
  float complex cd[FT8_NDS], cwave[FT8_NDS];
  float complex *cd0 = (float complex *)ctx->fft_a8.c;
  static float s_init;                                /* (only to keep the arrays off a huge stack frame) */
  (void)s_init;
  float s[2 * 1600 + 1], s0[2 * 1600 + 1], s1[2 * 1600 + 1], s8[8];
  float complex csymb[32];
  uint8_t msgbits[77];
  int itone[NN], itone_best[NN];
  char msg[38], msgsent[38], c77[78], grid4[5];
  float f1 = f1a;
  bool newdat = true;
  bool my_std = ftx_ft8_stdcall(mycall), his_std = ftx_ft8_stdcall(dxcall);
  snprintf(grid4, sizeof grid4, "%-4.4s", dxgrid);

  ftx_ft8_downsample(ctx, dd, &newdat, f1, (fftwf_complex *)cd);   /* mix from f1 to baseband */
  float fac = 1.e-6f;
  float fsd = 200.0f;                                 /* downsampled rate */
  float bt = 2.0f;
  float dt = 1.0f / fsd;
  float df = 200.0f / NFFT;
  for (int i = 0; i <= 2 * NH; i++) { s[i] = 0.0f; s0[i] = 0.0f; s1[i] = 0.0f; }
#define S(j) s[(j) + NH]
#define S0(j) s0[(j) + NH]
#define S1(j) s1[(j) + NH]

  float sbest = 0.0f, tbest = 0.0f;
  *fbest = 0.0f;
  snprintf(msgbest, 38, "%s", "");
  memset(itone_best, 0, sizeof itone_best);
  for (int imsg = 1; imsg <= NMSGS; imsg++) {         /* find the message that fits best */
    build_msg(imsg, mycall, dxcall, grid4, my_std, his_std, false, msg);
    int i3 = -1, n3 = -1;
    memset(c77, '0', 77);
    c77[77] = '\0';
    ftx_pack77(&ctx->pack77, msg, &i3, &n3, c77);
    ftx_ft8_genft8(ctx, msg, &i3, &n3, msgsent, msgbits, itone);
    ftx_gen_ft8wave(ctx, itone, NN, 32, bt, fsd, 0.0f, (fftwf_complex *)cwave, NWAVE);
    for (int i = NWAVE; i < NZZ; i++) cwave[i] = 0.0f;

    float spk = 0.0f, fpk = 0.0f, tpk = 0.0f;         /* best lag for this message */
    int lagpk = 0, lag1 = -200, lag2 = 200, lagstep = 4;
    for (int iter = 1; iter <= 2; iter++) {
      if (iter == 2) {
        lag1 = lagpk - 8;
        lag2 = lagpk + 8;
        lagstep = 1;
      }
      for (int lag = lag1; lag <= lag2; lag += lagstep) {
        for (int i = 0; i < NWAVE; i++) {
          int j = i + lag + 100;
          cd0[i] = 0.0f;
          if (j >= 0 && j <= NWAVE - 1) cd0[i] = cd[j] * conjf(cwave[i]);
        }
        for (int i = NWAVE; i < NFFT; i++) cd0[i] = 0.0f;
        ftx_fft_exec(&ctx->fft_a8);                   /* forward c2c FFT */
        for (int i = 0; i < NFFT; i++) {
          int j = i > NH ? i - NFFT : i;
          S(j) = fac * (crealf(cd0[i]) * crealf(cd0[i]) + cimagf(cd0[i]) * cimagf(cd0[i]));
        }
        smo121(s, NFFT + 1);
        for (int j = -NH; j <= NH; j++) {
          if (S(j) > spk) {
            spk = S(j);
            fpk = j * df + f1;
            lagpk = lag;
            tpk = lag * dt;
            memcpy(s0, s, sizeof s0);                 /* best-fit spectrum for this message */
          }
        }
      }
    }
    if (spk > sbest) {
      sbest = spk;
      *fbest = fpk;
      tbest = tpk;
      memcpy(msgbest, msg, 38);
      memcpy(itone_best, itone, sizeof itone_best);
      memcpy(s1, s0, sizeof s1);                      /* overall best-fit spectrum */
    }
  }

  float a[5] = { f1 - *fbest, 0.0f, 0.0f, 0.0f, 0.0f };
  if (fabsf(a[0]) > 5.0f) {                           /* effective Ftol = 5 Hz */
    msgbest[0] = '\0';
    return;
  }
  ftx_ft8_twkfreq1((fftwf_complex *)cd, NZZ, 200.0f, a, (fftwf_complex *)cd);   /* re-centre the spectrum */

  *xdt = tbest;
  float ave = 0.0f;
  {
    float sa = 0.0f, sb = 0.0f;
    for (int j = -200; j <= -100; j++) sa += S1(j);
    for (int j = 100; j <= 200; j++) sb += S1(j);
    ave = (sa + sb) / 202.0f;
  }
  for (int j = -NH; j <= NH; j++) S1(j) = S1(j) / ave - 1.0f;
  float s1pk = S1(-32);
  for (int j = -31; j <= 32; j++) if (S1(j) > s1pk) s1pk = S1(j);
  float sig = 0.0f;
  int nsig = 0;
  for (int i = -32; i <= 32; i++) {
    if (S1(i) < 0.5f * s1pk) continue;
    sig = sig + S1(i);
    nsig++;
  }
  sig = sig / (float)nsig;
  *xsnr = db(sig) - 35.0f;
  if (*xsnr < -30) *xsnr = -30;                        /* min SNR for 'a8' decodes */

  {
    int n = 37;
    while (n > 0 && msgbest[n - 1] == ' ') n--;
    msgbest[n] = '\0';
  }
  if (msgbest[0] != '\0') {                           /* probability of a successful decode */
    *plog = 0.0f;
    int nhard = 0, nsum = 0;
    float sum_sync = 0.0f, sum_sig = 0.0f, sum_big = 0.0f;
    for (int k = 1; k <= NN; k++) {
      int i0 = 32 * (k - 1) + (int)lroundf((tbest + 0.5f) / 0.005f);
      for (int i = 0; i < 32; i++) csymb[i] = 0.0f;
      for (int i = 0; i <= 31; i++)
        if (i0 + i >= 0 && i0 + i <= NZZ - 1) csymb[i] = cd[i0 + i];
      memcpy(ctx->fft_sym32.c, csymb, sizeof csymb);
      ftx_fft_exec(&ctx->fft_sym32);
      float complex *out = (float complex *)ctx->fft_sym32.c;
      float s8sum = 0.0f;
      for (int i = 0; i < 8; i++) s8[i] = crealf(out[i]) * crealf(out[i]) + cimagf(out[i]) * cimagf(out[i]);
      for (int i = 0; i < 8; i++) s8sum += s8[i];
      if (s8sum > 0.0f) {
        float pr = s8[itone_best[k - 1]] / s8sum;
        *plog = *plog + logf(pr);
        nsum++;
      }
      int ipk = 0;
      for (int i = 1; i < 8; i++) if (s8[i] > s8[ipk]) ipk = i;
      if (ipk != itone_best[k - 1]) nhard++;
      if (k <= 7 || (k >= 37 && k <= 43) || k >= 73) sum_sync = sum_sync + s8[itone_best[k - 1]];
      else sum_sig = sum_sig + s8[itone_best[k - 1]];
      sum_big = sum_big + s8[ipk];
    }
    if (nsum < NN) *plog = *plog + (NN - nsum) * logf(0.125f);
    float sigobig = (sum_sync + sum_sig) / sum_big;
    if (nhard > 54 || *plog < -159.0f || sigobig < 0.71f) msgbest[0] = '\0';
  }
#undef S
#undef S0
#undef S1
}
