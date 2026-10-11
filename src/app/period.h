/* period.h — assemble a continuous 12 kHz audio stream into UTC-aligned
 * FT8 (15 s) or FT4 (7.5 s) periods.
 *
 * Samples are counted; the wall clock (CLOCK_REALTIME, UTC) anchors the
 * count on the first block and re-anchors when the counted position drifts
 * from the clock by more than FTX_PERIOD_REANCHOR_MS (a dropped block, a
 * pause).  A completed period is handed over with the HHMMSS of its start.
 * Thread-agnostic: call ftx_period_push from the audio thread; the callback
 * fires on that thread.
 *
 * Part of FTX for Linux.  Copyright (C) 2026 Richard Fakenberg, OK1BR.
 * GPL-3.0-or-later.  No warranty.
 */
#ifndef FTX_PERIOD_H
#define FTX_PERIOD_H

#include <glib.h>
#include <stdint.h>

#define FTX_PERIOD_RATE        12000
#define FTX_PERIOD_REANCHOR_MS 100

typedef struct _FtxPeriod FtxPeriod;

/* A finished period: nsamp int16 samples (the full period: 180000 for FT8,
 * 90000 for FT4), nutc = HHMMSS of the period start, utc_day = YYMMDD,
 * drift_ms = clock minus sample count at the period end (health figure),
 * gap_ms = how much of the period had no audio (zeros: start-up, gaps),
 * peak = largest |sample| in the period (0..32767). */
typedef void (*FtxPeriodCb)(const int16_t *samples, guint nsamp, int utc_day, int nutc,
                            double drift_ms, int gap_ms, int peak, gpointer user_data);

/* period_ms: 15000 (FT8) or 7500 (FT4).  gain: float → int16 factor (32767
 * for a full-scale stream). */
FtxPeriod *ftx_period_new(int period_ms, float gain, FtxPeriodCb cb, gpointer user_data);
void       ftx_period_free(FtxPeriod *p);

/* Feed nframes mono float samples at rate Hz (only FTX_PERIOD_RATE is
 * accepted; other rates are reported once and dropped). */
void ftx_period_push(FtxPeriod *p, const float *mono, guint nframes, guint rate);

/* Stage marks: when the write position of the current period reaches
 * marks[i] samples, cb fires with the period so far (nsamp = marks[i];
 * samples beyond it are zero) — WSJT-X decodes FT8 at 41/47/50 half-symbols
 * (11.8/13.5/14.4 s) and FT4 at 21 (6.05 s).  gap_ms > 0 says the period
 * started late (missing audio before the first sample).  Fires on the
 * pushing thread, before the period-end callback. */
typedef void (*FtxPeriodStageCb)(const int16_t *samples, guint nsamp, int stage, int utc_day, int nutc,
                                 int gap_ms, gpointer user_data);
#define FTX_PERIOD_MAX_STAGES 4
void ftx_period_set_stages(FtxPeriod *p, const guint *marks, int nmarks, FtxPeriodStageCb cb, gpointer user_data);

/* Same, with the arrival time given (UTC ns) — for tests and replay. */
void ftx_period_push_at(FtxPeriod *p, const float *mono, guint nframes, guint rate, gint64 t_utc_ns);

/* Statistics since start / last reset. */
guint ftx_period_reanchors(FtxPeriod *p);

#endif /* FTX_PERIOD_H */
