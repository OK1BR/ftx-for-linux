/* messages.c — see messages.h.  Copyright (C) 2026 Richard Fakenberg, OK1BR.
 * GPL-3.0-or-later. */
#include "messages.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* MainWindow::stdCall */
gboolean ftx_msg_std_call(const char *call)
{
  static GRegex *re;
  if (!re)
    re = g_regex_new("^\\s*([A-Z]{0,2}|[A-Z][0-9]|[0-9][A-Z])([0-9][A-Z]{0,3})(/R|/P)?\\s*$",
                     G_REGEX_CASELESS, 0, NULL);
  return call && g_regex_match(re, call, 0, NULL);
}

/* Radio::base_callsign */
void ftx_msg_base_call(const char *call, char *base, gsize n)
{
  const char *slash = strchr(call, '/');
  if (slash) {
    gsize left = (gsize)(slash - call), right = strlen(slash + 1);
    if (right >= left) g_strlcpy(base, slash + 1, n);
    else { g_strlcpy(base, call, n); if (left < n) base[left] = '\0'; }
  } else {
    g_strlcpy(base, call, n);
  }
  for (char *p = base; *p; p++) *p = (char)g_ascii_toupper(*p);
}

/* Radio::is_callsign — very loose: a letter next to a digit */
gboolean ftx_msg_is_callsign(const char *s)
{
  if (!s) return FALSE;
  for (const char *p = s; p[0] && p[1]; p++)
    if ((g_ascii_isdigit(p[0]) && g_ascii_isalpha(p[1])) || (g_ascii_isalpha(p[0]) && g_ascii_isdigit(p[1]))) return TRUE;
  return FALSE;
}

/* mainwindow.cpp grid_regexp: \A(?![Rr]{2}73)[A-Ra-r]{2}[0-9]{2}([A-Xa-x]{2}){0,1}\z */
gboolean ftx_msg_is_grid(const char *s)
{
  static GRegex *re;
  if (!re) re = g_regex_new("\\A(?![Rr]{2}73)[A-Ra-r]{2}[0-9]{2}([A-Xa-x]{2}){0,1}\\z", 0, 0, NULL);
  return s && g_regex_match(re, s, 0, NULL);
}

gboolean ftx_msg_is_standard(ftx_pack77_ctx *pc, const char *msg)
{
  int i3 = -1, n3 = -1;
  char c77[78];
  ftx_pack77(pc, msg, &i3, &n3, c77);
  return i3 > 0 || n3 > 0;
}

/* pack and unpack a message: the text exactly as it will decode */
static gboolean msgtype(ftx_pack77_ctx *pc, const char *text, char out[38])
{
  char buf[64];
  g_strlcpy(buf, text, sizeof buf);
  g_strchomp(buf);
  int i3 = -1, n3 = -1;
  char c77[78];
  ftx_pack77(pc, buf, &i3, &n3, c77);
  gboolean std = i3 > 0 || n3 > 0;
  if (!ftx_unpack77(pc, c77, 0, out)) { g_strlcpy(out, buf, 38); return FALSE; }
  return std;
}

