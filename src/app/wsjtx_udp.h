/* wsjtx_udp.h — the WSJT-X UDP protocol as a client, so that log-for-linux
 * (and any other WSJT-X-aware logger) sees FTX as it sees WSJT-X.
 *
 * Ported from WSJT-X Network/NetworkMessage.hpp and Network/MessageClient.cpp
 * @ v3.0.2 (schema 3): outgoing Heartbeat (0), Status (1), Decode (2), Clear
 * (3), QSO Logged (5), Close (6) and Logged ADIF (12) with the field order of
 * MessageClient; incoming Halt Tx (8) and Highlight Callsign (13), which is
 * how log-for-linux answers a Status (worked-before colours, its
 * src/engine/wsjtx_udp.c).  Encodings: big-endian QDataStream; utf8 = quint32
 * length + bytes (0xffffffff = null); bool = quint8; QDateTime = qint64 Julian
 * day + quint32 ms since midnight + quint8 timespec (1 = UTC); QTime = quint32
 * ms; float = IEEE double; QColor = quint8 spec (0 invalid, 1 RGB) + 5 ×
 * quint16 (alpha, r, g, b, pad).  GLib/Gio only.
 *
 * Copyright (C) 2001-2026 by Joe Taylor, K1JT, Bill Somerville, G4WJS,
 * Steve Franke, K9AN, Nico Palermo, IV3NWV, Uwe Risse, DG2YCB, Brian Moran,
 * N9ADG, Roger Rehr, W3SZ, John Nelson, G4KLA, Charlie Suckling, DL3WDG,
 * Terrell Deppe, KJ5HST (WSJT-X).  C port Copyright (C) 2026 Richard
 * Fakenberg, OK1BR.  Part of FTX for Linux.  GPL-3.0-or-later.  No warranty.
 */
#ifndef FTX_WSJTX_UDP_H
#define FTX_WSJTX_UDP_H

#include <glib.h>

#define FTX_WSJTX_MAGIC        0xadbccbdaU
#define FTX_WSJTX_SCHEMA       3
#define FTX_WSJTX_DEFAULT_PORT 2237
#define FTX_WSJTX_PULSE_S      15          /* NetworkMessage::pulse */

enum {
  FTX_WSJTX_MSG_HEARTBEAT = 0, FTX_WSJTX_MSG_STATUS = 1, FTX_WSJTX_MSG_DECODE = 2,
  FTX_WSJTX_MSG_CLEAR = 3, FTX_WSJTX_MSG_REPLY = 4, FTX_WSJTX_MSG_QSO_LOGGED = 5,
  FTX_WSJTX_MSG_CLOSE = 6, FTX_WSJTX_MSG_REPLAY = 7, FTX_WSJTX_MSG_HALT_TX = 8,
  FTX_WSJTX_MSG_FREE_TEXT = 9, FTX_WSJTX_MSG_LOGGED_ADIF = 12, FTX_WSJTX_MSG_HIGHLIGHT = 13,
};

/* Status (type 1), MessageClient::status_update's arguments in order. */
typedef struct {
  guint64     dial_hz;
  const char *mode, *dx_call, *report, *tx_mode;
  gboolean    tx_enabled, transmitting, decoding;
  guint32     rx_df, tx_df;
  const char *de_call, *de_grid, *dx_grid;
  gboolean    watchdog;           /* Tx watchdog tripped */
  const char *sub_mode;
  gboolean    fast_mode;
  guint8      special_op;
  guint32     freq_tol;           /* 0xffffffff = n/a */
  guint32     tr_period;          /* seconds */
  const char *config_name, *tx_message;
} FtxWsjtxStatus;

/* QSO Logged (type 5), MessageClient::qso_logged's arguments; times are unix
 * UTC seconds.  freq_hz is what WSJT-X logs: dial + Tx audio offset
 * (mainwindow.cpp: m_freqNominal + TxFreqSpinBox). */
typedef struct {
  gint64      on_s, off_s;
  const char *dx_call, *dx_grid;
  guint64     freq_hz;
  const char *mode, *rst_sent, *rst_rcvd, *tx_power, *comments, *name;
  const char *op_call, *my_call, *my_grid, *exch_sent, *exch_rcvd, *prop_mode;
} FtxWsjtxQso;

