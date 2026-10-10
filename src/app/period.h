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

/* Same, with the arrival time given (UTC ns) — for tests and replay. */
void ftx_period_push_at(FtxPeriod *p, const float *mono, guint nframes, guint rate, gint64 t_utc_ns);

/* Statistics since start / last reset. */
guint ftx_period_reanchors(FtxPeriod *p);

#endif /* FTX_PERIOD_H */
