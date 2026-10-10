/* ftx_ldpc.c — the (174,91) LDPC code of FT8/FT4: encoder, BP and OSD decoders.
 *
 * Ported by hand to C from WSJT-X v3.0.2 (git ccdfaf3),
 *   lib/ft8/encode174_91.f90, encode174_91_nocrc.f90, decode174_91.f90,
 *   osd174_91.f90, lib/indexx.f90, lib/platanh.f90.
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
 * Porting notes.  All soft arithmetic is single precision in the same
 * operation order as the Fortran (sum() and product() intrinsics accumulate
 * sequentially), so decodes and distances reproduce bit for bit.  Table
 * indices are 1-based in the tables and converted here.
 */
#include "ftx_ldpc.h"
#include "ftx_crc14.h"
#include "ftx_internal.h"

#include <math.h>
#include <string.h>

#define N FTX_LDPC_N
#define K FTX_LDPC_K
#define M FTX_LDPC_M

/* ------------------------------------------------------------------------
 * Encoder
 * ---------------------------------------------------------------------- */
static int hexval(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return c - 'A' + 10;
}

/* gen(i,j) of the generator matrix, 0-based row i (0..82), column j (0..90) */
static inline int genbit(int i, int j)
{
  return (hexval(ftx_ldpc_generator_hex[i][j / 4]) >> (3 - j % 4)) & 1;
}

void ftx_ldpc_encode174_91_nocrc(const uint8_t message91[91], uint8_t codeword[174])
{
  for (int i = 0; i < M; i++) {
    int nsum = 0;
    for (int j = 0; j < K; j++) nsum += message91[j] * genbit(i, j);
    codeword[K + i] = (uint8_t)(nsum % 2);
  }
  memcpy(codeword, message91, K);
}

void ftx_ldpc_encode174_91(const uint8_t message77[77], uint8_t codeword[174])
{
  uint8_t message[K];
  memcpy(message, message77, 77);
  uint16_t ncrc14 = ftx_crc14_message77(message77);
  for (int i = 0; i < 14; i++) message[77 + i] = (uint8_t)((ncrc14 >> (13 - i)) & 1);
  ftx_ldpc_encode174_91_nocrc(message, codeword);
}

/* ------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */
static float platanh(float x)
{
  int isign = 1;
  float z = x;
  if (x < 0) {
    isign = -1;
    z = fabsf(x);
  }
  if (z <= 0.664f) return x / 0.83f;
  if (z <= 0.9217f) return (float)isign * (z - 0.4064f) / 0.322f;
  if (z <= 0.9951f) return (float)isign * (z - 0.8378f) / 0.0524f;
  if (z <= 0.9998f) return (float)isign * (z - 0.9914f) / 0.0012f;
  return (float)isign * 7.0f;
}

/* indexx (Numerical Recipes quicksort index): indx[1..n] orders arr[1..n]
 * ascending.  Arrays are 1-based here to keep the original control flow. */
void ftx_indexx(const float *arr, int n, int *indx)
{
  enum { MM = 7, NSTACK = 50 };
  int istack[NSTACK + 1];
  int i, indxt, ir, itemp, j, jstack, k, l;
  float a;

  for (j = 1; j <= n; j++) indx[j] = j;
  jstack = 0;
  l = 1;
  ir = n;
  for (;;) {
    if (ir - l < MM) {
      for (j = l + 1; j <= ir; j++) {
        indxt = indx[j];
        a = arr[indxt];
        for (i = j - 1; i >= 1; i--) {
          if (arr[indx[i]] <= a) break;
          indx[i + 1] = indx[i];
        }
        if (i < 1) i = 0;
        indx[i + 1] = indxt;
      }
      if (jstack == 0) return;
      ir = istack[jstack];
      l = istack[jstack - 1];
      jstack -= 2;
    } else {
      k = (l + ir) / 2;
      itemp = indx[k];
      indx[k] = indx[l + 1];
      indx[l + 1] = itemp;
      if (arr[indx[l + 1]] > arr[indx[ir]]) {
        itemp = indx[l + 1]; indx[l + 1] = indx[ir]; indx[ir] = itemp;
      }
      if (arr[indx[l]] > arr[indx[ir]]) {
        itemp = indx[l]; indx[l] = indx[ir]; indx[ir] = itemp;
      }
      if (arr[indx[l + 1]] > arr[indx[l]]) {
        itemp = indx[l + 1]; indx[l + 1] = indx[l]; indx[l] = itemp;
      }
      i = l + 1;
      j = ir;
      indxt = indx[l];
      a = arr[indxt];
      for (;;) {
        do i++; while (arr[indx[i]] < a);
        do j--; while (arr[indx[j]] > a);
        if (j < i) break;
        itemp = indx[i]; indx[i] = indx[j]; indx[j] = itemp;
      }
      indx[l] = indx[j];
      indx[j] = indxt;
      jstack += 2;
      if (ir - i + 1 >= j - l) {
        istack[jstack] = ir;
        istack[jstack - 1] = i;
        ir = j - 1;
      } else {
        istack[jstack] = j - 1;
        istack[jstack - 1] = l;
        l = i;
      }
    }
  }
}

