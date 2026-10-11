/* rx_engine.c — see rx_engine.h.  Copyright (C) 2026 Richard Fakenberg, OK1BR.
 * GPL-3.0-or-later. */
#include "rx_engine.h"

#include <fftw3.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "period.h"
#include "tci_client.h"

#define FTX_RX_ERROR (g_quark_from_static_string("ftx-rx-engine-error"))

struct job {
  int16_t *samples;             /* NULL = stop marker                        */
  guint    nsamp;
  int      day, nutc;
  double   dial_hz, drift_ms;
  int      gap_ms, peak;
  gboolean ft4;
  ftx_ft8_params p8;
  ftx_ft4_params p4;
};

struct _FtxRxEngine {
  gint          refs;
  GMainContext *ctx;

  FtxRxDecodeCb decode_cb; gpointer decode_user;
  FtxRxPeriodCb period_cb; gpointer period_user;
  FtxRxRowCb    row_cb;    gpointer row_user;
  FtxRxVfoCb    vfo_cb;    gpointer vfo_user;
  FtxRxTxCb     tx_cb;     gpointer tx_user;
  FtxRxClosedCb closed_cb; gpointer closed_user;

  GMutex   lock;                /* parameters, mode, counters                */
  gboolean ft4;
  ftx_ft8_params p8;
  ftx_ft4_params p4;
  int      periods, decodes, skipped_keyed, skipped_gap;

  GMutex        audio_lock;     /* period assembler and spectrum state       */
  FtxPeriod    *period;
  FtxTciClient *tci;
  GAsyncQueue  *queue;
  GThread      *worker;
  gboolean      connected;
  gboolean      keyed_seen;     /* the radio was keyed at some point of the period being assembled */

  /* worker thread only */
  gboolean    ltry_a8;
  int         nd;
  double      dt_sum;
  struct job *cur;

  /* spectrum (audio thread; rps read atomically) */
  gint           rps;
  float         *sbuf, *swin;
  int            sfill;
  fftwf_complex *sout;
  fftwf_plan     splan;
  double        *sacc;
  int            snacc;
  long           since_row;
};

/* ---- delivery to the consumer's context -------------------------------- */

enum { EM_DECODE, EM_PERIOD, EM_ROW, EM_VFO, EM_TX, EM_CLOSED };

struct emit {
  FtxRxEngine *e;
  int kind;
  FtxRxDecode d;
  FtxRxPeriod p;
  float *row; guint nbins; double bin_hz; gint64 t_us;
  double hz;
  gboolean tx;
};

static void engine_unref(FtxRxEngine *e);

static gboolean emit_dispatch(gpointer data)
{
  struct emit *m = data;
  FtxRxEngine *e = m->e;
  switch (m->kind) {
  case EM_DECODE: if (e->decode_cb) e->decode_cb(&m->d, e->decode_user); break;
  case EM_PERIOD: if (e->period_cb) e->period_cb(&m->p, e->period_user); break;
  case EM_ROW:    if (e->row_cb) e->row_cb(m->row, m->nbins, m->bin_hz, m->t_us, e->row_user); break;
  case EM_VFO:    if (e->vfo_cb) e->vfo_cb(m->hz, e->vfo_user); break;
  case EM_TX:     if (e->tx_cb) e->tx_cb(m->tx, e->tx_user); break;
  case EM_CLOSED: if (e->closed_cb) e->closed_cb(e->closed_user); break;
  }
  return G_SOURCE_REMOVE;
}

static void emit_free(gpointer data)
{
  struct emit *m = data;
  g_free(m->row);
  engine_unref(m->e);
  g_free(m);
}

static struct emit *emit_new(FtxRxEngine *e, int kind)
{
  struct emit *m = g_new0(struct emit, 1);
  m->e = e;
  m->kind = kind;
  g_atomic_int_inc(&e->refs);
  return m;
}

static void emit(struct emit *m)
{
  if (m->e->ctx) {
    g_main_context_invoke_full(m->e->ctx, G_PRIORITY_DEFAULT, emit_dispatch, m, emit_free);
  } else {
    emit_dispatch(m);
    emit_free(m);
  }
}

/* ---- decoder worker ------------------------------------------------------ */

