/* ftx-rx — live FT8/FT4 receiver: TCI audio in, decodes out, as jt9 prints
 * them.  A thin CLI over the RX engine (src/app/rx_engine.c), which decodes
 * in its own thread while the audio thread assembles periods.  Clock health:
 * the mean DT of each period's decodes is reported and flagged when it leaves
 * ±0.5 s (WSJT-X tolerates about ±1 s; a steady offset means the computer
 * clock is off).
 *
 * Copyright (C) 2026 Richard Fakenberg, OK1BR.  GPL-3.0-or-later.  No warranty.
 */
#include <glib.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/rx_engine.h"

static volatile sig_atomic_t stop_flag;
static void on_sigint(int s) { (void)s; stop_flag = 1; }

struct rx {
  gboolean quiet, server_closed;
};

static void on_decode(const FtxRxDecode *d, gpointer user)
{
  (void)user;
  char line[96];
  if (d->ft4) ftx_ft4_format_line(line, sizeof line, d->nutc, d->snr, d->dt, d->freq, d->msg, d->nap, d->qual);
  else ftx_ft8_format_line(line, sizeof line, d->nutc, d->snr, d->dt, d->freq, d->msg, d->nap, d->qual);
  printf("%s\n", line);
  fflush(stdout);
}

static void on_period(const FtxRxPeriod *p, gpointer user)
{
  struct rx *r = user;
  if (r->quiet || p->status != FTX_RX_PERIOD_DECODED) return;
  char health[64] = "";
  if (p->have_dt)
    /* a steady small negative offset is stream latency; a large one is the computer clock */
    snprintf(health, sizeof health, "mean DT %+.2f s%s", p->mean_dt, fabs(p->mean_dt) > 0.5 ? "  ** CLOCK OFF? **" : "");
  fprintf(stderr, "%06d_%06d: %d decode(s) in %.0f ms, dial %.0f Hz, peak %d (%.0f %% FS), drift %+.0f ms  %s\n",
          p->day, p->nutc, p->ndecodes, p->decode_ms, p->dial_hz, p->peak, 100.0 * p->peak / 32767.0, p->drift_ms, health);
}

static void on_closed(gpointer user)
{
  struct rx *r = user;
  r->server_closed = TRUE;
  stop_flag = 1;
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
  gboolean ft4 = FALSE;
  struct rx r;
  memset(&r, 0, sizeof r);
  ftx_ft8_params p8;
  ftx_ft4_params p4;
  memset(&p8, 0, sizeof p8);
  memset(&p4, 0, sizeof p4);
  p8.nfa = 200; p8.nfb = 4000; p8.nfqso = 1500; p8.ndepth = 3; p8.napwid = 75; p8.lapon = true;
  p4.nfa = 200; p4.nfb = 4000; p4.nfqso = 1500; p4.ndepth = 3;
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 0; }
    if (!strcmp(a, "-5")) { ft4 = TRUE; continue; }
    if (!strcmp(a, "-q")) { r.quiet = TRUE; continue; }
    if (i + 1 >= argc) { usage(); return 2; }
    const char *v = argv[++i];
    if (!strcmp(a, "-H")) host = v;
    else if (!strcmp(a, "-p")) port = atoi(v);
    else if (!strcmp(a, "-d")) p8.ndepth = p4.ndepth = atoi(v);
    else if (!strcmp(a, "-c")) { snprintf(p8.mycall, sizeof p8.mycall, "%s", v); snprintf(p4.mycall, sizeof p4.mycall, "%s", v); }
    else if (!strcmp(a, "-x")) { snprintf(p8.hiscall, sizeof p8.hiscall, "%s", v); snprintf(p4.hiscall, sizeof p4.hiscall, "%s", v); }
    else if (!strcmp(a, "-G")) snprintf(p8.hisgrid, sizeof p8.hisgrid, "%s", v);
    else if (!strcmp(a, "-f")) p8.nfqso = p4.nfqso = atoi(v);
    else if (!strcmp(a, "-Q")) p8.nQSOProgress = p4.nQSOProgress = atoi(v);
    else { usage(); return 2; }
  }
  FtxRxEngine *e = ftx_rx_engine_new(NULL);          /* callbacks on the producing threads */
  ftx_rx_engine_set_ft4(e, ft4);
  ftx_rx_engine_set_ft8_params(e, &p8);
  ftx_rx_engine_set_ft4_params(e, &p4);
  ftx_rx_engine_set_rows_per_second(e, 0);
  ftx_rx_engine_set_decode_cb(e, on_decode, &r);
  ftx_rx_engine_set_period_cb(e, on_period, &r);
  ftx_rx_engine_set_closed_cb(e, on_closed, &r);
  GError *err = NULL;
  if (!ftx_rx_engine_connect(e, host, (guint16)port, &err)) {
    fprintf(stderr, "ftx-rx: %s\n", err->message);
    g_error_free(err);
    return 1;
  }
  fprintf(stderr, "ftx-rx: connected to ws://%s:%d — device \"%s\", protocol \"%s\", dial %.0f Hz, mode %s; %s depth %d\n",
          host, port, ftx_rx_engine_device(e), ftx_rx_engine_protocol(e), ftx_rx_engine_vfo_hz(e),
          ftx_rx_engine_mode(e), ft4 ? "FT4" : "FT8", p8.ndepth);
  signal(SIGINT, on_sigint);
  signal(SIGTERM, on_sigint);
  while (!stop_flag) g_usleep(100000);
  int periods, decodes, skipped_tx, skipped_gap;
  guint reanchors;
  ftx_rx_engine_counters(e, &periods, &decodes, &skipped_tx, &skipped_gap, &reanchors);
  ftx_rx_engine_disconnect(e);
  fprintf(stderr, "ftx-rx: %d period(s), %d decode(s), %d skipped while keyed, %d skipped for gaps, %u re-anchor(s)%s\n",
          periods, decodes, skipped_tx, skipped_gap, reanchors,
          r.server_closed ? " — the server closed the connection" : "");
  ftx_rx_engine_free(e);
  return 0;
}