/* ------------------------------------------------------------------------
 * OSD (osd174_91 and its helpers)
 * ---------------------------------------------------------------------- */
static void osd_build_gen(ftx_ldpc_ctx *ctx, int k)
{
  uint8_t (*gen)[N] = ctx->gen[k - 77];
  uint8_t message91[K], cw[N];
  for (int i = 1; i <= k; i++) {
    memset(message91, 0, K);
    message91[i - 1] = 1;
    if (i <= 77) {
      /* CRC of the unit message; the first k-77 CRC bits are left for
       * detection, the remaining ones are cascaded into the code */
      uint16_t ncrc14 = ftx_crc14_message77(message91);
      for (int b = 0; b < 14; b++) message91[77 + b] = (uint8_t)((ncrc14 >> (13 - b)) & 1);
      for (int b = 77; b < k; b++) message91[b] = 0;
    }
    ftx_ldpc_encode174_91_nocrc(message91, cw);
    memcpy(gen[i - 1], cw, N);
  }
  ctx->gen_ready[k - 77] = true;
}

/* fast encoding for low-weight test patterns: codeword = XOR of the rows of
 * the reduced generator selected by me */
static void mrbencode91(const uint8_t *me, uint8_t *codeword, const uint8_t (*genmrb)[N], int k)
{
  memset(codeword, 0, N);
  for (int i = 0; i < k; i++) {
    if (me[i] == 1)
      for (int n = 0; n < N; n++) codeword[n] ^= genmrb[i][n];
  }
}

/* next test error pattern of weight iorder; iflag goes negative after the last */
static void nextpat91(uint8_t *mi, int k, int iorder, int *iflag)
{
  uint8_t ms[K];
  int ind = -1;
  for (int i = 1; i <= k - 1; i++)
    if (mi[i - 1] == 0 && mi[i] == 1) ind = i;
  if (ind < 0) {
    *iflag = ind;
    return;
  }
  memset(ms, 0, k);
  for (int i = 1; i <= ind - 1; i++) ms[i - 1] = mi[i - 1];
  ms[ind - 1] = 1;
  ms[ind] = 0;
  if (ind + 1 < k) {
    int sum = 0;
    for (int i = 0; i < k; i++) sum += ms[i];
    int nz = iorder - sum;
    for (int i = k - nz + 1; i <= k; i++) ms[i - 1] = 1;
  }
  memcpy(mi, ms, k);
  for (int i = 1; i <= k; i++) {
    if (mi[i - 1] == 1) {
      *iflag = i;
      break;
    }
  }
}

static int pattern_index(const uint8_t *e2, int ntau)
{
  int ipat = 0;
  for (int i = 1; i <= ntau; i++)
    if (e2[i - 1] == 1) ipat += 1 << (ntau - i);
  return ipat;
}

