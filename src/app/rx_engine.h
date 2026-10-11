/* rx_engine.h — the live receiver: TCI audio in, UTC periods, FT8/FT4
 * decoding in a worker thread, waterfall spectrum rows out.
 *
 * Shared by ftx-rx and the GUI.  The decoder call sequence is jt9's
 * (nzhsym 41/47/50 per FT8 period, ltry_a8 set at 41 and cleared by a decode
 * within 3 Hz of nfqso), periods with gaps or with the radio keyed are
 * skipped.  Decoder parameters are snapshotted into every period job when the
 * period completes, so the GUI may change them while a decode is running.
 *
 * Threads: the TCI client delivers audio on its own service thread (periods
 * are assembled there, the spectrum is computed there); decoding runs on the
 * engine's worker thread.  Callbacks are delivered on the GMainContext given
 * to ftx_rx_engine_new, or directly on the producing thread when it is NULL.
 *
 * Part of FTX for Linux.  Copyright (C) 2026 Richard Fakenberg, OK1BR.
 * GPL-3.0-or-later.  No warranty.
 */
#ifndef FTX_RX_ENGINE_H
#define FTX_RX_ENGINE_H

#include <glib.h>

#include "ftx/ftx_ft4.h"
#include "ftx/ftx_ft8.h"

G_BEGIN_DECLS

typedef struct _FtxRxEngine FtxRxEngine;
struct _FtxTciClient;

/* One decode, with the period it belongs to. */
typedef struct {
  int      day, nutc;      /* YYMMDD, HHMMSS of the period start           */
  gboolean ft4;
  float    sync;
  int      snr;
  float    dt, freq;
  char     msg[38];
  int      nap;
  float    qual;
} FtxRxDecode;

typedef enum { FTX_RX_PERIOD_DECODED, FTX_RX_PERIOD_GAP, FTX_RX_PERIOD_KEYED } FtxRxPeriodStatus;

/* End-of-period report (after decoding, or immediately for a skipped one). */
typedef struct {
  int      day, nutc;
  gboolean ft4;
  FtxRxPeriodStatus status;
  int      ndecodes;
  double   decode_ms;
  gboolean have_dt;        /* mean_dt valid (ndecodes > 0)                 */
  double   mean_dt;        /* mean DT of the period's decodes, s           */
  double   dial_hz, drift_ms;
  int      gap_ms, peak;
} FtxRxPeriod;

typedef void (*FtxRxDecodeCb)(const FtxRxDecode *d, gpointer user);
typedef void (*FtxRxPeriodCb)(const FtxRxPeriod *p, gpointer user);
/* One waterfall row: nbins power values in dB (relative), bin_hz apart from
 * 0 Hz; t_utc_us = wall-clock time of the row (UTC, microseconds). */
typedef void (*FtxRxRowCb)(const float *db, guint nbins, double bin_hz, gint64 t_utc_us, gpointer user);
typedef void (*FtxRxVfoCb)(double hz, gpointer user);
typedef void (*FtxRxTxCb)(gboolean tx, gpointer user);
typedef void (*FtxRxClosedCb)(gpointer user);

#define FTX_RX_SPECTRUM_NFFT  4096
#define FTX_RX_SPECTRUM_NBINS (FTX_RX_SPECTRUM_NFFT / 2 + 1)

FtxRxEngine *ftx_rx_engine_new(GMainContext *ctx);
void         ftx_rx_engine_free(FtxRxEngine *e);

void ftx_rx_engine_set_decode_cb(FtxRxEngine *e, FtxRxDecodeCb cb, gpointer user);
void ftx_rx_engine_set_period_cb(FtxRxEngine *e, FtxRxPeriodCb cb, gpointer user);
void ftx_rx_engine_set_row_cb(FtxRxEngine *e, FtxRxRowCb cb, gpointer user);
void ftx_rx_engine_set_vfo_cb(FtxRxEngine *e, FtxRxVfoCb cb, gpointer user);
void ftx_rx_engine_set_tx_cb(FtxRxEngine *e, FtxRxTxCb cb, gpointer user);
void ftx_rx_engine_set_closed_cb(FtxRxEngine *e, FtxRxClosedCb cb, gpointer user);

/* FT8 (15 s) or FT4 (7.5 s).  Switching while connected restarts the period
 * assembler; the next complete period is decoded in the new mode. */
void     ftx_rx_engine_set_ft4(FtxRxEngine *e, gboolean ft4);
gboolean ftx_rx_engine_get_ft4(FtxRxEngine *e);
void     ftx_rx_engine_set_ft8_params(FtxRxEngine *e, const ftx_ft8_params *p);
void     ftx_rx_engine_set_ft4_params(FtxRxEngine *e, const ftx_ft4_params *p);
/* Spectrum rows per second (0 = none, up to about 11). */
void     ftx_rx_engine_set_rows_per_second(FtxRxEngine *e, int rps);

/* Connect to the TCI server and start the audio stream; blocks for the
 * handshake (up to a few seconds) — call from a thread in a GUI. */
gboolean ftx_rx_engine_connect(FtxRxEngine *e, const char *host, guint16 port, GError **error);
void     ftx_rx_engine_disconnect(FtxRxEngine *e);
gboolean ftx_rx_engine_connected(FtxRxEngine *e);

/* The TCI client while connected (NULL otherwise) — the TX side sends on it. */
struct _FtxTciClient *ftx_rx_engine_tci(FtxRxEngine *e);
double      ftx_rx_engine_vfo_hz(FtxRxEngine *e);
gboolean    ftx_rx_engine_tx(FtxRxEngine *e);
const char *ftx_rx_engine_device(FtxRxEngine *e);
const char *ftx_rx_engine_protocol(FtxRxEngine *e);
const char *ftx_rx_engine_mode(FtxRxEngine *e);
void        ftx_rx_engine_counters(FtxRxEngine *e, int *periods, int *decodes, int *skipped_keyed,
                                   int *skipped_gap, guint *reanchors);

G_END_DECLS

#endif /* FTX_RX_ENGINE_H */