/* ---- builders (pure; exposed for the gates) ------------------------------- */
GByteArray *ftx_wsjtx_build_heartbeat(const char *id, const char *version, const char *revision);
GByteArray *ftx_wsjtx_build_status(const char *id, const FtxWsjtxStatus *s);
GByteArray *ftx_wsjtx_build_decode(const char *id, gboolean is_new, guint32 time_ms, gint32 snr, double dt,
                                   guint32 df, const char *mode, const char *msg, gboolean low_conf, gboolean off_air);
GByteArray *ftx_wsjtx_build_clear(const char *id);
GByteArray *ftx_wsjtx_build_close(const char *id);
GByteArray *ftx_wsjtx_build_qso_logged(const char *id, const FtxWsjtxQso *q);
GByteArray *ftx_wsjtx_build_logged_adif(const char *id, const char *adif_text);
/* LogBook::QSOToADIF (logbook.cpp): the record without <EOR>; band like "20m". */
char       *ftx_wsjtx_adif_record(const FtxWsjtxQso *q, const char *band);
/* The Logged ADIF text: header + record + " <EOR>" as MessageClient::logged_ADIF. */
char       *ftx_wsjtx_adif_text(const FtxWsjtxQso *q, const char *band, const char *programid);
gint64      ftx_wsjtx_julian_day(gint64 unix_s);
/* Highlight Callsign (type 13) from a server.  Returns FALSE on a malformed
 * datagram or another type.  Strings are newly allocated. */
gboolean    ftx_wsjtx_parse_highlight(const guint8 *data, gsize len, char **id, char **call,
                                      gboolean *bg_valid, guint8 bg_rgb[3],
                                      gboolean *fg_valid, guint8 fg_rgb[3], gboolean *last_only);
/* Header only: magic, schema, type, id (newly allocated). */
gboolean    ftx_wsjtx_parse_header(const guint8 *data, gsize len, guint32 *type, char **id, gsize *payload_off);

/* ---- the client ------------------------------------------------------------- */
typedef struct _FtxWsjtxUdp FtxWsjtxUdp;
typedef void (*FtxWsjtxHighlightCb)(const char *call, gboolean bg_valid, const guint8 bg_rgb[3], gboolean last_only, gpointer user);
typedef void (*FtxWsjtxHaltCb)(gboolean auto_only, gpointer user);

FtxWsjtxUdp *ftx_wsjtx_udp_new(const char *id, const char *version, const char *revision);
void         ftx_wsjtx_udp_free(FtxWsjtxUdp *u);               /* stops (Close) first */
/* Opens the socket, sends the first Heartbeat and starts the 15 s pulse on
 * the default main context.  Datagrams the server sends back to our port are
 * read there too. */
gboolean     ftx_wsjtx_udp_start(FtxWsjtxUdp *u, const char *host, guint16 port, GError **error);
void         ftx_wsjtx_udp_stop(FtxWsjtxUdp *u);               /* sends Close */
gboolean     ftx_wsjtx_udp_running(const FtxWsjtxUdp *u);
void         ftx_wsjtx_udp_set_highlight_cb(FtxWsjtxUdp *u, FtxWsjtxHighlightCb cb, gpointer user);
void         ftx_wsjtx_udp_set_halt_cb(FtxWsjtxUdp *u, FtxWsjtxHaltCb cb, gpointer user);

void ftx_wsjtx_udp_status(FtxWsjtxUdp *u, const FtxWsjtxStatus *s);
void ftx_wsjtx_udp_decode(FtxWsjtxUdp *u, gboolean is_new, guint32 time_ms, gint32 snr, double dt, guint32 df,
                          const char *mode, const char *msg, gboolean low_conf, gboolean off_air);
void ftx_wsjtx_udp_clear(FtxWsjtxUdp *u);
/* QSO Logged followed by Logged ADIF, as WSJT-X's logQSO does. */
void ftx_wsjtx_udp_qso_logged(FtxWsjtxUdp *u, const FtxWsjtxQso *q, const char *band);
/* Counters for the status bar. */
void ftx_wsjtx_udp_counts(const FtxWsjtxUdp *u, guint *sent, guint *received, guint *errors);

#endif /* FTX_WSJTX_UDP_H */