static void boxit91(ftx_ldpc_ctx *ctx, bool *reset, const uint8_t *e2, int ntau, int npindex, int i1, int i2)
{
  if (*reset) {
    for (int i = 0; i < FTX_OSD_FPSIZE; i++) ctx->fp[i] = -1;
    for (int i = 0; i <= FTX_OSD_MAXPAIRS; i++) {
      ctx->np[i] = -1;
      ctx->indexes[i][0] = -1;
      ctx->indexes[i][1] = -1;
    }
    *reset = false;
  }
  ctx->indexes[npindex][0] = i1;
  ctx->indexes[npindex][1] = i2;
  int ipat = pattern_index(e2, ntau);
  int ip = ctx->fp[ipat];
  if (ip == -1) {
    ctx->fp[ipat] = npindex;
  } else {
    while (ctx->np[ip] != -1) ip = ctx->np[ip];
    ctx->np[ip] = npindex;
  }
}

static void fetchit91(ftx_ldpc_ctx *ctx, bool *reset, const uint8_t *e2, int ntau, int *i1, int *i2)
{
  if (*reset) {
    ctx->lastpat = -1;
    *reset = false;
  }
  int ipat = pattern_index(e2, ntau);
  int index = ctx->fp[ipat];
  if (ctx->lastpat != ipat && index > 0) {
    *i1 = ctx->indexes[index][0];
    *i2 = ctx->indexes[index][1];
    ctx->inext = ctx->np[index];
  } else if (ctx->lastpat == ipat && ctx->inext > 0) {
    *i1 = ctx->indexes[ctx->inext][0];
    *i2 = ctx->indexes[ctx->inext][1];
    ctx->inext = ctx->np[ctx->inext];
  } else {
    *i1 = -1;
    *i2 = -1;
    ctx->inext = -1;
  }
  ctx->lastpat = ipat;
}

