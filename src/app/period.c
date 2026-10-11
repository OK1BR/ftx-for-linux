/* period.c — UTC-aligned period assembly (see period.h).
 * Copyright (C) 2026 Richard Fakenberg, OK1BR.  GPL-3.0-or-later. */
#include "period.h"

#include <math.h>
#include <string.h>
#include <time.h>

#define NS_PER_S 1000000000LL

struct _FtxPeriod {
  int      period_ms;
  gint64   period_ns;
  guint    nsamp;             /* samples per period                          */
  float    gain;
  int16_t *buf;
  gboolean anchored;
  gint64   anchor_ns;         /* UTC time of the first sample after the anchor */
  gint64   count;             /* samples received since the anchor           */
  gint64   period_index;      /* period being filled (floor(t / period))     */
  gint64   idx;               /* write index into buf                        */
  gint64   filled;            /* samples written into the current period     */
  guint    reanchors;
  gboolean warned_rate;
  int      peak;
  FtxPeriodCb cb;
  gpointer    user;
  guint    marks[FTX_PERIOD_MAX_STAGES];
  int      nmarks, next_stage;
  FtxPeriodStageCb stage_cb;
  gpointer stage_user;
};

static gint64 now_utc_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (gint64)ts.tv_sec * NS_PER_S + ts.tv_nsec;
}

FtxPeriod *ftx_period_new(int period_ms, float gain, FtxPeriodCb cb, gpointer user_data)
{
  FtxPeriod *p = g_new0(FtxPeriod, 1);
  p->period_ms = period_ms;
  p->period_ns = (gint64)period_ms * 1000000LL;
  p->nsamp = (guint)((gint64)FTX_PERIOD_RATE * period_ms / 1000);
  p->gain = gain;
  p->buf = g_new0(int16_t, p->nsamp);
  p->cb = cb;
  p->user = user_data;
  return p;
}

void ftx_period_free(FtxPeriod *p)
{
  if (!p) return;
  g_free(p->buf);
  g_free(p);
}

guint ftx_period_reanchors(FtxPeriod *p) { return p->reanchors; }

void ftx_period_set_stages(FtxPeriod *p, const guint *marks, int nmarks, FtxPeriodStageCb cb, gpointer user_data)
{
  p->nmarks = CLAMP(nmarks, 0, FTX_PERIOD_MAX_STAGES);
  for (int i = 0; i < p->nmarks; i++) p->marks[i] = marks[i];
  p->stage_cb = cb;
  p->stage_user = user_data;
  p->next_stage = 0;
  while (p->next_stage < p->nmarks && (gint64)p->marks[p->next_stage] <= p->idx) p->next_stage++;
}

static void period_stamp(FtxPeriod *p, int *day, int *nutc)
{
  gint64 start_ms = p->period_index * p->period_ms;
  time_t secs = (time_t)(start_ms / 1000);
  struct tm tm;
  gmtime_r(&secs, &tm);
  *nutc = tm.tm_hour * 10000 + tm.tm_min * 100 + tm.tm_sec;
  *day = (tm.tm_year % 100) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
}

static void emit(FtxPeriod *p, double drift_ms)
{
  if (!p->cb) return;
  int day, nutc;
  period_stamp(p, &day, &nutc);
  int gap_ms = (int)(((gint64)p->nsamp - p->filled) * 1000 / FTX_PERIOD_RATE);
  p->cb(p->buf, p->nsamp, day, nutc, drift_ms, gap_ms, p->peak, p->user);
}

static void emit_stage(FtxPeriod *p)
{
  int day, nutc;
  period_stamp(p, &day, &nutc);
  int gap_ms = (int)((p->idx - p->filled) * 1000 / FTX_PERIOD_RATE);
  p->stage_cb(p->buf, (guint)p->idx, p->next_stage, day, nutc, gap_ms, p->stage_user);
}

/* the first stage mark beyond the write position (a period joined late skips the earlier ones) */
static void reset_stages(FtxPeriod *p)
{
  p->next_stage = 0;
  while (p->next_stage < p->nmarks && (gint64)p->marks[p->next_stage] <= p->idx) p->next_stage++;
}

/* (Re)anchor on a block whose last sample is "now" (t_ns). */
static void anchor(FtxPeriod *p, gint64 t_ns, guint nframes)
{
  p->anchor_ns = t_ns - (gint64)nframes * NS_PER_S / FTX_PERIOD_RATE;
  p->count = 0;
  p->anchored = TRUE;
  gint64 pidx = p->anchor_ns / p->period_ns;
  gint64 off_ns = p->anchor_ns - pidx * p->period_ns;
  gint64 pos = off_ns * FTX_PERIOD_RATE / NS_PER_S;
  if (pidx != p->period_index || !p->filled) {       /* a new period (or the very first): start clean */
    memset(p->buf, 0, p->nsamp * sizeof(int16_t));
    p->peak = 0;
    p->filled = 0;
    p->period_index = pidx;
  }
  p->idx = pos;
  reset_stages(p);
}

void ftx_period_push(FtxPeriod *p, const float *mono, guint nframes, guint rate)
{
  ftx_period_push_at(p, mono, nframes, rate, now_utc_ns());
}

void ftx_period_push_at(FtxPeriod *p, const float *mono, guint nframes, guint rate, gint64 t)
{
  if (rate != FTX_PERIOD_RATE) {
    if (!p->warned_rate) { p->warned_rate = TRUE; g_warning("period: audio at %u Hz, want %d; dropped", rate, FTX_PERIOD_RATE); }
    return;
  }
  double drift_ms = 0.0;
  if (!p->anchored) {
    anchor(p, t, nframes);
  } else {
    /* the clock's position of the block end vs the sample count's */
    gint64 count_end_ns = p->anchor_ns + (p->count + (gint64)nframes) * NS_PER_S / FTX_PERIOD_RATE;
    drift_ms = (double)(t - count_end_ns) / 1e6;
    if (fabs(drift_ms) > FTX_PERIOD_REANCHOR_MS) {
      p->reanchors++;
      g_message("period: re-anchoring, drift %.0f ms (%u so far)", drift_ms, p->reanchors);
      anchor(p, t, nframes);
      drift_ms = 0.0;
    }
  }
  for (guint i = 0; i < nframes; i++) {
    if (p->idx >= (gint64)p->nsamp) {                /* period complete */
      emit(p, drift_ms);
      p->period_index++;
      memset(p->buf, 0, p->nsamp * sizeof(int16_t));
      p->peak = 0;
      p->filled = 0;
      p->idx = 0;
      p->next_stage = 0;
    }
    float v = mono[i] * p->gain;
    if (v > 32767.0f) v = 32767.0f;
    if (v < -32767.0f) v = -32767.0f;
    int16_t s = (int16_t)lrintf(v);
    p->buf[p->idx] = s;
    p->filled++;
    int a = s < 0 ? -s : s;
    if (a > p->peak) p->peak = a;
    p->idx++;
    if (p->stage_cb && p->next_stage < p->nmarks && p->idx == (gint64)p->marks[p->next_stage]) {
      emit_stage(p);
      p->next_stage++;
    }
  }
  p->count += nframes;
}
