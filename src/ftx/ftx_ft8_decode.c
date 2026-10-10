/* ftx_ft8_decode.c — the FT8 decoder driver: passes, early decodes,
 * subtraction, duplicate handling and the a7 table bookkeeping.
 *
 * Ported by hand to C from WSJT-X v3.0.2 (git ccdfaf3), lib/ft8_decode.f90
 * (ft8_decoder%decode) and lib/ft8/ft8_a7.f90 (ft8_a7_save).  See ftx_ft8.h
 * for the WSJT-X copyright and the GPL-3.0-or-later notice.
 * This port: Copyright (C) 2026 Richard Fakenberg, OK1BR.
 *
 * Not ported (yet): the real-time bail-outs on the wall clock (only active
 * when ldiskdat is false in WSJT-X), and the a7/a8 decoders that run after
 * the main passes (ft8_a7d, ft8_a8d).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ftx_ft8_internal.h"

static bool isgrid4(const char *w)                    /* w: 13-char word */
{
  int n = 13;
  while (n > 0 && w[n - 1] == ' ') n--;
  return n == 4 && w[0] >= 'A' && w[0] <= 'R' && w[1] >= 'A' && w[1] <= 'R' &&
         w[2] >= '0' && w[2] <= '9' && w[3] >= '0' && w[3] <= '9';
}

static void trimw(char *dst, const char *w)           /* dst = trim(w), w 13 chars */
{
  int n = 13;
  while (n > 0 && w[n - 1] == ' ') n--;
  memcpy(dst, w, (size_t)n);
  dst[n] = '\0';
}

static void set37(char *dst, const char *src)         /* dst = src as a 37-char field + NUL */
{
  snprintf(dst, 38, "%-37.37s", src);
}

static void trim37(char *s)                           /* strip trailing blanks of a 37-char field */
{
  int n = (int)strlen(s);
  while (n > 0 && s[n - 1] == ' ') s[--n] = '\0';
}

/* ft8_a7_save: enter a decode into the a7 table of this sequence */
static void ft8_a7_save(ftx_ft8_ctx *ctx, int jseq, float dt, float f, const char *msg)
{
  char m[38], w[19][14], t1[14], t2[14], t3[14], b[80];
  int nwords, nw[19];

  if (strchr(msg, '/') != NULL || strchr(msg, '<') != NULL) return;
  set37(m, msg);
  ftx_pack77_split77(m, &nwords, nw, w);
  if (nwords < 1) return;
  if (strncmp(w[0], "CQ_", 3) == 0) return;
  int j = jseq;

  ctx->ndec[j][1]++;                                  /* number of decodes in this sequence */
  int i = ctx->ndec[j][1];
  if (i > FT8_A7_MAXDEC) return;

  ctx->dt0[i - 1][j][1] = dt;
  ctx->f0[i - 1][j][1] = f;
  trimw(t1, w[0]);
  trimw(t2, w[1]);
  trimw(t3, w[2]);
  snprintf(b, sizeof b, "%s %s", t1, t2);             /* "call_1 call_2" */
  if (strncmp(w[0], "CQ ", 3) == 0 && nw[1] <= 2) snprintf(b, sizeof b, "CQ %s %s", t2, t3);  /* "CQ DX Call_2" */
  set37(ctx->msg0[i - 1][j][1], b);
  if (isgrid4(w[nwords - 1])) {                       /* include the grid */
    char g[14], cur[38];
    trimw(g, w[nwords - 1]);
    memcpy(cur, ctx->msg0[i - 1][j][1], 38);
    trim37(cur);
    snprintf(b, sizeof b, "%s %s", cur, g);
    set37(ctx->msg0[i - 1][j][1], b);
  }

  /* A transmission at this frequency with "call_1 call_2" decoded in the
   * previous sequence: flag it "do not use", its next transmission has been
   * decoded and subtracted already. */
  memcpy(m, ctx->msg0[i - 1][j][1], 38);
  ftx_pack77_split77(m, &nwords, nw, w);
  trimw(t2, w[1]);
  snprintf(b, sizeof b, " %s", t2);
  for (int k = 0; k < ctx->ndec[j][0]; k++) {
    if (ctx->f0[k][j][0] <= -98.0f) continue;
    const char *hit = strstr(ctx->msg0[k][j][0], b);
    int i2 = hit != NULL ? (int)(hit - ctx->msg0[k][j][0]) + 1 : 0;
    if (fabsf(f - ctx->f0[k][j][0]) <= 3.0f && i2 >= 3) ctx->f0[k][j][0] = -98.0f;
  }
}