void ftx_ldpc_osd174_91(ftx_ldpc_ctx *ctx, const float llr[174], int k, const uint8_t apmask[174],
                        int ndeep, uint8_t message91[91], uint8_t cw[174], int *nhardmin, float *dmin)
{
  uint8_t (*genmrb)[N] = ctx->genmrb;            /* scratch lives in the context, not in a static */
  uint8_t hdec[N], hdecp[N], apmaskr[N], c0[N], ce[N], cwp[N], nxor[N];
  uint8_t m0[K], me[K], mi[K], misub[K], e2sub[N], e2[N], ui[N], r2pat[N], temp[K];
  float rx[N], absrx[N], absrxp[N], arr1[N + 1];
  int indx[N + 1], indices[N];

  if (!ctx->gen_ready[k - 77]) osd_build_gen(ctx, k);
  const uint8_t (*gen)[N] = ctx->gen[k - 77];

  for (int i = 0; i < N; i++) {                  /* hard decisions, reliabilities */
    rx[i] = llr[i];
    hdec[i] = rx[i] >= 0 ? 1 : 0;
    absrx[i] = fabsf(rx[i]);
    arr1[i + 1] = absrx[i];
  }
  ftx_indexx(arr1, N, indx);

  /* columns of the generator in order of decreasing reliability */
  for (int i = 0; i < N; i++) {
    int src = indx[N - i] - 1;                   /* indx(N+1-i), 1-based i */
    for (int r = 0; r < k; r++) genmrb[r][i] = gen[r][src];
    indices[i] = indx[N - i];
  }

  /* Gaussian elimination: most reliable bits into positions 1:k */
  for (int id = 1; id <= k; id++) {
    for (int icol = id; icol <= k + 20; icol++) {
      if (genmrb[id - 1][icol - 1] == 1) {
        if (icol != id) {
          for (int r = 0; r < k; r++) {
            temp[r] = genmrb[r][id - 1];
            genmrb[r][id - 1] = genmrb[r][icol - 1];
            genmrb[r][icol - 1] = temp[r];
          }
          int itmp = indices[id - 1];
          indices[id - 1] = indices[icol - 1];
          indices[icol - 1] = itmp;
        }
        for (int ii = 1; ii <= k; ii++) {
          if (ii != id && genmrb[ii - 1][id - 1] == 1)
            for (int n = 0; n < N; n++) genmrb[ii - 1][n] ^= genmrb[id - 1][n];
        }
        break;
      }
    }
  }
  /* g2 = transpose(genmrb): g2(n,i) == genmrb[i-1][n-1]; used in place */

  for (int i = 0; i < N; i++) {                  /* permute into MRB order */
    hdecp[i] = hdec[indices[i] - 1];
    absrxp[i] = absrx[indices[i] - 1];
    apmaskr[i] = apmask[indices[i] - 1];
  }
  memcpy(m0, hdecp, k);                          /* order-0 message */

  mrbencode91(m0, c0, genmrb, k);
  int nhard = 0;
  float dm = 0.0f;
  for (int i = 0; i < N; i++) {
    nxor[i] = c0[i] ^ hdecp[i];
    nhard += nxor[i];
  }
  for (int i = 0; i < N; i++) dm += (float)nxor[i] * absrxp[i];

  memcpy(cwp, c0, N);
  int ntotal = 0, nrejected = 0, npre1 = 0, npre2 = 0, nord = 0, nt = 0, ntheta = 0, ntau = 0;
  (void)nrejected;

  if (ndeep == 0) goto done;
  if (ndeep > 6) ndeep = 6;
  switch (ndeep) {
  case 1: nord = 1; npre1 = 0; npre2 = 0; nt = 40; ntheta = 12; break;
  case 2: nord = 1; npre1 = 1; npre2 = 0; nt = 40; ntheta = 10; break;
  case 3: nord = 1; npre1 = 1; npre2 = 1; nt = 40; ntheta = 12; ntau = 14; break;
  case 4: nord = 2; npre1 = 1; npre2 = 1; nt = 40; ntheta = 12; ntau = 17; break;
  case 5: nord = 3; npre1 = 1; npre2 = 1; nt = 40; ntheta = 12; ntau = 15; break;
  default: nord = 4; npre1 = 1; npre2 = 1; nt = 95; ntheta = 12; ntau = 15; break;
  }

  for (int iorder = 1; iorder <= nord; iorder++) {
    memset(misub, 0, k - iorder);
    memset(misub + (k - iorder), 1, iorder);
    int iflag = k - iorder + 1;
    while (iflag >= 0) {
      int iend = (iorder == nord && npre1 == 0) ? iflag : 1;
      float d1 = 0.0f;
      for (int n1 = iflag; n1 >= iend; n1--) {
        memcpy(mi, misub, k);
        mi[n1 - 1] = 1;
        bool apconflict = false;
        for (int i = 0; i < k; i++)
          if ((apmaskr[i] & mi[i]) == 1) apconflict = true;
        if (apconflict) continue;
        ntotal++;
        for (int i = 0; i < k; i++) me[i] = m0[i] ^ mi[i];
        int nd1kpt;
        if (n1 == iflag) {
          mrbencode91(me, ce, genmrb, k);
          for (int i = k; i < N; i++) e2sub[i - k] = ce[i] ^ hdecp[i];
          memcpy(e2, e2sub, N - k);
          int s = 0;
          for (int i = 0; i < nt; i++) s += e2sub[i];
          nd1kpt = s + 1;
          d1 = 0.0f;
          for (int i = 0; i < k; i++) d1 += (float)(me[i] ^ hdecp[i]) * absrxp[i];
        } else {
          for (int i = k; i < N; i++) e2[i - k] = e2sub[i - k] ^ genmrb[n1 - 1][i];
          int s = 0;
          for (int i = 0; i < nt; i++) s += e2[i];
          nd1kpt = s + 2;
        }
        if (nd1kpt <= ntheta) {
          mrbencode91(me, ce, genmrb, k);
          for (int i = 0; i < N; i++) nxor[i] = ce[i] ^ hdecp[i];
          float dd;
          if (n1 == iflag) {
            float s = 0.0f;
            for (int i = k; i < N; i++) s += (float)e2sub[i - k] * absrxp[i];
            dd = d1 + s;
          } else {
            float s = 0.0f;
            for (int i = k; i < N; i++) s += (float)e2[i - k] * absrxp[i];
            dd = d1 + (float)(ce[n1 - 1] ^ hdecp[n1 - 1]) * absrxp[n1 - 1] + s;
          }
          if (dd < dm) {
            dm = dd;
            memcpy(cwp, ce, N);
            nhard = 0;
            for (int i = 0; i < N; i++) nhard += nxor[i];
          }
        } else {
          nrejected++;
        }
      }
      nextpat91(misub, k, iorder, &iflag);
    }
  }

  if (npre2 == 1) {
    bool reset = true;
    ntotal = 0;
    for (int i1 = k; i1 >= 1; i1--) {
      for (int i2 = i1 - 1; i2 >= 1; i2--) {
        ntotal++;
        for (int t = 0; t < ntau; t++) mi[t] = genmrb[i1 - 1][k + t] ^ genmrb[i2 - 1][k + t];
        boxit91(ctx, &reset, mi, ntau, ntotal, i1, i2);
      }
    }
    reset = true;
    memset(misub, 0, k - nord);
    memset(misub + (k - nord), 1, nord);
    int iflag = k - nord + 1;
    while (iflag >= 0) {
      for (int i = 0; i < k; i++) me[i] = m0[i] ^ misub[i];
      mrbencode91(me, ce, genmrb, k);
      for (int i = k; i < N; i++) e2sub[i - k] = ce[i] ^ hdecp[i];
      for (int i2 = 0; i2 <= ntau; i2++) {
        memset(ui, 0, N - k);
        if (i2 > 0) ui[i2 - 1] = 1;
        for (int i = 0; i < N - k; i++) r2pat[i] = e2sub[i] ^ ui[i];
        for (;;) {
          int in1, in2;
          fetchit91(ctx, &reset, r2pat, ntau, &in1, &in2);
          if (!(in1 > 0 && in2 > 0)) break;
          memcpy(mi, misub, k);
          mi[in1 - 1] = 1;
          mi[in2 - 1] = 1;
          int s = 0;
          bool apconflict = false;
          for (int i = 0; i < k; i++) {
            s += mi[i];
            if ((apmaskr[i] & mi[i]) == 1) apconflict = true;
          }
          if (s < nord + npre1 + npre2 || apconflict) break;   /* Fortran: cycle (next i2) */
          for (int i = 0; i < k; i++) me[i] = m0[i] ^ mi[i];
          mrbencode91(me, ce, genmrb, k);
          float dd = 0.0f;
          for (int i = 0; i < N; i++) nxor[i] = ce[i] ^ hdecp[i];
          for (int i = 0; i < N; i++) dd += (float)nxor[i] * absrxp[i];
          if (dd < dm) {
            dm = dd;
            memcpy(cwp, ce, N);
            nhard = 0;
            for (int i = 0; i < N; i++) nhard += nxor[i];
          }
        }
      }
      nextpat91(misub, k, nord, &iflag);
    }
  }

done:
  for (int i = 0; i < N; i++) cw[indices[i] - 1] = cwp[i];   /* back to [message][parity] order */
  memcpy(message91, cw, K);
  if (!ftx_crc14_check91(cw)) nhard = -nhard;
  *nhardmin = nhard;
  *dmin = dm;
}