static void on_decode(void *user, float sync, int snr, float dt, float freq, const char *decoded, int nap, float qual)
{
  FtxRxEngine *e = user;
  struct job *j = e->cur;
  if (fabsf(freq - j->p8.nfqso) < 3.0f) e->ltry_a8 = FALSE;   /* decoder.f90 */
  e->nd++;
  e->dt_sum += dt;
  g_mutex_lock(&e->lock);
  e->decodes++;
  g_mutex_unlock(&e->lock);
  struct emit *m = emit_new(e, EM_DECODE);
  m->d.day = j->day; m->d.nutc = j->nutc; m->d.ft4 = j->ft4;
  m->d.sync = sync; m->d.snr = snr; m->d.dt = dt; m->d.freq = freq;
  g_strlcpy(m->d.msg, decoded, sizeof m->d.msg);
  m->d.nap = nap; m->d.qual = qual;
  emit(m);
}

static void emit_period(FtxRxEngine *e, const struct job *j, FtxRxPeriodStatus st, int nd, double ms, double dt_sum)
{
  struct emit *m = emit_new(e, EM_PERIOD);
  m->p.day = j->day; m->p.nutc = j->nutc; m->p.ft4 = j->ft4; m->p.status = st;
  m->p.ndecodes = nd; m->p.decode_ms = ms;
  m->p.have_dt = nd > 0; m->p.mean_dt = nd > 0 ? dt_sum / nd : 0.0;
  m->p.dial_hz = j->dial_hz; m->p.drift_ms = j->drift_ms; m->p.gap_ms = j->gap_ms; m->p.peak = j->peak;
  emit(m);
}

static gpointer worker(gpointer data)
{
  FtxRxEngine *e = data;
  ftx_ft8_ctx *c8 = ftx_ft8_ctx_new();
  ftx_ft4_ctx *c4 = ftx_ft4_ctx_new();
  int16_t *id2a = g_malloc(FT8_NMAX * sizeof(int16_t));
  for (;;) {
    struct job *j = g_async_queue_pop(e->queue);
    if (j->samples == NULL) { g_free(j); break; }
    e->nd = 0;
    e->dt_sum = 0;
    e->cur = j;
    gint64 t0 = g_get_monotonic_time();
    if (j->ft4) {
      ftx_ft4_decode(c4, j->samples, &j->p4, on_decode, e);
    } else {
      static const int seq[3] = { 41, 47, 50 };
      e->ltry_a8 = TRUE;                              /* decoder.f90: set at nzhsym 41 */
      for (int s = 0; s < 3; s++) {
        memset(id2a, 0, FT8_NMAX * sizeof(int16_t));
        memcpy(id2a, j->samples, (size_t)seq[s] * 3456 * sizeof(int16_t));
        j->p8.nzhsym = seq[s];
        ftx_ft8_decode(c8, id2a, &j->p8, j->nutc, e->ltry_a8, false, 0.0f, on_decode, e);
      }
    }
    double ms = (g_get_monotonic_time() - t0) / 1000.0;
    emit_period(e, j, FTX_RX_PERIOD_DECODED, e->nd, ms, e->dt_sum);
    e->cur = NULL;
    g_free(j->samples);
    g_free(j);
  }
  g_free(id2a);
  ftx_ft8_ctx_free(c8);
  ftx_ft4_ctx_free(c4);
  return NULL;
}

/* ---- audio thread -------------------------------------------------------- */

static void on_period(const int16_t *samples, guint nsamp, int day, int nutc, double drift_ms, int gap_ms, int peak, gpointer user)
{
  FtxRxEngine *e = user;
  struct job *j = g_new0(struct job, 1);
  j->day = day; j->nutc = nutc; j->drift_ms = drift_ms; j->gap_ms = gap_ms; j->peak = peak;
  j->dial_hz = ftx_tci_client_vfo_hz(e->tci);
  g_mutex_lock(&e->lock);
  e->periods++;
  j->ft4 = e->ft4;
  j->p8 = e->p8;
  j->p4 = e->p4;
  FtxRxPeriodStatus st = FTX_RX_PERIOD_DECODED;
  gboolean keyed = e->keyed_seen || ftx_tci_client_tx(e->tci);
  e->keyed_seen = FALSE;
  if (gap_ms > 0) { e->skipped_gap++; st = FTX_RX_PERIOD_GAP; }
  else if (keyed) { e->skipped_keyed++; st = FTX_RX_PERIOD_KEYED; }
  g_mutex_unlock(&e->lock);
  if (st != FTX_RX_PERIOD_DECODED) {
    emit_period(e, j, st, 0, 0.0, 0.0);
    g_free(j);
    return;
  }
  j->samples = g_malloc(nsamp * sizeof(int16_t));
  memcpy(j->samples, samples, nsamp * sizeof(int16_t));
  j->nsamp = nsamp;
  g_async_queue_push(e->queue, j);
}