bool ftx_ft8_decode_init(ftx_ft8_ctx *ctx)
{
  ctx->dd = calloc(FT8_NMAX, sizeof(float));
  ctx->dd1 = calloc(FT8_NMAX, sizeof(float));
  if (ctx->dd == NULL || ctx->dd1 == NULL) return false;
  ctx->nutc0 = -1;
  ctx->ndec_early = 0;
  for (int i = 0; i < FT8_MAX_EARLY; i++) set37(ctx->allmessages[i], "");
  memset(ctx->ndec, 0, sizeof ctx->ndec);
  ctx->jseq = 0;
  for (int i = 0; i < FT8_A7_MAXDEC; i++)
    for (int j = 0; j < 2; j++)
      for (int k = 0; k < 2; k++) set37(ctx->msg0[i][j][k], "");
  return true;
}

void ftx_ft8_decode_free(ftx_ft8_ctx *ctx)
{
  free(ctx->dd);
  free(ctx->dd1);
}

void ftx_ft8_decode(ftx_ft8_ctx *ctx, const int16_t *iwave, const ftx_ft8_params *p, int nutc,
                    bool ltry_a8, bool ldiskdat, float emedelay, ftx_ft8_decode_cb cb, void *user)
{
  const int NPTS = FT8_NMAX;
  float *dd = ctx->dd, *dd1 = ctx->dd1;
  float sbase[FT8_NH1];
  ftx_ft8_candidate *candidate = malloc(FT8_MAXCAND * sizeof *candidate);
  int apsym2[58], aph10[10];
  int allsnrs[FT8_MAX_EARLY];
  int itone[FT8_NN];
  char msg37[38];
  int ndecodes = 0, n2 = 0;
  bool la8 = true;
  int nzhsym = p->nzhsym, ndepth = p->ndepth;
  (void)ltry_a8;
  (void)ldiskdat;
  (void)la8;
  (void)n2;

  if (nzhsym == 41) ctx->jseq = (nutc / 5) % 2;
  int jseq = ctx->jseq;
  if (ctx->nutc0 == -1) {
    for (int i = 0; i < FT8_A7_MAXDEC; i++)
      for (int j = 0; j < 2; j++)
        for (int k = 0; k < 2; k++) {
          set37(ctx->msg0[i][j][k], "");
          ctx->dt0[i][j][k] = 0.0f;
          ctx->f0[i][j][k] = 0.0f;
        }
  }
  if (nzhsym == 41 || nutc != ctx->nutc0) {           /* new UTC: previous a7 data k=1 → k=0 */
    int iz = ctx->ndec[jseq][1];
    for (int i = 0; i < iz; i++) {
      ctx->dt0[i][jseq][0] = ctx->dt0[i][jseq][1];
      ctx->f0[i][jseq][0] = ctx->f0[i][jseq][1];
      memcpy(ctx->msg0[i][jseq][0], ctx->msg0[i][jseq][1], 38);
    }
    ctx->ndec[jseq][0] = iz;
    ctx->ndec[jseq][1] = 0;
    ctx->nutc0 = nutc;
    for (int i = 0; i < FT8_A7_MAXDEC; i++) {
      ctx->dt0[i][jseq][1] = 0.0f;
      ctx->f0[i][jseq][1] = 0.0f;
    }
  }

  if (ndepth == 1 && nzhsym < 50) {
    ctx->ndec_early = 0;
    free(candidate);
    return;
  }
  if (ndepth == 1 && nzhsym == 50)
    for (int i = 0; i < NPTS; i++) dd[i] = iwave[i];
  ftx_ft8apset(ctx, p->mycall, p->hiscall, p->ncontest, apsym2, aph10);

  if (nzhsym <= 47) {
    for (int i = 0; i < NPTS; i++) dd[i] = iwave[i];
    memcpy(dd1, dd, NPTS * sizeof(float));
  }
  if (nzhsym == 41) {
    ndecodes = 0;
    for (int i = 0; i < FT8_MAX_EARLY; i++) set37(ctx->allmessages[i], "");
    memset(allsnrs, 0, sizeof allsnrs);
  } else {
    ndecodes = ctx->ndec_early;
  }

  if (nzhsym == 47 && ctx->ndec_early == 0) {
    memcpy(dd1, dd, NPTS * sizeof(float));
    goto l800;
  }
  if (nzhsym == 47 && ctx->ndec_early >= 1) {
    for (int i = 0; i < FT8_MAX_EARLY; i++) ctx->lsubtracted[i] = false;
    bool lrefinedt = ndepth > 2;
    for (int i = 0; i < ctx->ndec_early; i++) {
      if (ctx->xdt_save[i] - 0.5f < 0.396f) {
        ftx_ft8_subtract(ctx, dd, ctx->itone_save[i], ctx->f1_save[i], ctx->xdt_save[i], lrefinedt);
        ctx->lsubtracted[i] = true;
      }
    }
    memcpy(dd1, dd, NPTS * sizeof(float));
    goto l900;
  }
  if (nzhsym == 50 && ctx->ndec_early >= 1 && !p->nagain) {
    int n = 47 * 3456;
    memcpy(dd, dd1, (size_t)n * sizeof(float));
    for (int i = n; i < NPTS; i++) dd[i] = iwave[i];
    for (int i = 0; i < ctx->ndec_early; i++) {
      if (ctx->lsubtracted[i]) continue;
      ftx_ft8_subtract(ctx, dd, ctx->itone_save[i], ctx->f1_save[i], ctx->xdt_save[i], true);
    }
  }

  {
    int ifa = p->nfa, ifb = p->nfb;
    if (nzhsym == 50 && p->nagain) {
      for (int i = 0; i < NPTS; i++) dd[i] = iwave[i];
      ifa = p->nfqso - 20;
      ifb = p->nfqso + 20;
    }
    /* ndepth=1: 1 pass, bp; 2: subtraction, 3 passes, bp+osd; 3: dito */
    int npass = 3, imetric = 1;
    if (ndepth == 1) npass = 2;
    for (int ipass = 1; ipass <= npass; ipass++) {
      bool newdat = true, lsubtract = true;
      float syncmin = 1.3f;
      if (ndepth <= 2) syncmin = 2.1f;
      if (ipass == 1) {
        lsubtract = true;
        imetric = 1;
      } else if (ipass == 2) {
        n2 = ndecodes;
        imetric = 2;
        lsubtract = true;
      } else if (ipass == 3) {
        imetric = 2;
        if (ndecodes == 0) continue;
        lsubtract = true;
      }
      int ncand = ftx_ft8_sync8(ctx, dd, &ifa, &ifb, syncmin, p->nfqso, FT8_MAXCAND, candidate, sbase);
      for (int icand = 0; icand < ncand; icand++) {
        float sync = candidate[icand].sync;
        float f1 = candidate[icand].freq;
        float xdt = candidate[icand].dt;
        int ib = (int)lroundf(f1 / 3.125f);
        float xbase = powf(10.0f, 0.1f * (sbase[ib - 1] - 40.0f));
        msg37[0] = '\0';
        int iaptype = 0, nharderrors = -1, nbadcrc = 1, iappass = 0;
        float dmin = 0.0f, xsnr = 0.0f;
        ftx_ft8b(ctx, dd, &newdat, p, lsubtract, imetric, &f1, &xdt, xbase, apsym2, aph10,
                 &nharderrors, &dmin, &nbadcrc, &iaptype, &iappass, msg37, &xsnr, itone);
        int nsnr = (int)lroundf(xsnr);
        xdt = xdt - 0.5f;
        if (nbadcrc == 0) {
          char m37[38];
          set37(m37, msg37);
          bool ldupe = false;
          for (int id = 0; id < ndecodes; id++)
            if (memcmp(m37, ctx->allmessages[id], 37) == 0) ldupe = true;
          if (!ldupe) {
            if (ndecodes >= FT8_MAX_EARLY) continue;
            ndecodes++;
            memcpy(ctx->allmessages[ndecodes - 1], m37, 38);
            allsnrs[ndecodes - 1] = nsnr;
            ctx->f1_save[ndecodes - 1] = f1;
            ctx->xdt_save[ndecodes - 1] = xdt + 0.5f;
            memcpy(ctx->itone_save[ndecodes - 1], itone, sizeof itone);
          }
          if (!ldupe && cb != NULL) {
            float qual = 1.0f - (nharderrors + dmin) / 60.0f;   /* scale qual to [0,1] */
            if (emedelay != 0) xdt = xdt + 2.0f;
            cb(user, sync, nsnr, xdt, f1, msg37, iaptype, qual);
            ft8_a7_save(ctx, jseq, xdt, f1, msg37);
          }
        }
      }
    }
  }

l800:
  ctx->ndec_early = 0;
  if (nzhsym < 50) ctx->ndec_early = ndecodes;

l900:
  /* ft8_a7d over the previous sequence's table and the a8 list decoder
   * belong here (WSJT-X runs them at nzhsym == 50 with AP on); not yet
   * ported — see the header. */
  free(candidate);
}

void ftx_ft8_format_line(char *buf, size_t len, int nutc, int snr, float dt, float freq,
                         const char *decoded, int nap, float qual)
{
  char d37[38], annot[3] = "  ";
  snprintf(d37, sizeof d37, "%-37.37s", decoded);
  if (nap != 0) {
    snprintf(annot, sizeof annot, "a%u", (unsigned)nap % 10u);
    if (qual < 0.17f) d37[36] = '?';
  }
  snprintf(buf, len, "%06d%4d%5.1f%5d ~  %s %s", nutc, snr, dt, (int)lroundf(freq), d37, annot);
}
