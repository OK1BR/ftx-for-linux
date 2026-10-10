/* tci_client.h — TCI (ExpertSDR protocol) WebSocket client for RX audio.
 *
 * Connects to a TCI server (sdr-for-linux, icom-for-linux: ws://host:40001),
 * waits for the init block (ready;), subscribes to receiver 0's audio at a
 * chosen rate (audio_samplerate / audio_stream_channels:1 / audio_start:0)
 * and hands the mono float blocks to a callback.  Tracks the dial frequency
 * (vfo:0,0,<hz>), mode and keyed state (trx/tune).  Modelled on
 * skimmer-for-linux's tci_client (IQ) and sdr-for-linux's tci_server.
 *
 * Part of FTX for Linux.  Copyright (C) 2026 Richard Fakenberg, OK1BR.
 * GPL-3.0-or-later; see <https://www.gnu.org/licenses/>.  No warranty.
 *
 * All callbacks fire on the client's libwebsockets service thread, not on
 * the GLib main loop — consumers queue/marshal themselves.
 */
#ifndef FTX_TCI_CLIENT_H
#define FTX_TCI_CLIENT_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct _FtxTciClient FtxTciClient;

/* One RX audio block: nframes mono float samples at rate Hz. */
typedef void (*FtxTciAudioCb)(const float *mono, guint nframes, guint rate, gpointer user_data);
/* Dial frequency of receiver 0 channel A changed (Hz). */
typedef void (*FtxTciVfoCb)(double vfo_hz, gpointer user_data);
/* Keyed state changed (trx:0 OR tune:0). */
typedef void (*FtxTciTxCb)(gboolean tx, gpointer user_data);
/* The server closed the connection (not fired on ftx_tci_client_stop). */
typedef void (*FtxTciClosedCb)(gpointer user_data);

FtxTciClient *ftx_tci_client_new(const char *host, guint16 port);
void          ftx_tci_client_free(FtxTciClient *c);

void ftx_tci_client_set_audio_cb(FtxTciClient *c, FtxTciAudioCb cb, gpointer user_data);
void ftx_tci_client_set_vfo_cb(FtxTciClient *c, FtxTciVfoCb cb, gpointer user_data);
void ftx_tci_client_set_tx_cb(FtxTciClient *c, FtxTciTxCb cb, gpointer user_data);
void ftx_tci_client_set_closed_cb(FtxTciClient *c, FtxTciClosedCb cb, gpointer user_data);

/* Connect, handshake, subscribe to audio at rate (8000/12000/24000/48000).
 * Blocks up to a few seconds for the handshake. */
gboolean ftx_tci_client_start(FtxTciClient *c, guint rate, GError **error);
void     ftx_tci_client_stop(FtxTciClient *c);

double      ftx_tci_client_vfo_hz(FtxTciClient *c);
gboolean    ftx_tci_client_tx(FtxTciClient *c);
const char *ftx_tci_client_device(FtxTciClient *c);
const char *ftx_tci_client_protocol(FtxTciClient *c);
const char *ftx_tci_client_mode(FtxTciClient *c);     /* modulation:0,<mode> */

G_END_DECLS

#endif /* FTX_TCI_CLIENT_H */