static void spectrum_reset(FtxRxEngine *e)
{
  e->sfill = 0;
  e->snacc = 0;
  e->since_row = 0;
  memset(e->sacc, 0, FTX_RX_SPECTRUM_NBINS * sizeof(double));
}

static void spectrum_feed(FtxRxEngine *e, const float *mono, guint n, guint rate)
{
  const int rps = g_atomic_int_get(&e->rps);
  if (rps <= 0) { e->sfill = 0; return; }
  const int N = FTX_RX_SPECTRUM_NFFT, H = N / 4;      /* an FFT every 85 ms: rows up to ~11 per second */
  for (guint i = 0; i < n; i++) {
    e->sbuf[e->sfill++] = mono[i];
    if (e->sfill < N) continue;
    float in[FTX_RX_SPECTRUM_NFFT];
    for (int k = 0; k < N; k++) in[k] = e->sbuf[k] * e->swin[k];
    fftwf_execute_dft_r2c(e->splan, in, e->sout);
    for (int k = 0; k < FTX_RX_SPECTRUM_NBINS; k++)
      e->sacc[k] += (double)e->sout[k][0] * e->sout[k][0] + (double)e->sout[k][1] * e->sout[k][1];
    e->snacc++;
    memmove(e->sbuf, e->sbuf + H, (size_t)(N - H) * sizeof(float));   /* keep N-H, the next FFT after H new samples */
    e->sfill = N - H;
    e->since_row += H;
    const long row_len = (long)rate / rps;
    if (e->since_row >= row_len) {
      e->since_row -= row_len;
      struct emit *m = emit_new(e, EM_ROW);
      m->row = g_new(float, FTX_RX_SPECTRUM_NBINS);
      for (int k = 0; k < FTX_RX_SPECTRUM_NBINS; k++)
        m->row[k] = (float)(10.0 * log10(e->sacc[k] / e->snacc + 1e-30));
      m->nbins = FTX_RX_SPECTRUM_NBINS;
      m->bin_hz = (double)rate / N;
      m->t_us = g_get_real_time();
      memset(e->sacc, 0, FTX_RX_SPECTRUM_NBINS * sizeof(double));
      e->snacc = 0;
      emit(m);
    }
  }
}

static void on_audio(const float *mono, guint nframes, guint rate, gpointer user)
{
  FtxRxEngine *e = user;
  g_mutex_lock(&e->audio_lock);
  if (e->period) ftx_period_push(e->period, mono, nframes, rate);
  if (rate == FTX_PERIOD_RATE) spectrum_feed(e, mono, nframes, rate);
  g_mutex_unlock(&e->audio_lock);
}

static void on_vfo(double hz, gpointer user)
{
  struct emit *m = emit_new(user, EM_VFO);
  m->hz = hz;
  emit(m);
}

static void on_tx(gboolean tx, gpointer user)
{
  FtxRxEngine *e = user;
  if (tx) {
    g_mutex_lock(&e->lock);
    e->keyed_seen = TRUE;          /* our own transmission: that period is not decoded */
    g_mutex_unlock(&e->lock);
  }
  struct emit *m = emit_new(user, EM_TX);
  m->tx = tx;
  emit(m);
}

static void on_closed(gpointer user)
{
  emit(emit_new(user, EM_CLOSED));
}

/* ---- lifecycle ------------------------------------------------------------ */

