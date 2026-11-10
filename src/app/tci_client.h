/* tci_client.h — TCI (ExpertSDR protocol) WebSocket client: RX audio in,
 * commands and TX audio out.
 *
 * Connects to a TCI server (sdr-for-linux, icom-for-linux) over its Unix
 * socket or its TCP port, waits for the init block (ready;), subscribes to receiver 0's audio at a
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
typedef void (*FtxTciModeCb)(const char *mode, gpointer user_data);   /* modulation:0,<mode> echo */
/* Keyed state changed (trx:0 OR tune:0). */
typedef void (*FtxTciTxCb)(gboolean tx, gpointer user_data);
/* The server closed the connection (not fired on ftx_tci_client_stop). */
typedef void (*FtxTciClosedCb)(gpointer user_data);

/* server: "unix:/path" (the server's Unix socket), "ws://host[:port]" or a
 * bare "host[:port]" (port 40001 when absent).  NULL or "" = the default
 * order: sdr-for-linux's socket $XDG_RUNTIME_DIR/sdr-for-linux/tci.sock,
 * then ws://127.0.0.1:40001 — a local radio is found whichever listener it
 * has on.  An address that does not parse fails in ftx_tci_client_start. */
FtxTciClient *ftx_tci_client_new(const char *server);
/* The address for a host and a port as the command line and old configs give
 * them: NULL when both are the defaults (NULL/"127.0.0.1" and 0/40001), the
 * host itself when it is already an address ("unix:…", "…://…"), else
 * "ws://host:port".  g_free. */
char         *ftx_tci_server_from_host_port(const char *host, int port);
/* Is this a well-formed server address ("" and NULL are: the default)? */
gboolean      ftx_tci_server_valid(const char *server);
void          ftx_tci_client_free(FtxTciClient *c);

void ftx_tci_client_set_audio_cb(FtxTciClient *c, FtxTciAudioCb cb, gpointer user_data);
void ftx_tci_client_set_vfo_cb(FtxTciClient *c, FtxTciVfoCb cb, gpointer user_data);
void ftx_tci_client_set_mode_cb(FtxTciClient *c, FtxTciModeCb cb, gpointer user_data);
void ftx_tci_client_set_tx_cb(FtxTciClient *c, FtxTciTxCb cb, gpointer user_data);
void ftx_tci_client_set_closed_cb(FtxTciClient *c, FtxTciClosedCb cb, gpointer user_data);

/* Connect, handshake, subscribe to audio at rate (8000/12000/24000/48000).
 * Blocks up to a few seconds for the handshake. */
gboolean ftx_tci_client_start(FtxTciClient *c, guint rate, GError **error);
void     ftx_tci_client_stop(FtxTciClient *c);

/* Send a text command, e.g. "trx:0,true,tci;" (any thread). */
void ftx_tci_client_send(FtxTciClient *c, const char *cmd);
/* Send one TX audio Stream block (type 2): nframes mono float32 samples at
 * rate Hz (sdr-for-linux upsamples naively, so send 48000).  Any thread. */
void ftx_tci_client_send_tx_audio(FtxTciClient *c, const float *mono, guint nframes, guint rate);

/* The address in use ("unix:/path" or "ws://host:port") once started, else
 * the first one that would be tried. */
const char *ftx_tci_client_endpoint(FtxTciClient *c);
double      ftx_tci_client_vfo_hz(FtxTciClient *c);
gboolean    ftx_tci_client_tx(FtxTciClient *c);       /* trx OR tune        */
gboolean    ftx_tci_client_trx(FtxTciClient *c);
const char *ftx_tci_client_device(FtxTciClient *c);
const char *ftx_tci_client_protocol(FtxTciClient *c);
const char *ftx_tci_client_mode(FtxTciClient *c);     /* modulation:0,<mode> */
int         ftx_tci_client_drive(FtxTciClient *c);    /* drive:0,<n> — watts in sdr-for-linux, 0 = unknown */

G_END_DECLS

#endif /* FTX_TCI_CLIENT_H */