/* ------------------------------------------------------------------------
 * Hybrid BP / OSD decoder (decode174_91)
 * ---------------------------------------------------------------------- */
void ftx_ldpc_decode174_91(ftx_ldpc_ctx *ctx, const float llr[174], int keff, int maxosd, int norder,
                           const uint8_t apmask[174], uint8_t message91[91], uint8_t cw[174],
                           int *ntype, int *nharderror, float *dmin)
{
  float tov[N][3], toc[M][7], tanhtoc[M][7], zn[N], zsum[N], zsave[3][N];
  int ncheck = 0;

  const int maxiterations = 30;
  int nosd = 0;
  if (maxosd > 3) maxosd = 3;
  if (maxosd == 0) {
    nosd = 1;
    memcpy(zsave[0], llr, sizeof zsave[0]);
  } else if (maxosd > 0) {
    nosd = maxosd;
  }

  memset(toc, 0, sizeof toc);
  memset(tov, 0, sizeof tov);
  memset(tanhtoc, 0, sizeof tanhtoc);
  for (int j = 0; j < M; j++)
    for (int i = 0; i < ftx_ldpc_nrw[j]; i++) toc[j][i] = llr[ftx_ldpc_nm[j][i] - 1];

  int ncnt = 0, nclast = 0;
  for (int i = 0; i < N; i++) zsum[i] = 0.0f;

  for (int iter = 0; iter <= maxiterations; iter++) {
    for (int i = 0; i < N; i++) {                /* bit LLRs (tov = 0 in iteration 0) */
      if (apmask[i] != 1) zn[i] = llr[i] + ((tov[i][0] + tov[i][1]) + tov[i][2]);
      else zn[i] = llr[i];
    }
    for (int i = 0; i < N; i++) zsum[i] += zn[i];
    if (iter > 0 && iter <= maxosd) memcpy(zsave[iter - 1], zsum, sizeof zsum);

    for (int i = 0; i < N; i++) cw[i] = zn[i] > 0.0f ? 1 : 0;   /* codeword yet? */
    ncheck = 0;
    for (int i = 0; i < M; i++) {
      int synd = 0;
      for (int t = 0; t < ftx_ldpc_nrw[i]; t++) synd += cw[ftx_ldpc_nm[i][t] - 1];
      if (synd % 2 != 0) ncheck++;
    }
    if (ncheck == 0) {
      bool crcok = ftx_crc14_check91(cw);
      int nh = 0;
      for (int i = 0; i < N; i++)
        if (cw[i] ? llr[i] < 0.0f : llr[i] > 0.0f) nh++;
      *nharderror = nh;
      if (crcok) {
        memcpy(message91, cw, K);
        float d = 0.0f;
        for (int i = 0; i < N; i++) {
          uint8_t hdec = llr[i] >= 0.0f ? 1 : 0;
          d += (float)(hdec ^ cw[i]) * fabsf(llr[i]);
        }
        *dmin = d;
        *ntype = 1;
        return;
      }
    }

    if (iter > 0) {                              /* early stopping criterion */
      int nd = ncheck - nclast;
      if (nd < 0) ncnt = 0;
      else ncnt++;
      if (ncnt >= 5 && iter >= 10 && ncheck > 15) {
        *nharderror = -1;
        break;
      }
    }
    nclast = ncheck;

    for (int j = 0; j < M; j++) {                /* bits -> checks */
      for (int i = 0; i < ftx_ldpc_nrw[j]; i++) {
        int ibj = ftx_ldpc_nm[j][i] - 1;
        toc[j][i] = zn[ibj];
        for (int kk = 0; kk < 3; kk++)
          if (ftx_ldpc_mn[ibj][kk] - 1 == j) toc[j][i] = toc[j][i] - tov[ibj][kk];
      }
    }
    for (int i = 0; i < M; i++)                  /* checks -> bits */
      for (int t = 0; t < 7; t++) tanhtoc[i][t] = tanhf(-toc[i][t] / 2.0f);
    for (int j = 0; j < N; j++) {
      for (int i = 0; i < 3; i++) {
        int ichk = ftx_ldpc_mn[j][i] - 1;
        float tmn = 1.0f;
        for (int t = 0; t < ftx_ldpc_nrw[ichk]; t++)
          if (ftx_ldpc_nm[ichk][t] - 1 != j) tmn *= tanhtoc[ichk][t];
        float y = platanh(-tmn);
        tov[j][i] = 2 * y;
      }
    }
  }

  for (int i = 0; i < nosd; i++) {
    float dminosd;
    ftx_ldpc_osd174_91(ctx, zsave[i], keff, apmask, norder, message91, cw, nharderror, &dminosd);
    if (*nharderror > 0) {
      float d = 0.0f;
      for (int n = 0; n < N; n++) {
        uint8_t hdec = llr[n] >= 0.0f ? 1 : 0;
        d += (float)(hdec ^ cw[n]) * fabsf(llr[n]);
      }
      *dmin = d;
      *ntype = 2;
      return;
    }
  }
  *ntype = 0;
  *nharderror = -1;
}

void ftx_ldpc_init(ftx_ldpc_ctx *ctx)
{
  memset(ctx->gen_ready, 0, sizeof ctx->gen_ready);
  ctx->lastpat = -1;
  ctx->inext = -1;
}
