/* ftx_pack77.c — the 77-bit FT8/FT4 message codec (all message types).
 *
 * Ported by hand to C from WSJT-X v3.0.2 (git ccdfaf3),
 *   lib/77bit/packjt77.f90 (module packjt77), lib/chkcall.f90, lib/fmtmsg.f90.
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
 * Porting notes.  The Fortran works on fixed-length, blank-padded CHARACTER
 * fields and formatted binary I/O; both are reproduced literally (helpers
 * below) because the gate compares bit strings and decoded text with the
 * original, including its corner cases: a field that does not fit its width
 * is written as '*', list-directed integer reads follow gfortran's rules,
 * and the few locals the original reads before assigning are initialised
 * to zero / blanks (tools/vectors/README.md).  Comments quote the Fortran
 * routine names so the two can be read side by side.
 */
#include "ftx_pack77.h"
#include "ftx_hash.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define NTOKENS  2063592
#define MAX22    4194304
#define MAXGRID4 32400
#define NSEC     86
#define NUSCAN   171
#define NZZZ     46656

static const char A1[]  = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char A2[]  = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char A3[]  = "0123456789";
static const char A4[]  = " ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char C38[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/";
static const char C42[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?";

/* ARRL sections (Field Day, types 0.3/0.4) and US states / Canadian
 * provinces (ARRL RTTY Roundup, type 3) — the Fortran DATA tables. */
static const char *const CSEC[NSEC] = {
  "AB","AK","AL","AR","AZ","BC","CO","CT","DE","EB",
  "EMA","ENY","EPA","EWA","GA","GH","IA","ID","IL","IN",
  "KS","KY","LA","LAX","NS","MB","MDC","ME","MI","MN",
  "MO","MS","MT","NC","ND","NE","NFL","NH","NL","NLI",
  "NM","NNJ","NNY","TER","NTX","NV","OH","OK","ONE","ONN",
  "ONS","OR","ORG","PAC","PR","QC","RI","SB","SC","SCV",
  "SD","SDG","SF","SFL","SJV","SK","SNJ","STX","SV","TN",
  "UT","VA","VI","VT","WCF","WI","WMA","WNY","WPA","WTX",
  "WV","WWA","WY","DX","PE","NB" };
static const char *const CMULT[NUSCAN] = {
  "AL","AK","AZ","AR","CA","CO","CT","DE","FL","GA",
  "HI","ID","IL","IN","IA","KS","KY","LA","ME","MD",
  "MA","MI","MN","MS","MO","MT","NE","NV","NH","NJ",
  "NM","NY","NC","ND","OH","OK","OR","PA","RI","SC",
  "SD","TN","TX","UT","VT","VA","WA","WV","WI","WY",
  "NB","NS","QC","ON","MB","SK","AB","BC","NWT","NF",
  "LB","NU","YT","PEI","DC","DR","FR","GD","GR","OV",
  "ZH","ZL","X01","X02","X03","X04","X05","X06","X07","X08",
  "X09","X10","X11","X12","X13","X14","X15","X16","X17","X18",
  "X19","X20","X21","X22","X23","X24","X25","X26","X27","X28",
  "X29","X30","X31","X32","X33","X34","X35","X36","X37","X38",
  "X39","X40","X41","X42","X43","X44","X45","X46","X47","X48",
  "X49","X50","X51","X52","X53","X54","X55","X56","X57","X58",
  "X59","X60","X61","X62","X63","X64","X65","X66","X67","X68",
  "X69","X70","X71","X72","X73","X74","X75","X76","X77","X78",
  "X79","X80","X81","X82","X83","X84","X85","X86","X87","X88",
  "X89","X90","X91","X92","X93","X94","X95","X96","X97","X98",
  "X99" };

/* ------------------------------------------------------------------------
 * Fortran CHARACTER field helpers.  A field of length len is blank padded
 * and additionally NUL terminated at [len] (the buffer has len+1 bytes).
 * ---------------------------------------------------------------------- */
static void fpad(char *s, int from, int len)
{
  for (int i = from; i < len; i++) s[i] = ' ';
  s[len] = '\0';
}
static void fsetn(char *dst, int len, const char *src, int n)   /* dst = src(1:n) */
{
  if (n < 0) n = 0;
  if (n > len) n = len;
  memmove(dst, src, (size_t)n);
  fpad(dst, n, len);
}
static void fset(char *dst, int len, const char *src)           /* dst = src */
{
  fsetn(dst, len, src, (int)strlen(src));
}
static int ftrim(const char *s, int len)                         /* len_trim */
{
  while (len > 0 && s[len - 1] == ' ') len--;
  return len;
}
static void ftrimcpy(char *dst, const char *s, int len)          /* dst = trim(s), NUL */
{
  int n = ftrim(s, len);
  memcpy(dst, s, (size_t)n);
  dst[n] = '\0';
}
static bool feq(const char *s, int len, const char *lit)         /* s .eq. lit */
{
  int n = (int)strlen(lit);
  for (int i = 0; i < len || i < n; i++) {
    char a = i < len ? s[i] : ' ', b = i < n ? lit[i] : ' ';
    if (a != b) return false;
  }
  return true;
}
static bool fprefix(const char *s, int len, const char *lit)     /* s(1:n) .eq. lit */
{
  int n = (int)strlen(lit);
  for (int i = 0; i < n; i++)
    if ((i < len ? s[i] : ' ') != lit[i]) return false;
  return true;
}
static int findex(const char *s, int len, const char *sub)       /* index(), 1-based, 0 = none */
{
  int n = (int)strlen(sub);
  for (int i = 0; i + n <= len; i++)
    if (memcmp(s + i, sub, (size_t)n) == 0) return i + 1;
  return 0;
}
static void fsub(char *dst, int dlen, const char *s, int from, int to) /* dst = s(from:to) */
{
  int n = to - from + 1;
  if (n < 0 || from < 1) n = 0;
  fsetn(dst, dlen, s + (from - 1), n);
}
static void fadjustl(char *s, int len)
{
  int i = 0;
  while (i < len && s[i] == ' ') i++;
  memmove(s, s + i, (size_t)(len - i));
  fpad(s, len - i, len);
}
static void fadjustr(char *s, int len)
{
  int t = ftrim(s, len);
  memmove(s + (len - t), s, (size_t)t);
  memset(s, ' ', (size_t)(len - t));
  s[len] = '\0';
}
static int cidx(const char *alpha, char ch)                      /* index(alpha, ch) */
{
  if (ch == '\0') return 0;
  const char *p = strchr(alpha, ch);
  return p != NULL ? (int)(p - alpha) + 1 : 0;
}
static bool is_digit(char c)  { return c >= '0' && c <= '9'; }
static bool is_letter(char c) { return c >= 'A' && c <= 'Z'; }
static bool is_let2(char c)   { return is_letter(c) || (c >= 'a' && c <= 'z'); }

/* Fortran Bw.w output (zero padded binary; '*' when the value does not fit
 * or is negative — a negative INTEGER prints its 32/64-bit pattern, which
 * never fits the widths used here) and its inverse. */
static void wbits(char *c, int pos, int width, int64_t v)
{
  if (v < 0 || (width < 62 && v >= ((int64_t)1 << width))) {
    memset(c + pos, '*', (size_t)width);
    return;
  }
  for (int i = 0; i < width; i++)
    c[pos + i] = ((v >> (width - 1 - i)) & 1) ? '1' : '0';
}
static int64_t rbits(const char *c, int pos, int width)
{
  int64_t v = 0;
  for (int i = 0; i < width; i++) v = (v << 1) | (c[pos + i] == '1' ? 1 : 0);
  return v;
}

/* Fortran Iw.m output into dst (w chars + NUL). */
static void fmt_int(char *dst, int w, int m, int v)
{
  char digits[24];
  unsigned long long a = v < 0 ? (unsigned long long)(-(long long)v) : (unsigned long long)v;
  int n = snprintf(digits, sizeof digits, "%0*llu", m, a);
  int total = n + (v < 0 ? 1 : 0);
  if (total > w) {
    memset(dst, '*', (size_t)w);
    dst[w] = '\0';
    return;
  }
  memset(dst, ' ', (size_t)(w - total));
  int p = w - total;
  if (v < 0) dst[p++] = '-';
  memcpy(dst + p, digits, (size_t)n);
  dst[w] = '\0';
}

/* gfortran list-directed READ of one default INTEGER from a character
 * field.  Returns 1 = value stored, 2 = null value (variable unchanged),
 * 0 = error condition, -1 = end of file (blank field). */
static int fread_int(const char *s, int len, int *val)
{
  int i = 0;
  while (i < len && s[i] == ' ') i++;
  if (i == len) return -1;
  if (s[i] == ',' || s[i] == '/') return 2;
  int j = i;
  while (j < len && is_digit(s[j])) j++;
  if (j > i && j < len && s[j] == '*') {            /* repeat form r*c */
    i = j + 1;
    if (i == len || s[i] == ' ' || s[i] == ',' || s[i] == '/') return 2;
  }
  int sign = 1;
  if (s[i] == '+' || s[i] == '-') {
    if (s[i] == '-') sign = -1;
    i++;
  }
  if (i == len || !is_digit(s[i])) return 0;
  int64_t v = 0;
  while (i < len && is_digit(s[i])) {
    v = v * 10 + (s[i] - '0');
    if (v > 2147483648LL) return 0;
    i++;
  }
  if (i < len && s[i] != ' ' && s[i] != ',' && s[i] != '/') return 0;
  v *= sign;
  if (v > 2147483647LL || v < -2147483648LL) return 0;
  *val = (int)v;
  return 1;
}

/* Formatted READ with Z6 (blanks are null): six characters of hex. */
static bool fread_hex6(const char *s, int *val)
{
  int v = 0;
  for (int i = 0; i < 6; i++) {
    char c = s[i];
    int d;
    if (c == ' ') continue;
    if (is_digit(c)) d = c - '0';
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else return false;
    v = v * 16 + d;
  }
  *val = v;
  return true;
}

/* ------------------------------------------------------------------------
 * Hash tables (module state → ctx)
 * ---------------------------------------------------------------------- */
static void bracket(char *c13, const char *call, int len)       /* c13 = '<'//trim(call)//'>' */
{
  char t[16], b[20];
  ftrimcpy(t, call, len);
  snprintf(b, sizeof b, "<%s>", t);
  fset(c13, 13, b);
}

static void hash10(const ftx_pack77_ctx *ctx, int n10, char *c13)
{
  fset(c13, 13, "<...>");
  if (n10 < 0 || n10 > 1023) return;
  if (ftrim(ctx->calls10[n10], 13) > 0) bracket(c13, ctx->calls10[n10], 13);
}

static void hash12(const ftx_pack77_ctx *ctx, int n12, char *c13)
{
  fset(c13, 13, "<...>");
  if (n12 < 0 || n12 > 4095) return;
  if (ftrim(ctx->calls12[n12], 13) > 0) bracket(c13, ctx->calls12[n12], 13);
}

static void hash22(const ftx_pack77_ctx *ctx, int n22, char *c13)
{
  fset(c13, 13, "<...>");
  for (int i = 0; i < ctx->nzhash; i++) {
    if (ctx->ihash22[i] == n22) {
      bracket(c13, ctx->calls22[i], 13);
      return;
    }
  }
}

static void save_hash_call(ftx_pack77_ctx *ctx, const char *c13, int *n10, int *n12, int *n22)
{
  char cw[14];
  memcpy(cw, c13, 13);
  cw[13] = '\0';
  if (cw[0] == ' ' || fprefix(cw, 13, "<...>")) return;
  if (cw[0] == '<') {                 /* cw=cw(2:) */
    memmove(cw, cw + 1, 12);
    cw[12] = ' ';
  }
  int i = findex(cw, 13, ">");
  if (i > 0) fpad(cw, i - 1, 13);     /* cw(i:)=blanks */
  if (ftrim(cw, 13) < 3) return;

  int h10 = ftx_ihashcall(cw, 10);
  if (n10 != NULL) *n10 = h10;
  if (h10 >= 0 && h10 <= 1023 && !feq(cw, 13, ctx->mycall13)) memcpy(ctx->calls10[h10], cw, 14);

  int h12 = ftx_ihashcall(cw, 12);
  if (n12 != NULL) *n12 = h12;
  if (h12 >= 0 && h12 <= 4095 && !feq(cw, 13, ctx->mycall13)) memcpy(ctx->calls12[h12], cw, 14);

  int h22 = ftx_ihashcall(cw, 22);
  if (n22 != NULL) *n22 = h22;
  bool exists = false;
  for (int k = 0; k < FTX_MAXHASH; k++)
    if (ctx->ihash22[k] == h22) exists = true;
  if (exists) {                       /* refresh the callsign of an existing entry */
    for (int k = 0; k < FTX_MAXHASH; k++)
      if (ctx->ihash22[k] == h22) memcpy(ctx->calls22[k], cw, 14);
    return;
  }
  for (int k = FTX_MAXHASH - 1; k >= 1; k--) {   /* move the table down, insert at the top */
    ctx->ihash22[k] = ctx->ihash22[k - 1];
    memcpy(ctx->calls22[k], ctx->calls22[k - 1], 14);
  }
  ctx->ihash22[0] = h22;
  memcpy(ctx->calls22[0], cw, 14);
  if (ctx->nzhash < FTX_MAXHASH) ctx->nzhash++;
}

static void add_call_to_recent_calls(ftx_pack77_ctx *ctx, const char *callsign)
{
  bool ladd = true;
  for (int i = 0; i < FTX_MAXRECENT - 1; i++)
    if (memcmp(ctx->recent_calls[i], callsign, 13) == 0) ladd = false;
  if (ladd) {
    for (int i = FTX_MAXRECENT - 1; i >= 1; i--)
      memcpy(ctx->recent_calls[i], ctx->recent_calls[i - 1], 14);
    memcpy(ctx->recent_calls[0], callsign, 13);
    ctx->recent_calls[0][13] = '\0';
  }
}

/* ------------------------------------------------------------------------
 * chkcall (lib/chkcall.f90), callok (packjt77.f90)
 * ---------------------------------------------------------------------- */
static void chkcall(const char *w, char *bc, bool *cok)
{
  *cok = true;
  fsetn(bc, 6, w, 6);                                   /* bc=w(1:6) */
  int n1 = ftrim(w, 13);
  if (n1 > 11) goto fail;
  if (findex(w, 13, ".") >= 1) goto fail;
  if (findex(w, 13, "+") >= 1) goto fail;
  if (findex(w, 13, "-") >= 1) goto fail;
  if (findex(w, 13, "?") >= 1) goto fail;
  if (n1 > 6 && findex(w, 13, "/") <= 0) goto fail;

  int i0 = findex(w, 13, "/");
  {
    int a = i0 - 1, b = n1 - i0;
    if ((a > b ? a : b) > 6) goto fail;                 /* base call must be < 7 characters */
  }
  if (i0 >= 2 && i0 <= n1 - 1) {                        /* base call of a compound call */
    if (i0 - 1 <= n1 - i0) fsub(bc, 6, w, i0 + 1, n1);
    else fsub(bc, 6, w, 1, i0 - 1);
  }
  int nbc = ftrim(bc, 6);
  if (nbc > 6) goto fail;
  if (!is_letter(bc[0]) && !is_letter(bc[1])) goto fail;   /* one of the first two is a letter */
  if (bc[0] == 'Q' && !fprefix(bc, 6, "QU1RK")) goto fail; /* no Q calls, except the placeholder */
  int i1 = 0;                                           /* digit in 2nd or 3rd position */
  if (is_digit(bc[1])) i1 = 2;
  if (is_digit(bc[2])) i1 = 3;
  if (i1 == 0) goto fail;
  if (i1 == nbc) goto fail;                             /* suffix of 1-3 letters */
  {
    int n = 0;
    for (int i = i1 + 1; i <= nbc; i++) {
      if (!is_letter(bc[i - 1])) goto fail;
      n++;
    }
    if (n >= 1 && n <= 3) return;
  }
fail:
  *cok = false;
}

static bool callok(const char *w, int n)              /* w = trim(c13), n characters */
{
  if (n < 3) return false;
  if (w[0] == 'Q') return false;
  int i0;
  {
    int i;
    for (i = n; i >= 1; i--)
      if (is_digit(w[i - 1])) break;
    i0 = i;                                            /* call area position */
  }
  if (i0 != 2 && i0 != 3) return false;
  char pfx[3], sfx[4];
  fsub(pfx, 2, w, 1, i0 - 1);
  fsub(sfx, 3, w, i0 + 1, n);
  int nlp = 0, np = ftrim(pfx, 2);
  for (int i = 0; i < np; i++)
    if (is_let2(pfx[i])) nlp++;
  if (nlp == 0) return false;                          /* prefix needs a letter */
  int nls = 0, ns = ftrim(sfx, 3);
  for (int i = 0; i < ns; i++)
    if (is_let2(sfx[i])) nls++;
  if (nls < ns) return false;                          /* suffix all letters */
  return true;
}

/* ------------------------------------------------------------------------
 * pack28 / unpack28
 * ---------------------------------------------------------------------- */
static void pack28(ftx_pack77_ctx *ctx, char *c13, int *n28)
{
  /* The Fortran assigns `callsign` for 3DA0 and 3X prefixes here, but
   * reassigns it below before use, so those two lines have no effect. */
  int64_t n28v = -1;

  if (fprefix(c13, 13, "DE ")) { n28v = 0; goto done; }       /* special tokens */
  if (fprefix(c13, 13, "QRZ ")) { n28v = 1; goto done; }
  if (fprefix(c13, 13, "CQ ")) { n28v = 2; goto done; }

  if (fprefix(c13, 13, "CQ_")) {
    int n = ftrim(c13, 13);
    if (n >= 4 && n <= 7) {
      int nlet = 0, nnum = 0;
      for (int i = 4; i <= n; i++) {
        char c = c13[i - 1];
        if (c >= 'A' && c <= 'Z') nlet++;
        if (c >= '0' && c <= '9') nnum++;
      }
      if (nnum == 3 && nlet == 0) {
        int nqsy = 0;
        for (int i = 4; i <= 3 + nnum; i++) nqsy = nqsy * 10 + (c13[i - 1] - '0');
        n28v = 3 + nqsy;
        goto done;
      }
      if (nlet >= 1 && nlet <= 4 && nnum == 0) {
        char c4[5];
        fsub(c4, 4, c13, 4, n);
        fadjustr(c4, 4);
        int m = 0;
        for (int i = 0; i < 4; i++) {
          int j = 0;
          if (c4[i] >= 'A' && c4[i] <= 'Z') j = c4[i] - 'A' + 1;
          m = 27 * m + j;
        }
        n28v = 3 + 1000 + m;
        goto done;
      }
    }
  }

  if (c13[0] == '<') {                                 /* <...> callsign: 22-bit hash */
    save_hash_call(ctx, c13, NULL, NULL, NULL);
    int i2 = findex(c13, 13, ">");
    char t[14];
    fsub(t, 13, c13, 2, i2 - 1);
    memcpy(c13, t, 14);                                /* the Fortran modifies its argument */
    n28v = NTOKENS + ftx_ihashcall(c13, 22);
    goto done;
  }

  {                                                    /* standard callsign? */
    int n = ftrim(c13, 13);
    int iarea;
    {
      int i;
      for (i = n; i >= 2; i--)
        if (is_digit(c13[i - 1])) break;
      iarea = i;                                       /* call-area digit */
    }
    int npdig = 0, nplet = 0, nslet = 0;
    for (int i = 1; i <= iarea - 1; i++) {
      if (is_digit(c13[i - 1])) npdig++;
      if (is_letter(c13[i - 1])) nplet++;
    }
    for (int i = iarea + 1; i <= n; i++)
      if (is_letter(c13[i - 1])) nslet++;
    if (iarea < 2 || iarea > 3 || nplet == 0 || npdig >= iarea - 1 || nslet > 3) {
      save_hash_call(ctx, c13, NULL, NULL, NULL);      /* nonstandard: 22-bit hash */
      n28v = NTOKENS + ftx_ihashcall(c13, 22);
      goto done;
    }
    save_hash_call(ctx, c13, NULL, NULL, NULL);
    char callsign[7];
    if (iarea == 2) {
      callsign[0] = ' ';
      fsetn(callsign + 1, 5, c13, 5);
    } else {
      fsetn(callsign, 6, c13, 6);
    }
    int64_t i1 = cidx(A1, callsign[0]) - 1;
    int64_t i2 = cidx(A2, callsign[1]) - 1;
    int64_t i3 = cidx(A3, callsign[2]) - 1;
    int64_t i4 = cidx(A4, callsign[3]) - 1;
    int64_t i5 = cidx(A4, callsign[4]) - 1;
    int64_t i6 = cidx(A4, callsign[5]) - 1;
    n28v = 36 * 10 * 27 * 27 * 27 * i1 + 10 * 27 * 27 * 27 * i2 + 27 * 27 * 27 * i3 +
           27 * 27 * i4 + 27 * i5 + i6;
    n28v += NTOKENS + MAX22;
  }
done:
  *n28 = (int)(n28v & (((int64_t)1 << 28) - 1));
}

static void unpack28(const ftx_pack77_ctx *ctx, int n28_0, char *c13, bool *success)
{
  *success = true;
  int n28 = n28_0;
  if (n28 < NTOKENS) {                                 /* DE, QRZ, CQ, CQ_nnn, CQ_aaaa */
    if (n28 == 0) fset(c13, 13, "DE");
    if (n28 == 1) fset(c13, 13, "QRZ");
    if (n28 == 2) fset(c13, 13, "CQ");
    if (n28 <= 2) goto l900;
    if (n28 <= 1002) {
      char b[16];
      snprintf(b, sizeof b, "CQ_%03d", n28 - 3);
      fset(c13, 13, b);
      goto l900;
    }
    if (n28 <= 532443) {
      int n = n28 - 1003;
      int i1 = n / (27 * 27 * 27);
      n -= 27 * 27 * 27 * i1;
      int i2 = n / (27 * 27);
      n -= 27 * 27 * i2;
      int i3 = n / 27;
      int i4 = n - 27 * i3;
      char t[14];
      t[0] = A4[i1]; t[1] = A4[i2]; t[2] = A4[i3]; t[3] = A4[i4];
      fpad(t, 4, 13);
      fadjustl(t, 13);
      char b[20];
      memcpy(b, "CQ_", 3);
      memcpy(b + 3, t, 10);
      b[13] = '\0';
      fset(c13, 13, b);
      goto l900;
    }
  }
  n28 -= NTOKENS;
  if (n28 < MAX22) {                                   /* 22-bit hash */
    hash22(ctx, n28, c13);
    goto l900;
  }
  {                                                    /* standard callsign */
    int n = n28 - MAX22;
    int i1 = n / (36 * 10 * 27 * 27 * 27);
    n -= 36 * 10 * 27 * 27 * 27 * i1;
    int i2 = n / (10 * 27 * 27 * 27);
    n -= 10 * 27 * 27 * 27 * i2;
    int i3 = n / (27 * 27 * 27);
    n -= 27 * 27 * 27 * i3;
    int i4 = n / (27 * 27);
    n -= 27 * 27 * i4;
    int i5 = n / 27;
    int i6 = n - 27 * i5;
    c13[0] = A1[i1]; c13[1] = A2[i2]; c13[2] = A3[i3];
    c13[3] = A4[i4]; c13[4] = A4[i5]; c13[5] = A4[i6];
    fpad(c13, 6, 13);
    fadjustl(c13, 13);
    if (!callok(c13, ftrim(c13, 13))) {
      fset(c13, 13, "QU1RK");
      *success = false;
    }
  }
l900:
  {
    int i0 = findex(c13, 13, " ");
    if (i0 != 0 && i0 < ftrim(c13, 13)) {              /* embedded blank */
      fset(c13, 13, "QU1RK");
      *success = false;
    }
  }
}

/* ------------------------------------------------------------------------
 * split77: upper case, collapse blanks, split into up to 19 words of 13
 * ---------------------------------------------------------------------- */
typedef char word13[14];

static void split77(char *msg, int *nwords, int nw[19], word13 w[19])
{
  int iz = ftrim(msg, 37);
  int j = 0, k = 0, n = 0;
  char c0 = ' ';
  for (int i = 0; i < 19; i++) fpad(w[i], 0, 13);
  for (int i = 0; i < iz; i++) {
    if (msg[i] == '\0') msg[i] = ' ';
    char c = msg[i];
    if (c == ' ' && c0 == ' ') continue;               /* skip leading / repeated blanks */
    if (c != ' ' && c0 == ' ') {                       /* new word */
      k++;
      n = 0;
    }
    j++;
    n++;
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    msg[j - 1] = c;
    if (n <= 13 && k >= 1 && k <= 19) w[k - 1][n - 1] = c;
    c0 = c;
  }
  iz = j;
  *nwords = k;
  if (*nwords <= 0) return;
  for (int i = 0; i < *nwords && i < 19; i++) nw[i] = ftrim(w[i], 13);
  fpad(msg, iz, 37);
  if (*nwords < 3) return;
  char bcall_1[7];
  bool ok1;
  chkcall(w[2], bcall_1, &ok1);
  if (ok1 && fprefix(w[0], 13, "CQ ")) {               /* "CQ DX K1ABC ..." → "CQ_DX" */
    char t[14];
    memcpy(t, "CQ_", 3);
    memcpy(t + 3, w[1], 10);
    t[13] = '\0';
    memcpy(w[0], t, 14);
    for (int i = 1; i <= 11; i++) memcpy(w[i], w[i + 1], 14);   /* w(2:12)=w(3:13) */
    (*nwords)--;
  }
}

/* ------------------------------------------------------------------------
 * Grids
 * ---------------------------------------------------------------------- */
static bool is_grid4(const char *g)                   /* g: 4 characters */
{
  return ftrim(g, 4) == 4 &&
         g[0] >= 'A' && g[0] <= 'R' && g[1] >= 'A' && g[1] <= 'R' &&
         g[2] >= '0' && g[2] <= '9' && g[3] >= '0' && g[3] <= '9';
}
static bool is_grid6_strict(const char *g)            /* pack77_5: exactly 6 */
{
  return ftrim(g, 6) == 6 &&
         g[0] >= 'A' && g[0] <= 'R' && g[1] >= 'A' && g[1] <= 'R' &&
         g[2] >= '0' && g[2] <= '9' && g[3] >= '0' && g[3] <= '9' &&
         g[4] >= 'A' && g[4] <= 'X' && g[5] >= 'A' && g[5] <= 'X';
}
static bool is_grid6_or4(const char *g)               /* pack77_06: 4 or 6 */
{
  int l = ftrim(g, 6);
  return (l == 6 || l == 4) &&
         g[0] >= 'A' && g[0] <= 'R' && g[1] >= 'A' && g[1] <= 'R' &&
         g[2] >= '0' && g[2] <= '9' && g[3] >= '0' && g[3] <= '9' &&
         (l == 4 || (g[4] >= 'A' && g[4] <= 'X' && g[5] >= 'A' && g[5] <= 'X'));
}
static int grid4_to_int(const char *g)
{
  return (g[0] - 'A') * 18 * 10 * 10 + (g[1] - 'A') * 10 * 10 + (g[2] - '0') * 10 + (g[3] - '0');
}

static void to_grid4(int n, char *grid4, bool *ok)
{
  *ok = false;
  int j1 = n / (18 * 10 * 10);
  if (j1 < 0 || j1 > 17) return;
  n -= j1 * 18 * 10 * 10;
  int j2 = n / (10 * 10);
  if (j2 < 0 || j2 > 17) return;
  n -= j2 * 10 * 10;
  int j3 = n / 10;
  if (j3 < 0 || j3 > 9) return;
  int j4 = n - j3 * 10;
  if (j4 < 0 || j4 > 9) return;
  grid4[0] = (char)('A' + j1); grid4[1] = (char)('A' + j2);
  grid4[2] = (char)('0' + j3); grid4[3] = (char)('0' + j4);
  grid4[4] = '\0';
  *ok = true;
}

static void to_grid6(int n, char *grid6, bool *ok)    /* 24-based (EU VHF) */
{
  *ok = false;
  int j1 = n / (18 * 10 * 10 * 24 * 24);
  if (j1 < 0 || j1 > 17) return;
  n -= j1 * 18 * 10 * 10 * 24 * 24;
  int j2 = n / (10 * 10 * 24 * 24);
  if (j2 < 0 || j2 > 17) return;
  n -= j2 * 10 * 10 * 24 * 24;
  int j3 = n / (10 * 24 * 24);
  if (j3 < 0 || j3 > 9) return;
  n -= j3 * 10 * 24 * 24;
  int j4 = n / (24 * 24);
  if (j4 < 0 || j4 > 9) return;
  n -= j4 * 24 * 24;
  int j5 = n / 24;
  if (j5 < 0 || j5 > 23) return;
  int j6 = n - j5 * 24;
  if (j6 < 0 || j6 > 23) return;
  grid6[0] = (char)('A' + j1); grid6[1] = (char)('A' + j2);
  grid6[2] = (char)('0' + j3); grid6[3] = (char)('0' + j4);
  grid6[4] = (char)('A' + j5); grid6[5] = (char)('A' + j6);
  grid6[6] = '\0';
  *ok = true;
}

static void to_grid(int n, char *grid6, bool *ok)     /* 25-based, 4 or 6 chars (WSPR) */
{
  *ok = false;
  int j1 = n / (18 * 10 * 10 * 25 * 25);
  if (j1 < 0 || j1 > 17) return;
  n -= j1 * 18 * 10 * 10 * 25 * 25;
  int j2 = n / (10 * 10 * 25 * 25);
  if (j2 < 0 || j2 > 17) return;
  n -= j2 * 10 * 10 * 25 * 25;
  int j3 = n / (10 * 25 * 25);
  if (j3 < 0 || j3 > 9) return;
  n -= j3 * 10 * 25 * 25;
  int j4 = n / (25 * 25);
  if (j4 < 0 || j4 > 9) return;
  n -= j4 * 25 * 25;
  int j5 = n / 25;
  if (j5 < 0 || j5 > 24) return;
  int j6 = n - j5 * 25;
  if (j6 < 0 || j6 > 24) return;
  fpad(grid6, 0, 6);
  grid6[0] = (char)('A' + j1); grid6[1] = (char)('A' + j2);
  grid6[2] = (char)('0' + j3); grid6[3] = (char)('0' + j4);
  if (j5 != 24 || j6 != 24) {
    grid6[4] = (char)('A' + j5);
    grid6[5] = (char)('A' + j6);
  }
  *ok = true;
}

/* ------------------------------------------------------------------------
 * Free text: 13 characters of a 42-symbol alphabet in 71 bits
 * (packtext77 / unpacktext77 with the mp_short_* byte arithmetic)
 * ---------------------------------------------------------------------- */
static void packtext77(const char *c13, char *c71)
{
  char w[14];
  memcpy(w, c13, 13);
  w[13] = '\0';
  fadjustr(w, 13);
  uint8_t q[9] = { 0 };                                /* big-endian 72-bit accumulator */
  for (int i = 0; i < 13; i++) {
    int j = cidx(C42, w[i]) - 1;
    if (j < 0) j = 0;
    unsigned carry = 0;
    for (int k = 8; k >= 0; k--) {                     /* q = 42*q */
      unsigned t = q[k] * 42u + carry;
      q[k] = (uint8_t)(t & 0xff);
      carry = t >> 8;
    }
    carry = (unsigned)j;
    for (int k = 8; k >= 0; k--) {                     /* q = q + j */
      unsigned t = q[k] + carry;
      q[k] = (uint8_t)(t & 0xff);
      carry = t >> 8;
    }
  }
  wbits(c71, 0, 7, q[0]);
  for (int k = 1; k <= 8; k++) wbits(c71, 7 + 8 * (k - 1), 8, q[k]);
}

static void unpacktext77(const char *c71, char *c13)
{
  uint8_t q[9];
  q[0] = (uint8_t)rbits(c71, 0, 7);
  for (int k = 1; k <= 8; k++) q[k] = (uint8_t)rbits(c71, 7 + 8 * (k - 1), 8);
  for (int i = 12; i >= 0; i--) {
    unsigned ir = 0;
    for (int k = 0; k < 9; k++) {                      /* q, ir = divmod(q, 42) */
      unsigned t = 256u * ir + q[k];
      q[k] = (uint8_t)(t / 42u);
      ir = t % 42u;
    }
    c13[i] = C42[ir];
  }
  c13[13] = '\0';
}

/* ------------------------------------------------------------------------
 * pack77_* — one per message type
 * ---------------------------------------------------------------------- */
static void pack77_01(ftx_pack77_ctx *ctx, int nwords, word13 w[19], int *i3, int *n3, char *c77)
{
  /* Type 0.1, DXpedition mode: "K1ABC RR73; W9XYZ <KH1/KH7Z> -11"  28 28 10 5 */
  char bcall_1[7], bcall_2[7], c13[14];
  bool ok1, ok2;
  if (nwords != 5) return;
  if (!feq(w[1], 13, "RR73;")) return;
  if (w[3][0] != '<') return;
  if (findex(w[3], 13, ">") < 1) return;
  int n = -99;
  fread_int(w[4], 13, &n);                             /* err → n stays -99 */
  if (n == -99) return;
  int n5 = (n + 30) / 2;
  if (n5 < 0) n5 = 0;
  if (n5 > 31) n5 = 31;
  chkcall(w[0], bcall_1, &ok1);
  if (!ok1) return;
  chkcall(w[2], bcall_2, &ok2);
  if (!ok2) return;

  *i3 = 0;
  *n3 = 1;
  int n28a, n28b, n10 = 0, n12 = 0, n22 = 0;
  pack28(ctx, w[0], &n28a);
  pack28(ctx, w[2], &n28b);
  save_hash_call(ctx, w[3], &n10, &n12, &n22);
  int i2 = findex(w[3], 13, ">");
  fsub(c13, 13, w[3], 2, i2 - 1);
  n10 = ftx_ihashcall(c13, 10);
  wbits(c77, 0, 28, n28a);
  wbits(c77, 28, 28, n28b);
  wbits(c77, 56, 10, n10);
  wbits(c77, 66, 5, n5);
  wbits(c77, 71, 3, *n3);
  wbits(c77, 74, 3, *i3);
}

static void pack77_03(ftx_pack77_ctx *ctx, int nwords, word13 w[19], int *i3, int *n3, char *c77)
{
  /* Types 0.3 / 0.4, ARRL Field Day: "WA9XYZ KA1ABC R 16A EMA"  28 28 1 4 3 7 */
  char bcall_1[7], bcall_2[7];
  bool ok1, ok2;
  if (nwords < 4 || nwords > 5) return;
  chkcall(w[0], bcall_1, &ok1);
  chkcall(w[1], bcall_2, &ok2);
  if (!ok1 || !ok2) return;
  int isec = -1;
  for (int i = 0; i < NSEC; i++) {
    if (fprefix(w[nwords - 1], 3, CSEC[i]) && feq(w[nwords - 1], 3, CSEC[i])) {   /* csec(i).eq.w(nwords)(1:3) */
      isec = i + 1;
      break;
    }
  }
  if (isec == -1) return;
  if (nwords == 5 && !feq(w[2], 13, "R")) return;

  int ntx = -1;
  int j = ftrim(w[nwords - 2], 13) - 1;
  fread_int(w[nwords - 2], j, &ntx);                   /* err/end → ntx stays -1 */
  if (ntx < 1 || ntx > 32) return;
  int nclass = w[nwords - 2][j] - 'A';

  int m = ftrim(w[nwords - 1], 13);                    /* section abbreviation length */
  if (m < 2 || m > 3) return;

  *i3 = 0;
  *n3 = 3;
  int intx = ntx - 1;
  if (intx >= 16) {
    *n3 = 4;
    intx = ntx - 17;
  }
  int n28a, n28b;
  pack28(ctx, w[0], &n28a);
  pack28(ctx, w[1], &n28b);
  int ir = fprefix(w[2], 13, "R ") ? 1 : 0;
  wbits(c77, 0, 28, n28a);
  wbits(c77, 28, 28, n28b);
  wbits(c77, 56, 1, ir);
  wbits(c77, 57, 4, intx);
  wbits(c77, 61, 3, nclass);
  wbits(c77, 64, 7, isec);
  wbits(c77, 71, 3, *n3);
  wbits(c77, 74, 3, *i3);
}

static void pack77_06(ftx_pack77_ctx *ctx, int nwords, word13 w[19], int *i3, int *n3, char *c77,
                      int i3_hint, int n3_hint)
{
  /* Type 0.6, the three WSPR message types */
  int m1 = ftrim(w[0], 13), m2 = ftrim(w[1], 13), m3 = ftrim(w[2], 13);
  if (nwords == 3 && m1 >= 3 && m1 <= 6 && m2 == 4 && m3 <= 2) {   /* WSPR type 1 */
    if (!is_grid4(w[1])) return;
    if (!is_digit(w[2][0])) return;
    if (m3 == 2 && !is_digit(w[2][1])) return;
    *i3 = 0;
    *n3 = 6;
    int n28;
    pack28(ctx, w[0], &n28);
    int igrid4 = grid4_to_int(w[1]);
    int idbm = 0;
    fread_int(w[2], 13, &idbm);
    if (idbm < 0) idbm = 0;
    if (idbm > 60) idbm = 60;
    idbm = (int)lroundf(0.3f * (float)idbm);
    wbits(c77, 0, 28, n28);
    wbits(c77, 28, 15, igrid4);
    wbits(c77, 43, 5, idbm);
    wbits(c77, 48, 1, 0);
    wbits(c77, 49, 1, 0);
    wbits(c77, 50, 21, 0);
    wbits(c77, 71, 3, *n3);
    wbits(c77, 74, 3, *i3);
    return;
  }
  if (nwords == 2 && m1 >= 5 && m1 <= 10 && m2 <= 2) {               /* WSPR type 2 */
    int i1 = findex(w[0], 13, "/");
    if (i1 < 2 || i1 == m1) return;
    if (!is_digit(w[1][0])) return;
    if (i1 == m1 - 3 && !is_digit(w[0][m1 - 1])) return;
    if (m2 == 2 && !is_digit(w[1][1])) return;
    char bcall[7];
    bool ok;
    chkcall(w[0], bcall, &ok);
    if (!ok) return;
    int npfx = 0;                                      /* (uninitialised in the Fortran for a 4+ char suffix) */
    if (i1 <= 4) {                                     /* prefix */
      npfx = cidx(A2, w[0][0]) - 1;
      if (i1 >= 3) npfx = 36 * npfx + cidx(A2, w[0][1]) - 1;
      if (i1 == 4) npfx = 36 * npfx + cidx(A2, w[0][2]) - 1;
    } else {                                           /* suffix */
      if (m1 - i1 == 1) npfx = cidx(A2, w[0][i1]) - 1;
      if (m1 - i1 == 2) npfx = 36 * (cidx(A2, w[0][i1]) - 1) + cidx(A2, w[0][i1 + 1]) - 1;
      if (m1 - i1 == 3) {
        if (!is_digit(w[0][i1 + 2])) return;           /* third suffix character must be a digit */
        npfx = 36 * 10 * (cidx(A2, w[0][i1]) - 1) + 10 * (cidx(A2, w[0][i1 + 1]) - 1) +
               cidx(A2, w[0][i1 + 2]) - 1;
      }
      npfx += NZZZ;
    }
    *i3 = 0;
    *n3 = 6;
    char t[14];
    fsetn(t, 13, bcall, 6);
    int n28;
    pack28(ctx, t, &n28);
    int idbm = 0;
    fread_int(w[1], 13, &idbm);
    if (idbm < 0) idbm = 0;
    if (idbm > 60) idbm = 60;
    idbm = (int)lroundf(0.3f * (float)idbm);
    wbits(c77, 0, 28, n28);
    wbits(c77, 28, 16, npfx);
    wbits(c77, 44, 5, idbm);
    wbits(c77, 49, 1, 1);
    wbits(c77, 50, 21, 0);
    wbits(c77, 71, 3, *n3);
    wbits(c77, 74, 3, *i3);
    return;
  }
  if (i3_hint == 0 && n3_hint == 6 && nwords == 2 && m1 >= 5 && m1 <= 12 && m2 <= 6) {
    /* WSPR type 3 (only with the caller's 0.6 hint) */
    if (findex(w[0], 13, "<") < 1 || findex(w[0], 13, ">") < 1) return;
    char grid6[7];
    fsetn(grid6, 6, w[1], 6);
    if (!is_grid6_or4(grid6)) return;
    *i3 = 0;
    *n3 = 6;
    int n28;
    pack28(ctx, w[0], &n28);
    int n22 = n28 - NTOKENS;
    int k1 = (grid6[0] - 'A') * 18 * 10 * 10 * 25 * 25;
    int k2 = (grid6[1] - 'A') * 10 * 10 * 25 * 25;
    int k3 = (grid6[2] - '0') * 10 * 25 * 25;
    int k4 = (grid6[3] - '0') * 25 * 25;
    int igrid6;
    if (grid6[4] == ' ' && grid6[5] == ' ') {
      igrid6 = k1 + k2 + k3 + k4 + 24 * 25 + 24;
    } else {
      int k5 = (grid6[4] - 'A') * 25;
      int k6 = (grid6[5] - 'A');
      igrid6 = k1 + k2 + k3 + k4 + k5 + k6;
    }
    wbits(c77, 0, 22, n22);
    wbits(c77, 22, 25, igrid6);
    wbits(c77, 47, 3, 2);
    wbits(c77, 50, 21, 0);
    wbits(c77, 71, 3, *n3);
    wbits(c77, 74, 3, *i3);
  }
}

static bool ends_with_at4(const char *w, const char *suffix)   /* index(w//' ', suffix//' ') >= 4 */
{
  char t[15];
  memcpy(t, w, 13);
  t[13] = ' ';
  t[14] = '\0';
  char s[8];
  snprintf(s, sizeof s, "%s ", suffix);
  return findex(t, 14, s) >= 4;
}

static void pack77_1(ftx_pack77_ctx *ctx, int nwords, word13 w[19], int *i3, int *n3, char *c77)
{
  /* Type 1 (standard, "/R") and type 2 ("/P"): "WA9XYZ/R KA1ABC/R R FN42"  28 1 28 1 1 15 */
  char bcall_1[7], bcall_2[7], c13[14];
  bool ok1, ok2;
  int ir = 0, irpt = 0, igrid4;
  if (nwords < 2 || nwords > 4) return;
  chkcall(w[0], bcall_1, &ok1);
  chkcall(w[1], bcall_2, &ok2);
  if (fprefix(w[0], 13, "DE ") || fprefix(w[0], 13, "CQ_") || fprefix(w[0], 13, "CQ ") ||
      fprefix(w[0], 13, "QRZ ")) ok1 = true;
  if (w[0][0] == '<' && findex(w[0], 13, ">") >= 5) ok1 = true;
  if (w[1][0] == '<' && findex(w[1], 13, ">") >= 5) ok2 = true;
  if (!ok1 || !ok2) return;
  if (w[0][0] == '<' && findex(w[1], 13, "/") > 0) return;
  if (w[1][0] == '<' && findex(w[0], 13, "/") > 0) return;
  if (nwords == 2 && (!ok2 || findex(w[1], 13, "/") >= 2)) return;
  if (nwords != 2) {
    const char *wn = w[nwords - 1];
    char c1 = wn[0];
    bool rplus = wn[0] == 'R' && wn[1] == '+', rminus = wn[0] == 'R' && wn[1] == '-';
    if (!is_grid4(wn) && c1 != '+' && c1 != '-' && !rplus && !rminus &&
        !feq(wn, 13, "RRR") && !feq(wn, 13, "RR73") && !feq(wn, 13, "73")) return;
    if (c1 == '+' || c1 == '-') {
      ir = 0;
      if (fread_int(wn, 13, &irpt) != 1) return;
      if (irpt >= -50 && irpt <= -31) irpt += 101;
      irpt += 35;
    } else if (rplus || rminus) {
      ir = 1;
      if (fread_int(wn + 1, 12, &irpt) != 1) return;
      if (irpt >= -50 && irpt <= -31) irpt += 101;
      irpt += 35;
    } else if (feq(wn, 13, "RRR")) {
      ir = 0; irpt = 2;
    } else if (feq(wn, 13, "RR73")) {
      ir = 0; irpt = 3;
    } else if (feq(wn, 13, "73")) {
      ir = 0; irpt = 4;
    }
  }
  bool i1psuffix = ends_with_at4(w[0], "/P"), i2psuffix = ends_with_at4(w[1], "/P");
  if (nwords == 2 || nwords == 3 || (nwords == 4 && fprefix(w[2], 13, "R "))) {
    *n3 = 0;
    *i3 = 1;                                           /* standard message, possibly "/R" */
    if (i1psuffix || i2psuffix) *i3 = 2;               /* "/P" form */
  }
  fsetn(c13, 13, bcall_1, 6);
  if (fprefix(c13, 13, "CQ_") || w[0][0] == '<') memcpy(c13, w[0], 14);
  int n28a, n28b;
  pack28(ctx, c13, &n28a);
  fsetn(c13, 13, bcall_2, 6);
  if (w[1][0] == '<') memcpy(c13, w[1], 14);
  pack28(ctx, c13, &n28b);
  int ipa = (i1psuffix || ends_with_at4(w[0], "/R")) ? 1 : 0;
  int ipb = (i2psuffix || ends_with_at4(w[1], "/R")) ? 1 : 0;

  char grid4[5];
  fsetn(grid4, 4, w[nwords - 1], 4);
  if (is_grid4(grid4)) {
    ir = 0;
    if (feq(w[2], 13, "R")) ir = 1;
    igrid4 = grid4_to_int(grid4);
  } else {
    igrid4 = MAXGRID4 + irpt;
  }
  if (nwords == 2) {
    ir = 0;
    irpt = 1;
    igrid4 = MAXGRID4 + irpt;
  }
  wbits(c77, 0, 28, n28a);
  wbits(c77, 28, 1, ipa);
  wbits(c77, 29, 28, n28b);
  wbits(c77, 57, 1, ipb);
  wbits(c77, 58, 1, ir);
  wbits(c77, 59, 15, igrid4);
  wbits(c77, 74, 3, *i3);
}

static void pack77_3(ftx_pack77_ctx *ctx, int nwords, word13 w[19], int *i3, int *n3, char *c77)
{
  /* Type 3, ARRL RTTY Roundup: "TU; W9XYZ K1ABC R 579 MA"  1 28 28 1 3 13 */
  char bcall_1[7], bcall_2[7];
  bool ok1, ok2;
  if (w[0][0] == '<' && w[1][0] == '<') return;
  if (nwords == 4 || nwords == 5 || nwords == 6) {
    int i1 = feq(w[0], 13, "TU;") ? 2 : 1;
    chkcall(w[i1 - 1], bcall_1, &ok1);
    chkcall(w[i1], bcall_2, &ok2);
    if (!ok1 || !ok2) return;
    char crpt[4];
    fsetn(crpt, 3, w[nwords - 2], 3);
    if (findex(crpt, 3, "-") >= 1 || findex(crpt, 3, "+") >= 1) return;
    int nserial = 0;                                   /* (uninitialised in the Fortran unless 5x9) */
    if (crpt[0] == '5' && crpt[1] >= '2' && crpt[1] <= '9' && crpt[2] == '9') {
      nserial = 0;
      fread_int(w[nwords - 1], 13, &nserial);          /* err → unchanged */
    }
    char mult[4] = "   ";
    int imult = -1;
    for (int i = 0; i < NUSCAN; i++) {
      if (feq(w[nwords - 1], 13, CMULT[i])) {
        imult = i + 1;
        fset(mult, 3, CMULT[i]);
        break;
      }
    }
    int nexch = 0;
    if (nserial > 0) nexch = nserial;
    if (imult > 0) nexch = 8000 + imult;
    if (!feq(mult, 3, "") || nserial > 0) {
      *i3 = 3;
      *n3 = 0;
      int itu = feq(w[0], 13, "TU;") ? 1 : 0;
      int n28a, n28b;
      pack28(ctx, w[itu], &n28a);
      pack28(ctx, w[1 + itu], &n28b);
      int ir = fprefix(w[2 + itu], 13, "R ") ? 1 : 0;
      int irpt = 0;
      if (fread_int(w[2 + itu + ir], 13, &irpt) != 1) return;   /* i3 already 3, c77 untouched */
      irpt = (irpt - 509) / 10 - 2;
      if (irpt < 0) irpt = 0;
      if (irpt > 7) irpt = 7;
      wbits(c77, 0, 1, itu);
      wbits(c77, 1, 28, n28a);
      wbits(c77, 29, 28, n28b);
      wbits(c77, 57, 1, ir);
      wbits(c77, 58, 3, irpt);
      wbits(c77, 61, 13, nexch);
      wbits(c77, 74, 3, *i3);
    }
  }
}

static void pack77_4(ftx_pack77_ctx *ctx, int nwords, word13 w[19], int *i3, int *n3, char *c77)
{
  /* Type 4, one nonstandard call and one hashed call: "<WA9XYZ> PJ4/KA1ABC RR73"  12 58 1 2 1 */
  char call_1[14], call_2[14], bcall_1[7], bcall_2[7], c11[12];
  bool ok1, ok2;
  int iflip = 0;
  *i3 = -1;
  if (nwords == 2 || nwords == 3) {
    memcpy(call_1, w[0], 14);
    if (call_1[0] == '<') fsub(call_1, 13, w[0], 2, ftrim(w[0], 13) - 1);
    memcpy(call_2, w[1], 14);
    if (call_2[0] == '<') fsub(call_2, 13, w[1], 2, ftrim(w[1], 13) - 1);
    chkcall(call_1, bcall_1, &ok1);
    chkcall(call_2, bcall_2, &ok2);
    if (feq(call_1, 13, bcall_1) && feq(call_2, 13, bcall_2) && ok1 && ok2) return;
    int icq = 0;
    if (feq(w[0], 13, "CQ") || (ok1 && ok2)) {
      if (feq(w[0], 13, "CQ") && ftrim(w[1], 13) <= 4) return;
      *i3 = 4;
      *n3 = 0;
      if (feq(w[0], 13, "CQ")) icq = 1;
    }
    int n10 = 0, n12 = 0, n22 = 0;                     /* (n12, c11 uninitialised in the Fortran */
    fpad(c11, 0, 11);                                  /*  when no branch below is taken)        */
    if (icq == 1) {
      iflip = 0;
      n12 = 0;
      fsetn(c11, 11, call_2, 11);
      fadjustr(c11, 11);
      save_hash_call(ctx, w[1], &n10, &n12, &n22);
    } else if (w[0][0] == '<') {
      iflip = 0;
      *i3 = 4;
      save_hash_call(ctx, w[0], &n10, &n12, &n22);
      fsetn(c11, 11, call_2, 11);
      fadjustr(c11, 11);
    } else if (w[1][0] == '<') {
      iflip = 1;
      *i3 = 4;
      save_hash_call(ctx, w[1], &n10, &n12, &n22);
      fsetn(c11, 11, call_1, 11);
      fadjustr(c11, 11);
    }
    int64_t n58 = 0;
    for (int i = 0; i < 11; i++) n58 = n58 * 38 + cidx(C38, c11[i]) - 1;
    int nrpt = 0;
    if (feq(w[2], 13, "RRR")) nrpt = 1;
    if (feq(w[2], 13, "RR73")) nrpt = 2;
    if (feq(w[2], 13, "73")) nrpt = 3;
    if (icq == 1) {
      iflip = 0;
      nrpt = 0;
    }
    wbits(c77, 0, 12, n12);
    wbits(c77, 12, 58, n58);
    wbits(c77, 70, 1, iflip);
    wbits(c77, 71, 2, nrpt);
    wbits(c77, 73, 1, icq);
    wbits(c77, 74, 3, *i3);
    for (int i = 0; i < 77; i++)
      if (c77[i] == '*') c77[i] = '0';                 /* "clean up any illegal chars" */
  }
}

static void pack77_5(ftx_pack77_ctx *ctx, int nwords, word13 w[19], int *i3, int *n3, char *c77)
{
  /* Type 5, EU VHF contest: "<PA3XYZ> <G4ABC/P> R 590003 IO91NP"  h12 h22 r1 s3 S11 g25 */
  char c13[14], grid6[7];
  if (nwords < 4 || nwords > 5) return;
  if (w[0][0] != '<' || w[1][0] != '<') return;       /* both calls hashed */
  int nx = -1;
  fread_int(w[nwords - 2], 13, &nx);
  if (nx < 520001 || nx > 594095) return;
  fsetn(grid6, 6, w[nwords - 1], 6);
  if (!is_grid6_strict(grid6)) return;

  *i3 = 5;
  *n3 = 0;
  int n10 = 0, n12 = 0, n22 = 0, n10a = 0, n12a = 0;
  save_hash_call(ctx, w[0], &n10, &n12, &n22);
  int i2 = findex(w[0], 13, ">");
  fsub(c13, 13, w[0], 2, i2 - 1);
  n12 = ftx_ihashcall(c13, 12);
  save_hash_call(ctx, w[1], &n10a, &n12a, &n22);
  i2 = findex(w[1], 13, ">");
  fsub(c13, 13, w[1], 2, i2 - 1);
  n22 = ftx_ihashcall(c13, 22);

  int ir = fprefix(w[2], 13, "R ") ? 1 : 0;
  int irpt = nx / 10000 - 52;
  int iserial = nx % 10000;
  if (iserial > 2047) iserial = 2047;
  int igrid6 = (grid6[0] - 'A') * 18 * 10 * 10 * 24 * 24 + (grid6[1] - 'A') * 10 * 10 * 24 * 24 +
               (grid6[2] - '0') * 10 * 24 * 24 + (grid6[3] - '0') * 24 * 24 +
               (grid6[4] - 'A') * 24 + (grid6[5] - 'A');
  wbits(c77, 0, 12, n12);
  wbits(c77, 12, 22, n22);
  wbits(c77, 34, 1, ir);
  wbits(c77, 35, 3, irpt);
  wbits(c77, 38, 11, iserial);
  wbits(c77, 49, 25, igrid6);
  wbits(c77, 74, 3, *i3);
}

/* ------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
void ftx_pack77_init(ftx_pack77_ctx *ctx)
{
  memset(ctx, 0, sizeof *ctx);
  for (int i = 0; i < 1024; i++) fpad(ctx->calls10[i], 0, 13);
  for (int i = 0; i < 4096; i++) fpad(ctx->calls12[i], 0, 13);
  for (int i = 0; i < FTX_MAXHASH; i++) {
    fpad(ctx->calls22[i], 0, 13);
    ctx->ihash22[i] = -1;
  }
  for (int i = 0; i < FTX_MAXRECENT; i++) fpad(ctx->recent_calls[i], 0, 13);
  fpad(ctx->mycall13, 0, 13);
  fpad(ctx->dxcall13, 0, 13);
  fpad(ctx->dxbase, 0, 6);
  fpad(ctx->mycall13_0, 0, 13);
  fpad(ctx->dxcall13_0, 0, 13);
}

void ftx_pack77_set_mycall(ftx_pack77_ctx *ctx, const char *call) { fset(ctx->mycall13, 13, call); }
void ftx_pack77_set_dxcall(ftx_pack77_ctx *ctx, const char *call) { fset(ctx->dxcall13, 13, call); }
void ftx_pack77_set_dxbase(ftx_pack77_ctx *ctx, const char *base) { fset(ctx->dxbase, 6, base); }

void ftx_fmtmsg(char *msg)
{
  int len = (int)strlen(msg);
  int iz = len;
  for (int i = 0; i < len; i++) {                      /* upper case; iz = last nonblank */
    if (msg[i] >= 'a' && msg[i] <= 'z') msg[i] = (char)(msg[i] - 'a' + 'A');
    if (msg[i] != ' ') iz = i + 1;
  }
  for (int iter = 0; iter < 37; iter++) {              /* collapse multiple blanks into one */
    int ib2 = findex(msg, iz, "  ");
    if (ib2 < 1) break;
    memmove(msg + ib2, msg + ib2 + 1, (size_t)(len - ib2 - 1));
    len--;
    msg[len] = '\0';
    iz--;
  }
}