/* MainWindow::genStdMsgs for FT8/FT4 (is77BitMode, SpecOp::NONE) */
void ftx_msg_gen_std(FtxStdMsgs *m, ftx_pack77_ctx *pc, const char *mycall, const char *mygrid,
                     const char *hiscall, int report, gboolean rr73)
{
  memset(m, 0, sizeof *m);
  char my[16], his[16], mybase[16], hisbase[16], grid4[8];
  g_strlcpy(my, mycall ? mycall : "", sizeof my);  g_strstrip(my);
  g_strlcpy(his, hiscall ? hiscall : "", sizeof his); g_strstrip(his);
  for (char *p = my; *p; p++) *p = (char)g_ascii_toupper(*p);
  for (char *p = his; *p; p++) *p = (char)g_ascii_toupper(*p);
  g_strlcpy(grid4, mygrid ? mygrid : "", sizeof grid4);
  grid4[4] = '\0';
  ftx_msg_base_call(my, mybase, sizeof mybase);
  ftx_msg_base_call(his, hisbase, sizeof hisbase);
  ftx_pack77_set_mycall(pc, my);
  ftx_pack77_set_dxcall(pc, his);
  ftx_pack77_set_dxbase(pc, hisbase);

  /* genCQMsg */
  if (my[0] && grid4[0]) {
    char t[64];
    if (ftx_msg_std_call(my)) snprintf(t, sizeof t, "CQ %s %s", my, grid4);
    else snprintf(t, sizeof t, "CQ %s", my);
    m->valid[5] = msgtype(pc, t, m->tx[5]);
  }
  if (!his[0]) return;

  const gboolean bMy = ftx_msg_std_call(my), bHis = ftx_msg_std_call(his);
  char t0[96], t0a[96], t0b[96], t[128], rpt[8];
  if (bHis && bMy) snprintf(t0, sizeof t0, "%s %s ", his, my);
  else snprintf(t0, sizeof t0, "%s %s ", hisbase, mybase);
  snprintf(t0a, sizeof t0a, "<%s> %s ", his, my);
  snprintf(t0b, sizeof t0b, "%s <%s> ", his, my);
  snprintf(rpt, sizeof rpt, "%+03d", report);

  /* Tx1 */
  snprintf(t, sizeof t, "%s%s", t0, grid4);
  if (!bMy) g_strlcpy(t, t0a, sizeof t);
  m->valid[0] = msgtype(pc, t, m->tx[0]);
  /* Tx2, Tx3 */
  char tt[96];
  g_strlcpy(tt, t0, sizeof tt);
  if (!bMy) { g_strlcpy(tt, t0b, sizeof tt); m->valid[0] = msgtype(pc, t0a, m->tx[0]); }
  if (!bHis) { g_strlcpy(tt, t0a, sizeof tt); snprintf(t, sizeof t, "%s%s", t0a, grid4); m->valid[0] = msgtype(pc, t, m->tx[0]); }
  snprintf(t, sizeof t, "%s%s", tt, rpt);
  m->valid[1] = msgtype(pc, t, m->tx[1]);
  snprintf(t, sizeof t, "%sR%s", tt, rpt);
  m->valid[2] = msgtype(pc, t, m->tx[2]);
  /* Tx4 */
  const char *rr = rr73 ? "RR73" : "RRR";
  snprintf(t, sizeof t, "%s%s", t0, rr);
  if (!bHis && bMy) snprintf(t, sizeof t, "%s <%s> %s", his, my, rr);
  if (bHis && !bMy) snprintf(t, sizeof t, "<%s> %s %s", his, my, rr);
  m->valid[3] = msgtype(pc, t, m->tx[3]);
  /* Tx5 */
  snprintf(t, sizeof t, "%s73", t0);
  if (!bHis && bMy) snprintf(t, sizeof t, "%s <%s> 73", his, my);
  if (bHis && !bMy) snprintf(t, sizeof t, "<%s> %s 73", his, my);
  m->valid[4] = msgtype(pc, t, m->tx[4]);
}

/* Decoder/decodedtext.cpp tokens_re (angle brackets already removed) */
static GRegex *tokens_re(void)
{
  static GRegex *re;
  if (!re)
    re = g_regex_new(
      "^(?:(?<dual>[A-Z0-9/]+)\\sRR73;\\s)?"
      "(?:(?<word1>(?:CQ|DE|QRZ)(?:\\s?DX|\\s(?:[A-Z]{1,4}|\\d{3}))|[A-Z0-9/]+|\\.{3})\\s)"
      "(?:(?<word2>[A-Z0-9/]+)"
        "(?:\\s(?<word3>[-+A-Z0-9]+)"
          "(?:\\s(?<word4>(?:OOO|(?!RR73)[A-R]{2}[0-9]{2}|5[0-9]{5}))"
            "(?:\\s(?<word5>[A-R]{2}[0-9]{2}[A-X]{2}))?"
          ")?"
        ")?"
      ")?", 0, 0, NULL);
  return re;
}

static void strip_brackets(const char *msg, char *out, gsize n)
{
  gsize j = 0;
  for (const char *p = msg; *p && j + 1 < n; p++)
    if (*p != '<' && *p != '>') out[j++] = *p;
  out[j] = '\0';
}

static void fetch(GMatchInfo *mi, const char *name, char *dst, gsize n)
{
  char *s = g_match_info_fetch_named(mi, name);
  g_strlcpy(dst, s ? s : "", n);
  g_free(s);
}

gboolean ftx_msg_tokens(const char *msg, char w1[16], char w2[16], char w3[16], char w4[16])
{
  char clean[64];
  strip_brackets(msg, clean, sizeof clean);
  w1[0] = w2[0] = w3[0] = w4[0] = '\0';
  GMatchInfo *mi = NULL;
  gboolean ok = g_regex_match(tokens_re(), clean, 0, &mi);
  if (ok) {
    fetch(mi, "word1", w1, 16);
    fetch(mi, "word2", w2, 16);
    fetch(mi, "word3", w3, 16);
    fetch(mi, "word4", w4, 16);
  }
  g_match_info_free(mi);
  return ok;
}

void ftx_msg_de_call_and_grid(const char *msg, char call[16], char grid[8])
{
  const char *semi = strstr(msg, "; ");
  char w1[16], w2[16], w3[16], w4[16];
  ftx_msg_tokens(semi ? semi + 2 : msg, w1, w2, w3, w4);
  g_strlcpy(call, w2, 16);
  g_strlcpy(grid, strcmp(w3, "R") == 0 ? w4 : w3, 8);
}

