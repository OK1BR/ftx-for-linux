/* tx_engine.c — see tx_engine.h.  Copyright (C) 2026 Richard Fakenberg, OK1BR.
 * GPL-3.0-or-later. */
#include "tx_engine.h"

#include <math.h>
#include <string.h>

#include "ftx/ftx_ft4.h"
#include "ftx/ftx_ft8.h"
#include "tci_client.h"

#define BLOCK      2048                              /* frames per Stream block (42.7 ms) */
#define LEAD_FRAMES (FTX_TX_RATE * FTX_TX_LEAD_MS / 1000)
#define TAIL_FRAMES (FTX_TX_RATE / 10)               /* 100 ms of silence before unkeying */

struct _FtxTxEngine {
  gint          refs;
  GMainContext *ctx;
  FtxTxStateCb  cb;
  gpointer      user;
  ftx_ft8_ctx  *c8;
  ftx_ft4_ctx  *c4;
  GThread      *thread;
  volatile gint stop;
  volatile gint active;        /* set by the caller, cleared by the thread after unkeying */
  /* the job */
  FtxTciClient *tci;
  float        *wave;
  int           nwave;
  gint64        start_us;
  gboolean      tune;
  double        hz, level;
};

struct emit { FtxTxEngine *t; gboolean transmitting; char info[64]; };

static void engine_unref(FtxTxEngine *t)
{
  if (!g_atomic_int_dec_and_test(&t->refs)) return;
  if (t->c8) ftx_ft8_ctx_free(t->c8);
  if (t->c4) ftx_ft4_ctx_free(t->c4);
  g_free(t->wave);
  if (t->ctx) g_main_context_unref(t->ctx);
  g_free(t);
}

static gboolean emit_dispatch(gpointer data)
{
  struct emit *m = data;
  if (m->t->cb) m->t->cb(m->transmitting, m->info, m->t->user);
  return G_SOURCE_REMOVE;
}

static void emit_free(gpointer data)
{
  struct emit *m = data;
  engine_unref(m->t);
  g_free(m);
}

static void emit_state(FtxTxEngine *t, gboolean transmitting, const char *info)
{
  struct emit *m = g_new0(struct emit, 1);
  m->t = t;
  m->transmitting = transmitting;
  g_strlcpy(m->info, info, sizeof m->info);
  g_atomic_int_inc(&t->refs);
  if (t->ctx) g_main_context_invoke_full(t->ctx, G_PRIORITY_DEFAULT, emit_dispatch, m, emit_free);
  else { emit_dispatch(m); emit_free(m); }
}

static void sleep_until(FtxTxEngine *t, gint64 when_us)
{
  for (;;) {
    if (g_atomic_int_get(&t->stop)) return;
    gint64 d = when_us - g_get_real_time();
    if (d <= 0) return;
    g_usleep(d > 5000 ? 5000 : (gulong)d);
  }
}

static void send_block(FtxTxEngine *t, const float *x, int n)
{
  ftx_tci_client_send_tx_audio(t->tci, x, (guint)n, FTX_TX_RATE);
}

/* Content index k of the stream maps to UTC time start_us + k/rate.  The
 * exciter plays a block the moment it arrives when its ring is empty and
 * continuously afterwards, so the first block must be sent exactly when its
 * first frame is due: FTX_TX_LEAD_MS of silence ahead of the waveform (the
 * stream then runs that much ahead of real time, which the server's ring
 * absorbs), or — on a late start — the waveform from the frame that is due
 * right now (Modulator.cpp: m_ic). */
static float content(FtxTxEngine *t, gint64 k, double *phase)
{
  if (k < 0) return 0.0f;
  if (t->tune) {
    double ramp = k < FTX_TX_RATE / 100 ? (double)k / (FTX_TX_RATE / 100) : 1.0;   /* 10 ms ramp */
    float v = (float)(t->level * ramp * sin(*phase));
    *phase += 2.0 * G_PI * t->hz / FTX_TX_RATE;
    if (*phase > 2.0 * G_PI) *phase -= 2.0 * G_PI;
    return v;
  }
  return k < t->nwave ? t->wave[k] : 0.0f;
}