void ftx_pack77(ftx_pack77_ctx *ctx, const char *msg0, int *i3, int *n3, char c77[78])
{
  char msg[38];
  word13 w[19];
  int nw[19];
  int nwords = 0;
  fsetn(msg, 37, msg0, (int)strlen(msg0));
  c77[77] = '\0';

  if (fprefix(msg, 37, "$DX")) {                       /* "$DX ..." → base of the DX call */
    int i1 = findex(msg, 37, " ");
    char b[64], t[8];
    ftrimcpy(t, ctx->dxbase, 6);
    snprintf(b, sizeof b, "%s %.*s", t, 37 - i1, msg + i1);
    fset(msg, 37, b);
  }

  int i3_hint = *i3, n3_hint = *n3;
  *i3 = -1;
  *n3 = -1;
  if (i3_hint == 0 && n3_hint == 5) goto telemetry;

  split77(msg, &nwords, nw, w);
  if (fprefix(msg, 37, "CQ ") || fprefix(msg, 37, "DE ") || fprefix(msg, 37, "QRZ ")) goto l100;

  pack77_01(ctx, nwords, w, i3, n3, c77);              /* 0.1 DXpedition */
  if (*i3 >= 0 || *n3 >= 1) return;
  pack77_03(ctx, nwords, w, i3, n3, c77);              /* 0.3, 0.4 Field Day */
  if (*i3 >= 0) return;
  if (nwords >= 2) goto l100;

telemetry:
  {                                                    /* 0.5 telemetry, 18 hex digits */
    int i0 = findex(msg, 37, " ");
    char c18[19];
    fsub(c18, 18, msg, 1, i0 - 1);
    fadjustr(c18, 18);
    int ntel[3] = { -99, -99, -99 };
    bool err = false;
    for (int k = 0; k < 3 && !err; k++)
      if (!fread_hex6(c18 + 6 * k, &ntel[k])) err = true;
    if (!err && ntel[0] >= (1 << 23)) goto freetext;
    if (ntel[0] >= 0 && ntel[1] >= 0 && ntel[2] >= 0) {
      *i3 = 0;
      *n3 = 5;
      wbits(c77, 0, 23, ntel[0]);
      wbits(c77, 23, 24, ntel[1]);
      wbits(c77, 47, 24, ntel[2]);
      wbits(c77, 71, 3, *n3);
      wbits(c77, 74, 3, *i3);
      return;
    }
  }

l100:
  pack77_06(ctx, nwords, w, i3, n3, c77, i3_hint, n3_hint);   /* 0.6 WSPR */
  if (*i3 >= 0) return;
  pack77_1(ctx, nwords, w, i3, n3, c77);               /* 1 standard, 2 "/P" */
  if (*i3 >= 0) return;
  pack77_3(ctx, nwords, w, i3, n3, c77);               /* 3 ARRL RTTY */
  if (*i3 >= 0) return;
  pack77_4(ctx, nwords, w, i3, n3, c77);               /* 4 nonstandard + hashed call */
  if (*i3 >= 0) return;
  pack77_5(ctx, nwords, w, i3, n3, c77);               /* 5 EU VHF */
  if (*i3 >= 0) return;

freetext:                                              /* 0.0 free text */
  *i3 = 0;
  *n3 = 0;
  fpad(msg, 13, 37);
  packtext77(msg, c77);
  wbits(c77, 71, 3, *n3);
  wbits(c77, 74, 3, *i3);
}

