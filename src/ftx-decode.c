/* ftx-decode — decode FT8 from 15 s WAV files (12000 S/s, 16-bit mono) and
 * print decodes the way WSJT-X's jt9 does.
 *
 * Copyright (C) 2026 Richard Fakenberg, OK1BR.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.  It is distributed WITHOUT ANY WARRANTY; see the GNU
 * General Public License for details: <https://www.gnu.org/licenses/>.
 *
 * The decoder core is a C port of WSJT-X v3.0.2 (Joe Taylor, K1JT, et al.,
 * GPL-3.0); see the headers in src/ftx/ for the detailed provenance.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ftx/ftx_ft8.h"

#ifndef FTX_VERSION
#define FTX_VERSION "dev"
#endif

struct cbctx {
  int nutc;
  int nfqso;
  bool ltry_a8;
  int ndecodes;
};

static void on_decode(void *user, float sync, int snr, float dt, float freq, const char *decoded, int nap, float qual)
{
  struct cbctx *c = user;
  char line[96];
  (void)sync;
  if (fabsf(freq - c->nfqso) < 3.0f) c->ltry_a8 = false;   /* decoder.f90 */
  ftx_ft8_format_line(line, sizeof line, c->nutc, snr, dt, freq, decoded, nap, qual);
  puts(line);
  c->ndecodes++;
}

static int nutc_from_name(const char *path)
{
  const char *base = strrchr(path, '/');
  base = base != NULL ? base + 1 : path;
  const char *u = strrchr(base, '_');
  if (u == NULL) return 0;
  int v = 0;
  for (int i = 1; i <= 6; i++) {
    if (u[i] < '0' || u[i] > '9') return 0;
    v = v * 10 + (u[i] - '0');
  }
  return v;
}

static int read_wav(const char *path, int16_t *iwave)
{
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    perror(path);
    return -1;
  }
  unsigned char hdr[44];
  if (fread(hdr, 1, 44, fp) != 44 || memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
    fprintf(stderr, "%s: not a RIFF/WAVE file\n", path);
    fclose(fp);
    return -1;
  }
  int rate = hdr[24] | hdr[25] << 8 | hdr[26] << 16 | hdr[27] << 24;
  int bits = hdr[34] | hdr[35] << 8, chans = hdr[22] | hdr[23] << 8;
  if (rate != 12000 || bits != 16 || chans != 1)
    fprintf(stderr, "%s: warning: expected 12000 S/s 16-bit mono, got %d S/s %d-bit %d ch\n", path, rate, bits, chans);
  memset(iwave, 0, FT8_NMAX * sizeof(int16_t));
  size_t n = fread(iwave, sizeof(int16_t), FT8_NMAX, fp);
  fclose(fp);
  if (n < FT8_NMAX) fprintf(stderr, "%s: warning: only %zu samples, rest zero\n", path, n);
  return 0;
}

static void usage(void)
{
  fprintf(stderr,
          "ftx-decode %s — FT8 decoder for 15 s WAV files (12000 S/s, 16-bit mono)\n"
          "usage: ftx-decode [options] file.wav...\n"
          "  -d DEPTH     decoding depth 1..3 (default 3)\n"
          "  -c MYCALL    my callsign for a-priori decoding\n"
          "  -x HISCALL   DX callsign for a-priori decoding\n"
          "  -G GRID      DX grid (reserved for the a8 decoder)\n"
          "  -f HZ        Rx frequency (default 1500)\n"
          "  -Q N         QSO progress 0..5 (default 0)\n"
          "  -1           single decode call (nzhsym 50) instead of jt9's 41/47/50 sequence\n"
          "Output: HHMMSS SNR DT FREQ ~ MESSAGE [aN], as jt9 prints it.\n",
          FTX_VERSION);
}

int main(int argc, char **argv)
{
  ftx_ft8_params p;
  memset(&p, 0, sizeof p);
  p.nfa = 200;
  p.nfb = 4000;
  p.nfqso = 1500;
  p.ndepth = 3;
  p.napwid = 75;
  p.lapon = true;
  bool single = false;
  int i = 1;
  for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
    const char *a = argv[i];
    if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) { usage(); return 0; }
    if (strcmp(a, "-1") == 0) { single = true; continue; }
    if (i + 1 >= argc) { usage(); return 2; }
    const char *v = argv[++i];
    if (strcmp(a, "-d") == 0) p.ndepth = atoi(v);
    else if (strcmp(a, "-c") == 0) snprintf(p.mycall, sizeof p.mycall, "%s", v);
    else if (strcmp(a, "-x") == 0) snprintf(p.hiscall, sizeof p.hiscall, "%s", v);
    else if (strcmp(a, "-G") == 0) snprintf(p.hisgrid, sizeof p.hisgrid, "%s", v);
    else if (strcmp(a, "-f") == 0) p.nfqso = atoi(v);
    else if (strcmp(a, "-Q") == 0) p.nQSOProgress = atoi(v);
    else { usage(); return 2; }
  }
  if (i >= argc) { usage(); return 2; }
  if (p.ndepth < 1 || p.ndepth > 3 || p.nQSOProgress < 0 || p.nQSOProgress > 5) { usage(); return 2; }

  ftx_ft8_ctx *ctx = ftx_ft8_ctx_new();
  if (ctx == NULL) {
    fprintf(stderr, "out of memory\n");
    return 1;
  }
  int16_t *iwave = malloc(FT8_NMAX * sizeof(int16_t));
  int16_t *id2a = malloc(FT8_NMAX * sizeof(int16_t));
  for (; i < argc; i++) {
    if (read_wav(argv[i], iwave) != 0) continue;
    struct cbctx c = { nutc_from_name(argv[i]), p.nfqso, true, 0 };
    static const int seq[3] = { 41, 47, 50 };
    for (int s = single ? 2 : 0; s < 3; s++) {
      int nzhsym = seq[s];
      memset(id2a, 0, FT8_NMAX * sizeof(int16_t));
      memcpy(id2a, iwave, (size_t)nzhsym * 3456 * sizeof(int16_t));
      p.nzhsym = nzhsym;
      ftx_ft8_decode(ctx, id2a, &p, c.nutc, c.ltry_a8, true, 0.0f, on_decode, &c);
    }
  }
  free(iwave);
  free(id2a);
  ftx_ft8_ctx_free(ctx);
  return 0;
}
