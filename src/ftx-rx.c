/* ftx-rx — live FT8/FT4 receiver: TCI audio in, decodes out, as jt9 prints
 * them.  Decoding runs in its own thread; the audio thread only assembles
 * periods.  Clock health: the mean DT of the decodes of each period is
 * reported, and flagged when it leaves ±0.5 s (WSJT-X tolerates about ±1 s;
 * a steady offset means the computer clock is off).
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
#include "ftx/ftx_ft4.h"
#include "ftx/ftx_ft8.h"

static volatile sig_atomic_t stop_flag;
static void on_sigint(int s) { (void)s; stop_flag = 1; }

struct job {
  int16_t *samples;
  guint    nsamp;
  int      day, nutc;
  double   dial_hz;
  double   drift_ms;
  int      gap_ms, peak;
};

struct rx {
  gboolean ft4;
  ftx_ft8_params p8;
  ftx_ft4_params p4;
  FtxTciClient *tci;
  GAsyncQueue *queue;
  GThread *worker;
  gboolean quiet;
  int periods, decodes, skipped_tx, skipped_gap;
  /* per-period decode statistics (worker thread) */
  int    nd;
  double dt_sum;
  int    nutc_cur;
  double dial_cur;
};

static void on_decode(void *user, float sync, int snr, float dt, float freq, const char *decoded, int nap, float qual)
{
  struct rx *r = user;
  char line[96];
  (void)sync;
  if (r->ft4) ftx_ft4_format_line(line, sizeof line, r->nutc_cur, snr, dt, freq, decoded, nap, qual);
  else ftx_ft8_format_line(line, sizeof line, r->nutc_cur, snr, dt, freq, decoded, nap, qual);
  printf("%s\n", line);
  fflush(stdout);
  r->nd++;
  r->dt_sum += dt;
  r->decodes++;
}

static gpointer worker(gpointer data)
{
  struct rx *r = data;
  ftx_ft8_ctx *c8 = r->ft4 ? NULL : ftx_ft8_ctx_new();
  ftx_ft4_ctx *c4 = r->ft4 ? ftx_ft4_ctx_new() : NULL;
  int16_t *id2a = malloc(FT8_NMAX * sizeof(int16_t));
  for (;;) {
    struct job *j = g_async_queue_pop(r->queue);
    if (j->samples == NULL) { free(j); break; }      /* stop marker */
    r->nd = 0;
    r->dt_sum = 0;
    r->nutc_cur = j->nutc;
    r->dial_cur = j->dial_hz;
    gint64 t0 = g_get_monotonic_time();
    if (r->ft4) {
      ftx_ft4_decode(c4, j->samples, &r->p4, on_decode, r);
    } else {
      static const int seq[3] = { 41, 47, 50 };
      gboolean ltry_a8 = TRUE;
      for (int s = 0; s < 3; s++) {
        memset(id2a, 0, FT8_NMAX * sizeof(int16_t));
        memcpy(id2a, j->samples, (size_t)seq[s] * 3456 * sizeof(int16_t));
        r->p8.nzhsym = seq[s];
        ftx_ft8_decode(c8, id2a, &r->p8, j->nutc, ltry_a8, false, 0.0f, on_decode, r);
      }
    }
    double ms = (g_get_monotonic_time() - t0) / 1000.0;
    if (!r->quiet) {
      char health[64] = "";
      if (r->nd > 0) {
        double mdt = r->dt_sum / r->nd;
        snprintf(health, sizeof health, "mean DT %+.2f s%s", mdt, fabs(mdt) > 0.5 ? "  ** CLOCK? **" : "");
      }
      fprintf(stderr, "%06d_%06d: %d decode(s) in %.0f ms, dial %.0f Hz, peak %d (%.0f %% FS), drift %+.0f ms  %s\n",
              j->day, j->nutc, r->nd, ms, j->dial_hz, j->peak, 100.0 * j->peak / 32767.0, j->drift_ms, health);
    }
    free(j->samples);
    free(j);
  }
  free(id2a);
  if (c8) ftx_ft8_ctx_free(c8);
  if (c4) ftx_ft4_ctx_free(c4);
  return NULL;
}

static void on_period(const int16_t *samples, guint nsamp, int day, int nutc, double drift_ms, int gap_ms, int peak, gpointer user)
{
  struct rx *r = user;
  r->periods++;
  if (gap_ms > 0) { r->skipped_gap++; return; }
  if (ftx_tci_client_tx(r->tci)) { r->skipped_tx++; return; }
  struct job *j = malloc(sizeof *j);
  j->samples = malloc(nsamp * sizeof(int16_t));
  memcpy(j->samples, samples, nsamp * sizeof(int16_t));
  j->nsamp = nsamp;
  j->day = day;
  j->nutc = nutc;
  j->dial_hz = ftx_tci_client_vfo_hz(r->tci);
  j->drift_ms = drift_ms;
  j->gap_ms = gap_ms;
  j->peak = peak;
  g_async_queue_push(r->queue, j);
}