static gpointer tx_thread(gpointer data)
{
  FtxTxEngine *t = data;
  ftx_tci_client_send(t->tci, "trx:0,true,tci;");
  emit_state(t, TRUE, "keyed");
  sleep_until(t, t->start_us - (gint64)FTX_TX_LEAD_MS * 1000);

  float block[BLOCK];
  const gint64 t_first = g_get_real_time();
  const gint64 k0 = (t_first - t->start_us) * FTX_TX_RATE / 1000000;   /* < 0: silence first */
  gint64 sent = k0;
  double phase = 0.0;
  gboolean stopped = FALSE;
  for (;;) {
    if (g_atomic_int_get(&t->stop)) { stopped = TRUE; break; }
    gint64 now = g_get_real_time();
    gint64 target = k0 + (now - t_first) * FTX_TX_RATE / 1000000 + LEAD_FRAMES;
    if (!t->tune && target > t->nwave) target = t->nwave;
    while (sent < target) {
      int n = (int)MIN((gint64)BLOCK, target - sent);
      for (int i = 0; i < n; i++) block[i] = content(t, sent + i, &phase);
      send_block(t, block, n);
      sent += n;
    }
    if (!t->tune && sent >= t->nwave) break;
    g_usleep(10000);
  }
  /* a silent tail, then unkey once the ring has played it out */
  memset(block, 0, sizeof block);
  for (int n = 0; n < TAIL_FRAMES; n += BLOCK) send_block(t, block, BLOCK);
  g_usleep((gulong)(FTX_TX_LEAD_MS + 100) * 1000);
  ftx_tci_client_send(t->tci, "trx:0,false;");
  g_atomic_int_set(&t->active, 0);
  emit_state(t, FALSE, stopped ? "stopped" : "done");
  return NULL;
}

FtxTxEngine *ftx_tx_engine_new(GMainContext *ctx)
{
  FtxTxEngine *t = g_new0(FtxTxEngine, 1);
  t->refs = 1;
  t->ctx = ctx ? g_main_context_ref(ctx) : NULL;
  t->c8 = ftx_ft8_ctx_new();     /* created here, on the caller's thread: FFTW planning is not thread-safe */
  t->c4 = ftx_ft4_ctx_new();
  return t;
}

void ftx_tx_engine_free(FtxTxEngine *t)
{
  if (!t) return;
  ftx_tx_engine_stop(t);
  t->cb = NULL;
  engine_unref(t);
}

void ftx_tx_engine_set_state_cb(FtxTxEngine *t, FtxTxStateCb cb, gpointer user) { t->cb = cb; t->user = user; }

static void reap(FtxTxEngine *t)
{
  if (t->thread) { g_thread_join(t->thread); t->thread = NULL; }
  g_atomic_int_set(&t->active, 0);
}

gboolean ftx_tx_engine_transmit(FtxTxEngine *t, FtxTciClient *tci, gboolean ft4, const char *msg,
                                double hz, gint64 start_utc_us, double level, char msgsent[38])
{
  if (g_atomic_int_get(&t->active)) return FALSE;
  reap(t);
  int nwave;
  if (ft4) {
    int itone[103];
    if (!ftx_ft4_tx_tones(t->c4, msg, msgsent, itone)) return FALSE;
    nwave = 105 * 2304;
    t->wave = g_realloc(t->wave, (gsize)nwave * sizeof(float));
    ftx_ft4_tx_wave(t->c4, itone, 2304, FTX_TX_RATE, (float)hz, t->wave, nwave);
  } else {
    int itone[79];
    if (!ftx_ft8_tx_tones(t->c8, msg, msgsent, itone)) return FALSE;
    nwave = 79 * 7680;
    t->wave = g_realloc(t->wave, (gsize)nwave * sizeof(float));
    ftx_ft8_tx_wave(t->c8, itone, 7680, FTX_TX_RATE, (float)hz, t->wave, nwave);
  }
  for (int i = 0; i < nwave; i++) t->wave[i] *= (float)level;
  t->nwave = nwave;
  t->tci = tci;
  t->tune = FALSE;
  t->hz = hz;
  t->level = level;
  t->start_us = start_utc_us;
  g_atomic_int_set(&t->stop, 0);
  g_atomic_int_set(&t->active, 1);
  t->thread = g_thread_new("ftx-tx", tx_thread, t);
  return TRUE;
}

gboolean ftx_tx_engine_tune(FtxTxEngine *t, FtxTciClient *tci, double hz, double level)
{
  if (g_atomic_int_get(&t->active)) return FALSE;
  reap(t);
  t->tci = tci;
  t->tune = TRUE;
  t->hz = hz;
  t->level = level;
  t->nwave = 0;
  t->start_us = g_get_real_time() + (gint64)FTX_TX_LEAD_MS * 1000;
  g_atomic_int_set(&t->stop, 0);
  g_atomic_int_set(&t->active, 1);
  t->thread = g_thread_new("ftx-tune", tx_thread, t);
  return TRUE;
}

void ftx_tx_engine_stop(FtxTxEngine *t)
{
  if (!t->thread) return;
  g_atomic_int_set(&t->stop, 1);
  reap(t);
}

gboolean ftx_tx_engine_active(FtxTxEngine *t)
{
  return g_atomic_int_get(&t->active) != 0;
}