FtxRxEngine *ftx_rx_engine_new(GMainContext *ctx)
{
  FtxRxEngine *e = g_new0(FtxRxEngine, 1);
  e->refs = 1;
  e->ctx = ctx ? g_main_context_ref(ctx) : NULL;
  g_mutex_init(&e->lock);
  g_mutex_init(&e->audio_lock);
  e->p8.nfa = 200; e->p8.nfb = 4000; e->p8.nfqso = 1500; e->p8.ndepth = 3; e->p8.napwid = 75; e->p8.lapon = true;
  e->p4.nfa = 200; e->p4.nfb = 4000; e->p4.nfqso = 1500; e->p4.ndepth = 3;
  e->rps = 2;
  const int N = FTX_RX_SPECTRUM_NFFT;
  e->sbuf = g_new0(float, N);
  e->swin = g_new(float, N);
  for (int k = 0; k < N; k++) e->swin[k] = (float)(0.5 - 0.5 * cos(2.0 * G_PI * k / N));   /* Hann */
  e->sout = fftwf_alloc_complex(FTX_RX_SPECTRUM_NBINS);
  e->sacc = g_new0(double, FTX_RX_SPECTRUM_NBINS);
  float *tmp = fftwf_alloc_real(N);
  e->splan = fftwf_plan_dft_r2c_1d(N, tmp, e->sout, FFTW_ESTIMATE);
  fftwf_free(tmp);
  return e;
}

static void engine_unref(FtxRxEngine *e)
{
  if (!g_atomic_int_dec_and_test(&e->refs)) return;
  fftwf_destroy_plan(e->splan);
  fftwf_free(e->sout);
  g_free(e->sbuf);
  g_free(e->swin);
  g_free(e->sacc);
  if (e->ctx) g_main_context_unref(e->ctx);
  g_mutex_clear(&e->lock);
  g_mutex_clear(&e->audio_lock);
  g_free(e);
}

void ftx_rx_engine_free(FtxRxEngine *e)
{
  if (!e) return;
  ftx_rx_engine_disconnect(e);
  /* emits already queued on the consumer's context are dispatched after
   * this returns: without callbacks they are no-ops (the engine itself lives
   * on until the last of them is freed) */
  e->decode_cb = NULL; e->period_cb = NULL; e->row_cb = NULL;
  e->vfo_cb = NULL; e->tx_cb = NULL; e->closed_cb = NULL;
  engine_unref(e);
}

void ftx_rx_engine_set_decode_cb(FtxRxEngine *e, FtxRxDecodeCb cb, gpointer u) { e->decode_cb = cb; e->decode_user = u; }
void ftx_rx_engine_set_period_cb(FtxRxEngine *e, FtxRxPeriodCb cb, gpointer u) { e->period_cb = cb; e->period_user = u; }
void ftx_rx_engine_set_row_cb(FtxRxEngine *e, FtxRxRowCb cb, gpointer u)       { e->row_cb = cb; e->row_user = u; }
void ftx_rx_engine_set_vfo_cb(FtxRxEngine *e, FtxRxVfoCb cb, gpointer u)       { e->vfo_cb = cb; e->vfo_user = u; }
void ftx_rx_engine_set_tx_cb(FtxRxEngine *e, FtxRxTxCb cb, gpointer u)         { e->tx_cb = cb; e->tx_user = u; }
void ftx_rx_engine_set_closed_cb(FtxRxEngine *e, FtxRxClosedCb cb, gpointer u) { e->closed_cb = cb; e->closed_user = u; }

void ftx_rx_engine_set_ft4(FtxRxEngine *e, gboolean ft4)
{
  g_mutex_lock(&e->lock);
  gboolean changed = e->ft4 != ft4;
  e->ft4 = ft4;
  g_mutex_unlock(&e->lock);
  if (!changed) return;
  g_mutex_lock(&e->audio_lock);
  if (e->period) {
    ftx_period_free(e->period);
    e->period = ftx_period_new(ft4 ? 7500 : 15000, 32767.0f, on_period, e);
  }
  g_mutex_unlock(&e->audio_lock);
}

gboolean ftx_rx_engine_get_ft4(FtxRxEngine *e)
{
  g_mutex_lock(&e->lock);
  gboolean v = e->ft4;
  g_mutex_unlock(&e->lock);
  return v;
}

void ftx_rx_engine_set_ft8_params(FtxRxEngine *e, const ftx_ft8_params *p)
{
  g_mutex_lock(&e->lock);
  e->p8 = *p;
  g_mutex_unlock(&e->lock);
}