bool ftx_unpack77(ftx_pack77_ctx *ctx, const char *c77, int nrx, char msg_out[38])
{
  char msg[38], call_1[14], call_2[14], call_3[14], call_1a[14];
  char crpt[8], cntx[4], grid4[5] = "    ", grid6[7], cexch[8], cserial[8], b[128], t1[14], t2[14], t3[14];
  bool unpk28_success, unpk77_success, unpkg4_success;
  int ndum10, ndum12, ndum22;

  if (!feq(ctx->mycall13, 13, ctx->mycall13_0)) {
    if (ftrim(ctx->mycall13, 13) > 2) {
      ctx->mycall13_set = true;
      memcpy(ctx->mycall13_0, ctx->mycall13, 14);
      save_hash_call(ctx, ctx->mycall13, &ctx->hashmy10, &ctx->hashmy12, &ctx->hashmy22);
    } else {
      ctx->mycall13_set = false;
    }
  }
  if (!feq(ctx->dxcall13, 13, ctx->dxcall13_0)) {
    if (ftrim(ctx->dxcall13, 13) > 2) {
      ctx->dxcall13_set = true;
      memcpy(ctx->dxcall13_0, ctx->dxcall13, 14);
      ctx->hashdx10 = ftx_ihashcall(ctx->dxcall13, 10);
      ctx->hashdx12 = ftx_ihashcall(ctx->dxcall13, 12);
      ctx->hashdx22 = ftx_ihashcall(ctx->dxcall13, 22);
    }
  }
  unpk77_success = true;

  for (int i = 0; i < 77; i++) {                       /* check for bad data */
    if (c77[i] != '0' && c77[i] != '1') {
      strcpy(msg_out, "failed unpack");
      return false;
    }
  }
  int n3 = (int)rbits(c77, 71, 3), i3 = (int)rbits(c77, 74, 3);
  fpad(msg, 0, 37);

  if (i3 == 0 && n3 == 0) {
    /* 0.0  free text */
    unpacktext77(c77, msg);
    fpad(msg, 13, 37);
    fadjustl(msg, 37);
    if (msg[0] == ' ') {
      unpk77_success = false;
      goto out;
    }
  } else if (i3 == 0 && n3 == 1) {
    /* 0.1  K1ABC RR73; W9XYZ <KH1/KH7Z> -11   28 28 10 5 */
    int n28a = (int)rbits(c77, 0, 28), n28b = (int)rbits(c77, 28, 28);
    int n10 = (int)rbits(c77, 56, 10), n5 = (int)rbits(c77, 66, 5);
    ctx->n28a = n28a;
    ctx->n28b = n28b;
    int irpt = 2 * n5 - 30;
    fmt_int(crpt, 3, 2, irpt);
    if (irpt >= 0) crpt[0] = '+';
    unpack28(ctx, n28a, call_1, &unpk28_success);
    if (!unpk28_success || n28a <= 2) unpk77_success = false;
    unpack28(ctx, n28b, call_2, &unpk28_success);
    if (!unpk28_success || n28b <= 2) unpk77_success = false;
    hash10(ctx, n10, call_3);
    if (nrx == 1 && ctx->dxcall13_set && ftrim(ctx->dxcall13, 13) >= 3 && ctx->hashdx10 == n10)
      bracket(call_3, ctx->dxcall13, 13);
    if (nrx == 0 && ctx->mycall13_set && n10 == ctx->hashmy10)
      bracket(call_3, ctx->mycall13, 13);
    ftrimcpy(t1, call_1, 13); ftrimcpy(t2, call_2, 13); ftrimcpy(t3, call_3, 13);
    snprintf(b, sizeof b, "%s RR73; %s %s %s", t1, t2, t3, crpt);
    fset(msg, 37, b);
  } else if (i3 == 0 && n3 == 2) {
    unpk77_success = false;
  } else if (i3 == 0 && (n3 == 3 || n3 == 4)) {
    /* 0.3 / 0.4  WA9XYZ KA1ABC R 16A EMA   28 28 1 4 3 7 */
    int n28a = (int)rbits(c77, 0, 28), n28b = (int)rbits(c77, 28, 28);
    int ir = (int)rbits(c77, 56, 1), intx = (int)rbits(c77, 57, 4);
    int nclass = (int)rbits(c77, 61, 3), isec = (int)rbits(c77, 64, 7);
    ctx->n28a = n28a;
    ctx->n28b = n28b;
    if (isec > NSEC || isec < 1) {
      unpk77_success = false;
      isec = 1;
    }
    unpack28(ctx, n28a, call_1, &unpk28_success);
    if (!unpk28_success || n28a <= 2) unpk77_success = false;
    unpack28(ctx, n28b, call_2, &unpk28_success);
    if (!unpk28_success || n28b <= 2) unpk77_success = false;
    int ntx = intx + 1;
    if (n3 == 4) ntx += 16;
    fmt_int(cntx, 2, 0, ntx);
    cntx[2] = (char)('A' + nclass);
    cntx[3] = '\0';
    char csec[4];
    fset(csec, 3, CSEC[isec - 1]);
    ftrimcpy(t1, call_1, 13); ftrimcpy(t2, call_2, 13);
    if (ir == 0 && ntx < 10) snprintf(b, sizeof b, "%s %s%s %s", t1, t2, cntx, csec);
    if (ir == 1 && ntx < 10) snprintf(b, sizeof b, "%s %s R%s %s", t1, t2, cntx, csec);
    if (ir == 0 && ntx >= 10) snprintf(b, sizeof b, "%s %s %s %s", t1, t2, cntx, csec);
    if (ir == 1 && ntx >= 10) snprintf(b, sizeof b, "%s %s R %s %s", t1, t2, cntx, csec);
    fset(msg, 37, b);
  } else if (i3 == 0 && n3 == 5) {
    /* 0.5  telemetry, 18 hex digits */
    int ntel0 = (int)rbits(c77, 0, 23), ntel1 = (int)rbits(c77, 23, 24), ntel2 = (int)rbits(c77, 47, 24);
    snprintf(b, sizeof b, "%06X%06X%06X", ntel0, ntel1, ntel2);
    fset(msg, 37, b);
    for (int i = 0; i < 18; i++) {                     /* strip leading zeros */
      if (msg[i] != '0') break;
      msg[i] = ' ';
    }
    fadjustl(msg, 37);
  } else if (i3 == 0 && n3 == 6) {
    /* 0.6  WSPR types 1, 2, 3 */
    int j48 = c77[47] == '1', j49 = c77[48] == '1', j50 = c77[49] == '1';
    int itype;
    if (j50 == 1) itype = 2;
    else if (j49 == 0) itype = 1;
    else if (j48 == 0) itype = 3;
    else {
      itype = -1;
      unpk77_success = false;
    }
    if (itype == 1) {
      int n28 = (int)rbits(c77, 0, 28), igrid4 = (int)rbits(c77, 28, 15), idbm = (int)rbits(c77, 43, 5);
      idbm = (int)lroundf((float)idbm * 10.0f / 3.0f);
      if (idbm < 0 || idbm > 60) unpk77_success = false;
      unpack28(ctx, n28, call_1, &unpk28_success);
      if (!unpk28_success) unpk77_success = false;
      to_grid4(igrid4, grid4, &unpkg4_success);
      if (!unpkg4_success) unpk77_success = false;
      fmt_int(crpt, 3, 0, idbm);
      ftrimcpy(t1, call_1, 13);
      char r[4];
      {
        int k = 0;
        while (crpt[k] == ' ') k++;
        strcpy(r, crpt + k);
      }
      snprintf(b, sizeof b, "%s %.4s %s", t1, grid4, r);
      fset(msg, 37, b);
      if (unpk77_success) save_hash_call(ctx, call_1, &ndum10, &ndum12, &ndum22);
    } else if (itype == 2) {
      int n28 = (int)rbits(c77, 0, 28), npfx = (int)rbits(c77, 28, 16), idbm = (int)rbits(c77, 44, 5);
      idbm = (int)lroundf((float)idbm * 10.0f / 3.0f);
      if (idbm < 0 || idbm > 60) unpk77_success = false;
      unpack28(ctx, n28, call_1, &unpk28_success);
      if (!unpk28_success) unpk77_success = false;
      fmt_int(crpt, 3, 0, idbm);
      char r[4], cpfx[4] = "   ", p[4];
      {
        int k = 0;
        while (crpt[k] == ' ') k++;
        strcpy(r, crpt + k);
      }
      ftrimcpy(t1, call_1, 13);
      if (npfx < NZZZ) {                               /* prefix */
        for (int i = 3; i >= 1; i--) {
          int j = npfx % 36 + 1;
          cpfx[i - 1] = A2[j - 1];
          npfx /= 36;
          if (npfx == 0) break;
        }
        fadjustl(cpfx, 3);
        ftrimcpy(p, cpfx, 3);
        snprintf(b, sizeof b, "%s/%s %s", p, t1, r);
        fset(msg, 37, b);
        snprintf(b, sizeof b, "%s/%s", p, t1);
        fset(call_1a, 13, b);
        save_hash_call(ctx, call_1a, &ndum10, &ndum12, &ndum22);
      } else {                                         /* suffix */
        npfx -= NZZZ;
        if (npfx <= 35) {
          cpfx[0] = A2[npfx];
        } else if (npfx > 35 && npfx <= 1295) {
          cpfx[0] = A2[npfx / 36];
          cpfx[1] = A2[npfx % 36];
        } else if (npfx > 1295 && npfx <= 12959) {
          cpfx[0] = A2[npfx / 360];
          cpfx[1] = A2[(npfx / 10) % 36];
          cpfx[2] = A2[npfx % 10];
        } else {
          unpk77_success = false;
          goto out;
        }
        fadjustl(cpfx, 3);
        ftrimcpy(p, cpfx, 3);
        snprintf(b, sizeof b, "%s/%s %s", t1, p, r);
        fset(msg, 37, b);
        snprintf(b, sizeof b, "%s/%s", t1, p);
        fset(call_1a, 13, b);
        save_hash_call(ctx, call_1a, &ndum10, &ndum12, &ndum22);
      }
    } else if (itype == 3) {
      int n22 = (int)rbits(c77, 0, 22), igrid6 = (int)rbits(c77, 22, 25);
      int n28 = n22 + 2063592;
      unpack28(ctx, n28, call_1, &unpk28_success);
      if (!unpk28_success) unpk77_success = false;
      to_grid(igrid6, grid6, &unpkg4_success);
      if (!unpkg4_success) unpk77_success = false;
      ftrimcpy(t1, call_1, 13);
      snprintf(b, sizeof b, "%s %.6s", t1, grid6);
      fset(msg, 37, b);
    }
  } else if (i3 == 0 && n3 > 6) {
    unpk77_success = false;
  } else if (i3 == 1 || i3 == 2) {
    /* Type 1 (standard) or type 2 ("/P"):  28 1 28 1 1 15 */
    int n28a = (int)rbits(c77, 0, 28), ipa = (int)rbits(c77, 28, 1);
    int n28b = (int)rbits(c77, 29, 28), ipb = (int)rbits(c77, 57, 1);
    int ir = (int)rbits(c77, 58, 1), igrid4 = (int)rbits(c77, 59, 15);
    ctx->n28a = n28a;
    ctx->n28b = n28b;
    unpack28(ctx, n28a, call_1, &unpk28_success);
    if (nrx == 1 && ctx->mycall13_set && ctx->hashmy22 == n28a - 2063592) {
      bracket(call_1, ctx->mycall13, 13);
      unpk28_success = true;
    }
    if (!unpk28_success) unpk77_success = false;
    unpack28(ctx, n28b, call_2, &unpk28_success);
    if (!unpk28_success) unpk77_success = false;
    if (fprefix(call_1, 13, "CQ_")) call_1[2] = ' ';
    if (findex(call_1, 13, "<") <= 0) {
      int i = findex(call_1, 13, " ");
      if (i >= 4 && ipa == 1 && i3 == 1) { call_1[i - 1] = '/'; if (i < 13) call_1[i] = 'R'; }
      if (i >= 4 && ipa == 1 && i3 == 2) { call_1[i - 1] = '/'; if (i < 13) call_1[i] = 'P'; }
      if (i >= 4) add_call_to_recent_calls(ctx, call_1);
    }
    if (findex(call_2, 13, "<") <= 0) {
      int i = findex(call_2, 13, " ");
      if (i >= 4 && ipb == 1 && i3 == 1) { call_2[i - 1] = '/'; if (i < 13) call_2[i] = 'R'; }
      if (i >= 4 && ipb == 1 && i3 == 2) { call_2[i - 1] = '/'; if (i < 13) call_2[i] = 'P'; }
      if (i >= 4) {
        add_call_to_recent_calls(ctx, call_2);
        save_hash_call(ctx, call_2, &ndum10, &ndum12, &ndum22);   /* only hash the "from" call */
      }
    }
    ftrimcpy(t1, call_1, 13); ftrimcpy(t2, call_2, 13);
    if (igrid4 <= MAXGRID4) {
      to_grid4(igrid4, grid4, &unpkg4_success);
      if (!unpkg4_success) unpk77_success = false;
      if (ir == 0) snprintf(b, sizeof b, "%s %s %.4s", t1, t2, grid4);
      if (ir == 1) snprintf(b, sizeof b, "%s %s R %.4s", t1, t2, grid4);
      fset(msg, 37, b);
      if (fprefix(msg, 37, "CQ ") && ir == 1) unpk77_success = false;
    } else {
      int irpt = igrid4 - MAXGRID4;
      if (irpt == 1) snprintf(b, sizeof b, "%s %s", t1, t2);
      if (irpt == 2) snprintf(b, sizeof b, "%s %s RRR", t1, t2);
      if (irpt == 3) snprintf(b, sizeof b, "%s %s RR73", t1, t2);
      if (irpt == 4) snprintf(b, sizeof b, "%s %s 73", t1, t2);
      if (irpt >= 5) {
        int isnr = irpt - 35;
        if (isnr > 50) isnr -= 101;
        fmt_int(crpt, 3, 2, isnr);
        if (crpt[0] == ' ') crpt[0] = '+';
        if (ir == 0) snprintf(b, sizeof b, "%s %s %s", t1, t2, crpt);
        if (ir == 1) snprintf(b, sizeof b, "%s %s R%s", t1, t2, crpt);
      }
      if (irpt >= 1) fset(msg, 37, b);                 /* irpt == 0: msg stays blank */
      if (fprefix(msg, 37, "CQ ") && irpt >= 2) unpk77_success = false;
    }
  } else if (i3 == 3) {
    /* Type 3, ARRL RTTY Roundup:  1 28 28 1 3 13 */
    int itu = (int)rbits(c77, 0, 1), n28a = (int)rbits(c77, 1, 28), n28b = (int)rbits(c77, 29, 28);
    int ir = (int)rbits(c77, 57, 1), irpt = (int)rbits(c77, 58, 3), nexch = (int)rbits(c77, 61, 13);
    ctx->n28a = n28a;
    ctx->n28b = n28b;
    snprintf(crpt, sizeof crpt, "5%d9", irpt + 2);
    unpack28(ctx, n28a, call_1, &unpk28_success);
    if (!unpk28_success) unpk77_success = false;
    unpack28(ctx, n28b, call_2, &unpk28_success);
    if (!unpk28_success) unpk77_success = false;
    int imult = 0, nserial = 0;
    if (nexch > 8000) imult = nexch - 8000;
    if (nexch < 8000) nserial = nexch;
    ftrimcpy(t1, call_1, 13); ftrimcpy(t2, call_2, 13);
    if (imult >= 1 && imult <= NUSCAN) {
      char cm[4];
      fset(cm, 3, CMULT[imult - 1]);
      if (itu == 0 && ir == 0) snprintf(b, sizeof b, "%s %s %s %s", t1, t2, crpt, cm);
      if (itu == 1 && ir == 0) snprintf(b, sizeof b, "TU; %s %s %s %s", t1, t2, crpt, cm);
      if (itu == 0 && ir == 1) snprintf(b, sizeof b, "%s %s R %s %s", t1, t2, crpt, cm);
      if (itu == 1 && ir == 1) snprintf(b, sizeof b, "TU; %s %s R %s %s", t1, t2, crpt, cm);
      fset(msg, 37, b);
    } else if (nserial >= 1 && nserial <= 7999) {
      fmt_int(cserial, 4, 4, nserial);
      if (itu == 0 && ir == 0) snprintf(b, sizeof b, "%s %s %s %s", t1, t2, crpt, cserial);
      if (itu == 1 && ir == 0) snprintf(b, sizeof b, "TU; %s %s %s %s", t1, t2, crpt, cserial);
      if (itu == 0 && ir == 1) snprintf(b, sizeof b, "%s %s R %s %s", t1, t2, crpt, cserial);
      if (itu == 1 && ir == 1) snprintf(b, sizeof b, "TU; %s %s R %s %s", t1, t2, crpt, cserial);
      fset(msg, 37, b);
    }
  } else if (i3 == 4) {
    /* Type 4, nonstandard + hashed call:  12 58 1 2 1 */
    int n12 = (int)rbits(c77, 0, 12);
    int64_t n58 = rbits(c77, 12, 58);
    int iflip = (int)rbits(c77, 70, 1), nrpt = (int)rbits(c77, 71, 2), icq = (int)rbits(c77, 73, 1);
    char c11[12];
    for (int i = 11; i >= 1; i--) {
      int j = (int)(n58 % 38) + 1;
      c11[i - 1] = C38[j - 1];
      n58 /= 38;
    }
    c11[11] = '\0';
    hash12(ctx, n12, call_3);
    if (iflip == 0) {                                  /* 12-bit hash for the TO call */
      memcpy(call_1, call_3, 14);
      fset(call_2, 13, c11);
      fadjustl(call_2, 13);
      add_call_to_recent_calls(ctx, call_2);
      save_hash_call(ctx, call_2, &ndum10, &ndum12, &ndum22);
      if (nrx == 1 && ctx->dxcall13_set && ctx->mycall13_set && feq(call_2, 13, ctx->dxcall13) &&
          n12 == ctx->hashmy12)
        bracket(call_1, ctx->mycall13, 13);
      if (nrx == 1 && ctx->mycall13_set && findex(call_1, 13, "<...>") > 0 && n12 == ctx->hashmy12)
        bracket(call_1, ctx->mycall13, 13);
    } else {                                           /* 12-bit hash for the DE call */
      fset(call_1, 13, c11);
      fadjustl(call_1, 13);
      memcpy(call_2, call_3, 14);
      add_call_to_recent_calls(ctx, call_1);
      if (nrx == 0 && ctx->mycall13_set && n12 == ctx->hashmy12) bracket(call_2, ctx->mycall13, 13);
    }
    ftrimcpy(t1, call_1, 13); ftrimcpy(t2, call_2, 13);
    if (icq == 0) {
      if (nrpt == 0) snprintf(b, sizeof b, "%s %s", t1, t2);
      if (nrpt == 1) snprintf(b, sizeof b, "%s %s RRR", t1, t2);
      if (nrpt == 2) snprintf(b, sizeof b, "%s %s RR73", t1, t2);
      if (nrpt == 3) snprintf(b, sizeof b, "%s %s 73", t1, t2);
    } else {
      snprintf(b, sizeof b, "CQ %s", t2);
    }
    fset(msg, 37, b);
  } else if (i3 == 5) {
    /* Type 5, EU VHF contest:  h12 h22 r1 s3 S11 g25 */
    int n12 = (int)rbits(c77, 0, 12), n22 = (int)rbits(c77, 12, 22), ir = (int)rbits(c77, 34, 1);
    int irpt = (int)rbits(c77, 35, 3), iserial = (int)rbits(c77, 38, 11), igrid6 = (int)rbits(c77, 49, 25);
    if (igrid6 < 0 || igrid6 > 18662399) {
      unpk77_success = false;
      goto out;
    }
    hash12(ctx, n12, call_1);
    if (n12 == ctx->hashmy12) bracket(call_1, ctx->mycall13, 13);
    hash22(ctx, n22, call_2);
    int nrs = 52 + irpt;
    fmt_int(cexch, 2, 0, nrs);
    fmt_int(cexch + 2, 4, 4, iserial);
    to_grid6(igrid6, grid6, &unpk77_success);
    ftrimcpy(t1, call_1, 13); ftrimcpy(t2, call_2, 13);
    if (ir == 0) snprintf(b, sizeof b, "%s %s %s %.6s", t1, t2, cexch, grid6);
    if (ir == 1) snprintf(b, sizeof b, "%s %s R %s %.6s", t1, t2, cexch, grid6);
    fset(msg, 37, b);
  } else if (i3 >= 6) {
    unpk77_success = false;                            /* i3 = 6, 7 not defined */
  }
  if (fprefix(msg, 37, "CQ <")) unpk77_success = false;

out:
  ftrimcpy(msg_out, msg, 37);
  return unpk77_success;
}

bool ftx_c77_to_bits(const char *c77, uint8_t bits[77])
{
  for (int i = 0; i < 77; i++) {
    if (c77[i] != '0' && c77[i] != '1') return false;
    bits[i] = c77[i] == '1';
  }
  return true;
}

void ftx_bits_to_c77(const uint8_t bits[77], char c77[78])
{
  for (int i = 0; i < 77; i++) c77[i] = bits[i] ? '1' : '0';
  c77[77] = '\0';
}

void ftx_pack77_save_hash_call(ftx_pack77_ctx *ctx, const char *c13, int *n10, int *n12, int *n22)
{
  save_hash_call(ctx, c13, n10, n12, n22);
}

void ftx_pack77_split77(char *msg, int *nwords, int nw[19], char w[19][14])
{
  split77(msg, nwords, nw, w);
}