static void on_audio(const float *mono, guint nframes, guint rate, gpointer user)
{
  ftx_period_push((FtxPeriod *)user, mono, nframes, rate);
}

static void usage(void)
{
  fprintf(stderr,
          "ftx-rx — live FT8/FT4 decoding from a TCI radio (RX only)\n"
          "usage: ftx-rx [-H host] [-p port] [-5] [-d depth] [-c MYCALL] [-x HISCALL] [-G GRID] [-f hz] [-Q n] [-q]\n"
          "  -H host    TCI server (default 127.0.0.1)   -p port   default 40001\n"
          "  -5         FT4 (7.5 s periods)              -d        depth 1..3 (default 3)\n"
          "  -c/-x/-G   my call, DX call, DX grid (a-priori decoding)\n"
          "  -f hz      Rx frequency in the passband (default 1500)   -Q n  QSO progress 0..5\n"
          "  -q         no per-period status line on stderr\n"
          "Decodes go to stdout in jt9's format; status (decode time, peak, clock drift, mean DT) to stderr.\n");
}

int main(int argc, char **argv)
{
  const char *host = "127.0.0.1";
  int port = 40001;
  struct rx r;
  memset(&r, 0, sizeof r);
  r.p8.nfa = 200; r.p8.nfb = 4000; r.p8.nfqso = 1500; r.p8.ndepth = 3; r.p8.napwid = 75; r.p8.lapon = true;
  r.p4.nfa = 200; r.p4.nfb = 4000; r.p4.nfqso = 1500; r.p4.ndepth = 3;
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 0; }
    if (!strcmp(a, "-5")) { r.ft4 = TRUE; continue; }
    if (!strcmp(a, "-q")) { r.quiet = TRUE; continue; }
    if (i + 1 >= argc) { usage(); return 2; }
    const char *v = argv[++i];
    if (!strcmp(a, "-H")) host = v;
    else if (!strcmp(a, "-p")) port = atoi(v);
    else if (!strcmp(a, "-d")) r.p8.ndepth = r.p4.ndepth = atoi(v);
    else if (!strcmp(a, "-c")) { snprintf(r.p8.mycall, sizeof r.p8.mycall, "%s", v); snprintf(r.p4.mycall, sizeof r.p4.mycall, "%s", v); }
    else if (!strcmp(a, "-x")) { snprintf(r.p8.hiscall, sizeof r.p8.hiscall, "%s", v); snprintf(r.p4.hiscall, sizeof r.p4.hiscall, "%s", v); }
    else if (!strcmp(a, "-G")) snprintf(r.p8.hisgrid, sizeof r.p8.hisgrid, "%s", v);
    else if (!strcmp(a, "-f")) r.p8.nfqso = r.p4.nfqso = atoi(v);
    else if (!strcmp(a, "-Q")) r.p8.nQSOProgress = r.p4.nQSOProgress = atoi(v);
    else { usage(); return 2; }
  }
  r.tci = ftx_tci_client_new(host, (guint16)port);
  r.queue = g_async_queue_new();
  FtxPeriod *per = ftx_period_new(r.ft4 ? 7500 : 15000, 32767.0f, on_period, &r);
  ftx_tci_client_set_audio_cb(r.tci, on_audio, per);
  GError *err = NULL;
  if (!ftx_tci_client_start(r.tci, FTX_PERIOD_RATE, &err)) {
    fprintf(stderr, "ftx-rx: %s\n", err->message);
    g_error_free(err);
    return 1;
  }
  fprintf(stderr, "ftx-rx: connected to ws://%s:%d — device \"%s\", protocol \"%s\", dial %.0f Hz, mode %s; %s depth %d\n",
          host, port, ftx_tci_client_device(r.tci), ftx_tci_client_protocol(r.tci), ftx_tci_client_vfo_hz(r.tci),
          ftx_tci_client_mode(r.tci), r.ft4 ? "FT4" : "FT8", r.p8.ndepth);
  r.worker = g_thread_new("ftx-decode", worker, &r);
  signal(SIGINT, on_sigint);
  signal(SIGTERM, on_sigint);
  while (!stop_flag) g_usleep(100000);
  ftx_tci_client_stop(r.tci);
  struct job *stopj = calloc(1, sizeof *stopj);
  g_async_queue_push(r.queue, stopj);
  g_thread_join(r.worker);
  fprintf(stderr, "ftx-rx: %d period(s), %d decode(s), %d skipped while keyed, %d skipped for gaps, %u re-anchor(s)\n",
          r.periods, r.decodes, r.skipped_tx, r.skipped_gap, ftx_period_reanchors(per));
  ftx_period_free(per);
  g_async_queue_unref(r.queue);
  ftx_tci_client_free(r.tci);
  return 0;
}
