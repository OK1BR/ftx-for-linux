/* ftx-record — record UTC-aligned FT8/FT4 periods from a TCI radio into
 * WAV files (12000 S/s, 16-bit mono), named like WSJT-X: YYMMDD_HHMMSS.wav.
 *
 * Copyright (C) 2026 Richard Fakenberg, OK1BR.  GPL-3.0-or-later.  No warranty.
 */
#include <glib.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/period.h"
#include "app/tci_client.h"

static volatile sig_atomic_t stop_flag;
static void on_sigint(int s) { (void)s; stop_flag = 1; }

struct rec {
  const char *dir;
  int max_files, written;
  gboolean quiet;
  FtxTciClient *tci;
  int tx_skipped;
};

static gboolean write_wav(const char *path, const int16_t *s, guint n)
{
  FILE *f = fopen(path, "wb");
  if (!f) { perror(path); return FALSE; }
  guint32 datalen = n * 2, rate = FTX_PERIOD_RATE, byterate = rate * 2, fmtlen = 16;
  guint16 fmt = 1, ch = 1, align = 2, bits = 16;
  guint32 riff = 36 + datalen;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
  fwrite("fmt ", 1, 4, f); fwrite(&fmtlen, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f);
  fwrite(&rate, 4, 1, f); fwrite(&byterate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&datalen, 4, 1, f);
  fwrite(s, 2, n, f);
  fclose(f);
  return TRUE;
}

static void on_audio(const float *mono, guint nframes, guint rate, gpointer user)
{
  ftx_period_push((FtxPeriod *)user, mono, nframes, rate);
}

static void on_period(const int16_t *samples, guint nsamp, int day, int nutc, double drift_ms, int gap_ms, int peak, gpointer user)
{
  struct rec *r = user;
  if (ftx_tci_client_tx(r->tci)) {
    r->tx_skipped++;
    if (!r->quiet) fprintf(stderr, "%06d_%06d: radio keyed, period skipped\n", day, nutc);
    return;
  }
  if (gap_ms > 0) {                                   /* start-up or a gap: not a usable period */
    if (!r->quiet) fprintf(stderr, "%06d_%06d: %d ms without audio, period skipped\n", day, nutc, gap_ms);
    return;
  }
  char path[512];
  snprintf(path, sizeof path, "%s/%06d_%06d.wav", r->dir, day, nutc);
  if (write_wav(path, samples, nsamp)) {
    r->written++;
    if (!r->quiet)
      fprintf(stderr, "%s  %u samples  peak %5d (%.0f %% FS)  clock-count drift %+.0f ms  dial %.0f Hz\n", path, nsamp,
              peak, 100.0 * peak / 32767.0, drift_ms, ftx_tci_client_vfo_hz(r->tci));
  }
  if (r->max_files > 0 && r->written >= r->max_files) stop_flag = 1;
}

static void usage(void)
{
  fprintf(stderr,
          "ftx-record — record UTC-aligned FT8/FT4 periods from a TCI radio as WAV files\n"
          "usage: ftx-record [-H host] [-p port] [-5] [-g gain] [-n count] [-q] directory\n"
          "  -H host    TCI server (default 127.0.0.1)      -p port    default 40001\n"
          "  -5         7.5 s FT4 periods (default 15 s FT8)\n"
          "  -g gain    float → int16 factor (default 32767 = full scale)\n"
          "  -n count   stop after count files              -q         quiet\n"
          "RX only: audio_samplerate:12000, mono float32, receiver 0. Periods while the\n"
          "radio is keyed are skipped. Ctrl-C stops.\n");
}

int main(int argc, char **argv)
{
  const char *host = "127.0.0.1";
  int port = 40001, period_ms = 15000, nmax = 0;
  float gain = 32767.0f;
  gboolean quiet = FALSE;
  int i = 1;
  for (; i < argc && argv[i][0] == '-'; i++) {
    const char *a = argv[i];
    if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 0; }
    if (!strcmp(a, "-5")) { period_ms = 7500; continue; }
    if (!strcmp(a, "-q")) { quiet = TRUE; continue; }
    if (i + 1 >= argc) { usage(); return 2; }
    const char *v = argv[++i];
    if (!strcmp(a, "-H")) host = v;
    else if (!strcmp(a, "-p")) port = atoi(v);
    else if (!strcmp(a, "-g")) gain = (float)atof(v);
    else if (!strcmp(a, "-n")) nmax = atoi(v);
    else { usage(); return 2; }
  }
  if (i != argc - 1) { usage(); return 2; }
  const char *dir = argv[i];
  if (g_mkdir_with_parents(dir, 0755) != 0) { perror(dir); return 1; }

  struct rec r = { dir, nmax, 0, quiet, NULL, 0 };
  r.tci = ftx_tci_client_new(host, (guint16)port);
  FtxPeriod *per = ftx_period_new(period_ms, gain, on_period, &r);
  ftx_tci_client_set_audio_cb(r.tci, on_audio, per);
  GError *err = NULL;
  if (!ftx_tci_client_start(r.tci, FTX_PERIOD_RATE, &err)) {
    fprintf(stderr, "ftx-record: %s\n", err->message);
    g_error_free(err);
    return 1;
  }
  fprintf(stderr, "ftx-record: connected to ws://%s:%d — device \"%s\", protocol \"%s\", dial %.0f Hz, mode %s; "
          "%d ms periods → %s\n", host, port, ftx_tci_client_device(r.tci), ftx_tci_client_protocol(r.tci),
          ftx_tci_client_vfo_hz(r.tci), ftx_tci_client_mode(r.tci), period_ms, dir);
  signal(SIGINT, on_sigint);
  signal(SIGTERM, on_sigint);
  while (!stop_flag) g_usleep(100000);
  ftx_tci_client_stop(r.tci);
  fprintf(stderr, "ftx-record: %d file(s) written, %d period(s) skipped while keyed, %u re-anchor(s)\n", r.written,
          r.tx_skipped, ftx_period_reanchors(per));
  ftx_period_free(per);
  ftx_tci_client_free(r.tci);
  return 0;
}