void ftx_rx_engine_set_ft4_params(FtxRxEngine *e, const ftx_ft4_params *p)
{
  g_mutex_lock(&e->lock);
  e->p4 = *p;
  g_mutex_unlock(&e->lock);
}

void ftx_rx_engine_set_rows_per_second(FtxRxEngine *e, int rps)
{
  g_atomic_int_set(&e->rps, rps);
}

gboolean ftx_rx_engine_connect(FtxRxEngine *e, const char *host, guint16 port, GError **error)
{
  if (e->connected) {
    g_set_error(error, FTX_RX_ERROR, 1, "already connected");
    return FALSE;
  }
  e->tci = ftx_tci_client_new(host, port);
  g_mutex_lock(&e->audio_lock);
  e->period = ftx_period_new(e->ft4 ? 7500 : 15000, 32767.0f, on_period, e);
  spectrum_reset(e);
  g_mutex_unlock(&e->audio_lock);
  ftx_tci_client_set_audio_cb(e->tci, on_audio, e);
  ftx_tci_client_set_vfo_cb(e->tci, on_vfo, e);
  ftx_tci_client_set_tx_cb(e->tci, on_tx, e);
  ftx_tci_client_set_closed_cb(e->tci, on_closed, e);
  e->queue = g_async_queue_new();
  e->worker = g_thread_new("ftx-decode", worker, e);
  if (!ftx_tci_client_start(e->tci, FTX_PERIOD_RATE, error)) {
    e->connected = TRUE;              /* so that disconnect tears everything down */
    ftx_rx_engine_disconnect(e);
    return FALSE;
  }
  e->connected = TRUE;
  return TRUE;
}

void ftx_rx_engine_disconnect(FtxRxEngine *e)
{
  if (!e->connected) return;
  ftx_tci_client_stop(e->tci);
  g_async_queue_push(e->queue, g_new0(struct job, 1));   /* stop marker */
  g_thread_join(e->worker);
  e->worker = NULL;
  struct job *j;
  while ((j = g_async_queue_try_pop(e->queue)) != NULL) { g_free(j->samples); g_free(j); }
  g_async_queue_unref(e->queue);
  e->queue = NULL;
  g_mutex_lock(&e->audio_lock);
  ftx_period_free(e->period);
  e->period = NULL;
  g_mutex_unlock(&e->audio_lock);
  ftx_tci_client_free(e->tci);
  e->tci = NULL;
  e->connected = FALSE;
}

gboolean    ftx_rx_engine_connected(FtxRxEngine *e) { return e->connected; }
struct _FtxTciClient *ftx_rx_engine_tci(FtxRxEngine *e) { return e->connected ? e->tci : NULL; }
double      ftx_rx_engine_vfo_hz(FtxRxEngine *e)    { return e->tci ? ftx_tci_client_vfo_hz(e->tci) : 0.0; }
gboolean    ftx_rx_engine_tx(FtxRxEngine *e)        { return e->tci ? ftx_tci_client_tx(e->tci) : FALSE; }
const char *ftx_rx_engine_device(FtxRxEngine *e)    { return e->tci ? ftx_tci_client_device(e->tci) : ""; }
const char *ftx_rx_engine_protocol(FtxRxEngine *e)  { return e->tci ? ftx_tci_client_protocol(e->tci) : ""; }
const char *ftx_rx_engine_mode(FtxRxEngine *e)      { return e->tci ? ftx_tci_client_mode(e->tci) : ""; }

void ftx_rx_engine_counters(FtxRxEngine *e, int *periods, int *decodes, int *skipped_keyed, int *skipped_gap, guint *reanchors)
{
  g_mutex_lock(&e->lock);
  if (periods) *periods = e->periods;
  if (decodes) *decodes = e->decodes;
  if (skipped_keyed) *skipped_keyed = e->skipped_keyed;
  if (skipped_gap) *skipped_gap = e->skipped_gap;
  g_mutex_unlock(&e->lock);
  if (reanchors) {
    g_mutex_lock(&e->audio_lock);
    *reanchors = e->period ? ftx_period_reanchors(e->period) : 0;
    g_mutex_unlock(&e->audio_lock);
  }
}