void ftx_msg_report_string(int snr, char rpt[8])
{
  if (snr < -50) snr = -50;
  if (snr > 49) snr = 49;
  snprintf(rpt, 8, "%+03d", snr);
}

static gboolean word_is(const char *w, const char *base)
{
  if (!base[0]) return FALSE;
  if (strcmp(w, base) == 0) return TRUE;
  gsize lw = strlen(w), lb = strlen(base);
  if (lw > lb + 1 && w[lw - lb - 1] == '/' && strcmp(w + lw - lb, base) == 0) return TRUE;   /* ends with /base */
  if (lw > lb + 1 && strncmp(w, base, lb) == 0 && w[lb] == '/') return TRUE;                   /* starts with base/ */
  return FALSE;
}

gboolean ftx_msg_report_in(ftx_pack77_ctx *pc, const char *msg, const char *mybase, const char *dxbase, char rpt[8])
{
  rpt[0] = '\0';
  char clean[64];
  strip_brackets(msg, clean, sizeof clean);
  char **tok = g_strsplit(g_strstrip(clean), " ", -1);
  const char *w[16];
  int n = 0;
  for (char **p = tok; *p && n < 16; p++) if (**p) w[n++] = *p;
  gboolean found = FALSE;
  if (n > 2) {
    int off = (strcmp(w[1], "RR73;") == 0 && n > 3) ? 2 : 0;
    if (ftx_msg_is_standard(pc, msg) &&
        (word_is(w[off], mybase) || (n > off + 1 && dxbase && dxbase[0] && word_is(w[off + 1], dxbase)))) {
      if (n > off + 2) {
        const char *tt = w[off + 2];
        char *end;
        long v = strtol(tt, &end, 10);
        if (*tt && *end == '\0' && v >= -50 && v < 50) { g_strlcpy(rpt, tt, 8); found = TRUE; }
        else if (tt[0] == 'R' && tt[1]) {
          v = strtol(tt + 1, &end, 10);
          if (*end == '\0' && v >= -50 && v < 50) { g_strlcpy(rpt, tt + 1, 8); found = TRUE; }
        }
      }
    }
  }
  g_strfreev(tok);
  return found;
}

gboolean ftx_grid_latlon(const char *grid, double *lat, double *lon)
{
  if (!grid) return FALSE;
  gsize n = strlen(grid);
  if (n != 4 && n != 6) return FALSE;
  if (g_ascii_strncasecmp(grid, "RR73", 4) == 0) return FALSE;   /* grid_regexp excludes RR73 */
  char g[7];
  for (gsize i = 0; i < n; i++) g[i] = (char)g_ascii_toupper(grid[i]);
  g[n] = '\0';
  if (g[0] < 'A' || g[0] > 'R' || g[1] < 'A' || g[1] > 'R' || !g_ascii_isdigit(g[2]) || !g_ascii_isdigit(g[3])) return FALSE;
  double lo = (g[0] - 'A') * 20.0 - 180.0 + (g[2] - '0') * 2.0;
  double la = (g[1] - 'A') * 10.0 - 90.0 + (g[3] - '0');
  if (n == 6) {
    if (g[4] < 'A' || g[4] > 'X' || g[5] < 'A' || g[5] > 'X') return FALSE;
    lo += (g[4] - 'A') * (2.0 / 24) + 1.0 / 24;
    la += (g[5] - 'A') * (1.0 / 24) + 0.5 / 24;
  } else {
    lo += 1.0;
    la += 0.5;
  }
  *lat = la;
  *lon = lo;
  return TRUE;
}

gboolean ftx_grid_az_dist(const char *mygrid, const char *hisgrid, double *az, double *km)
{
  double lat1, lon1, lat2, lon2;
  if (!ftx_grid_latlon(mygrid, &lat1, &lon1) || !ftx_grid_latlon(hisgrid, &lat2, &lon2)) return FALSE;
  const double d2r = G_PI / 180.0;
  lat1 *= d2r; lon1 *= d2r; lat2 *= d2r; lon2 *= d2r;
  double dlon = lon2 - lon1;
  double a = sin((lat2 - lat1) / 2), b = sin(dlon / 2);
  double h = a * a + cos(lat1) * cos(lat2) * b * b;
  *km = 2.0 * 6371.0 * asin(sqrt(h > 1 ? 1 : h));
  double y = sin(dlon) * cos(lat2);
  double x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dlon);
  *az = fmod(atan2(y, x) / d2r + 360.0, 360.0);
  return TRUE;
}
