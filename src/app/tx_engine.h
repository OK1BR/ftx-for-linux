/* tx_engine.h — transmitting over TCI: the FT8/FT4 waveform (as WSJT-X
 * sends it, 48 kHz) streamed to the radio as TX audio, keyed with
 * "trx:0,true,tci;" and paced in real time; Tune = a continuous tone.
 *
 * Timing follows WSJT-X's Modulator: the FT8 waveform starts 0.5 s after the
 * period start, FT4 0.3 s; a late start skips into the waveform so the
 * symbols stay aligned to UTC.  The radio is keyed FTX_TX_LEAD_MS before the
 * audio starts and unkeyed after a short silent tail.
 *
 * Part of FTX for Linux.  Copyright (C) 2026 Richard Fakenberg, OK1BR.
 * GPL-3.0-or-later.  No warranty.
 */
#ifndef FTX_TX_ENGINE_H
#define FTX_TX_ENGINE_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct _FtxTxEngine FtxTxEngine;
struct _FtxTciClient;

#define FTX_TX_RATE       48000
#define FTX_TX_DELAY_FT8  0.5      /* s into the period (Modulator.cpp delay_ms) */
#define FTX_TX_DELAY_FT4  0.3
#define FTX_TX_LEAD_MS    150      /* key-up before the audio, and the stream's lead over real time */

/* transmitting: TRUE at key-up, FALSE at unkey; info says what happened
 * ("keyed", "done", "stopped", or an error).  Delivered on the engine's
 * GMainContext. */
typedef void (*FtxTxStateCb)(gboolean transmitting, const char *info, gpointer user);

FtxTxEngine *ftx_tx_engine_new(GMainContext *ctx);
void         ftx_tx_engine_free(FtxTxEngine *t);
void         ftx_tx_engine_set_state_cb(FtxTxEngine *t, FtxTxStateCb cb, gpointer user);

/* Transmit msg at hz with the audio starting at start_utc_us (UTC, µs);
 * level 0..1 scales the waveform.  msgsent receives the message as it will
 * decode.  FALSE if the message does not pack or a transmission is running. */
gboolean ftx_tx_engine_transmit(FtxTxEngine *t, struct _FtxTciClient *tci, gboolean ft4, const char *msg,
                                double hz, gint64 start_utc_us, double level, char msgsent[38]);
/* A continuous tone at hz until ftx_tx_engine_stop. */
gboolean ftx_tx_engine_tune(FtxTxEngine *t, struct _FtxTciClient *tci, double hz, double level);
/* Unkey now (Halt TX, Tune off); returns when the radio has been unkeyed. */
void     ftx_tx_engine_stop(FtxTxEngine *t);
gboolean ftx_tx_engine_active(FtxTxEngine *t);

G_END_DECLS

#endif /* FTX_TX_ENGINE_H */
