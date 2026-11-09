/* wsjtx_udp.c — see wsjtx_udp.h.  Ported from WSJT-X Network/NetworkMessage.hpp,
 * Network/MessageClient.cpp and logbook/logbook.cpp (QSOToADIF) @ v3.0.2.
 * Copyright (C) 2001-2026 the WSJT-X authors (see the header); C port
 * Copyright (C) 2026 Richard Fakenberg, OK1BR.  GPL-3.0-or-later. */
#include "wsjtx_udp.h"

#include <gio/gio.h>
#include <string.h>

/* ---- writers (QDataStream, big-endian) --------------------------------------- */

static void wr_u8(GByteArray *a, guint8 v) { g_byte_array_append(a, &v, 1); }
static void wr_u16(GByteArray *a, guint16 v) { guint8 b[2] = { (guint8)(v >> 8), (guint8)v }; g_byte_array_append(a, b, 2); }
static void wr_u32(GByteArray *a, guint32 v)
{
  guint8 b[4] = { (guint8)(v >> 24), (guint8)(v >> 16), (guint8)(v >> 8), (guint8)v };
  g_byte_array_append(a, b, 4);
}
static void wr_u64(GByteArray *a, guint64 v) { wr_u32(a, (guint32)(v >> 32)); wr_u32(a, (guint32)v); }
static void wr_i32(GByteArray *a, gint32 v) { wr_u32(a, (guint32)v); }
static void wr_i64(GByteArray *a, gint64 v) { wr_u64(a, (guint64)v); }
static void wr_bool(GByteArray *a, gboolean v) { wr_u8(a, v ? 1 : 0); }
static void wr_f64(GByteArray *a, double d)
{
  union { double d; guint64 u; } x; x.d = d;
  wr_u64(a, x.u);
}
static void wr_utf8(GByteArray *a, const char *s)
{
  if (!s) { wr_u32(a, 0xffffffffU); return; }
  gsize n = strlen(s);
  wr_u32(a, (guint32)n);
  if (n) g_byte_array_append(a, (const guint8 *)s, (guint)n);
}
/* QDateTime: Julian day (qint64), ms since midnight (quint32), timespec 1 = UTC */
gint64 ftx_wsjtx_julian_day(gint64 unix_s)
{
  gint64 days = unix_s >= 0 ? unix_s / 86400 : -((-unix_s + 86399) / 86400);
  return 2440588 + days;                      /* 1970-01-01 = JD 2440588 */
}
static void wr_qdatetime_utc(GByteArray *a, gint64 unix_s)
{
  gint64 jd = ftx_wsjtx_julian_day(unix_s);
  gint64 secs = unix_s - (jd - 2440588) * 86400;
  wr_i64(a, jd);
  wr_u32(a, (guint32)(secs * 1000));
  wr_u8(a, 1);
}
static GByteArray *begin_msg(guint32 type, const char *id)
{
  GByteArray *a = g_byte_array_sized_new(160);
  wr_u32(a, FTX_WSJTX_MAGIC);
  wr_u32(a, FTX_WSJTX_SCHEMA);
  wr_u32(a, type);
  wr_utf8(a, id ? id : "FTX for Linux");
  return a;
}

/* ---- builders ----------------------------------------------------------------- */

GByteArray *ftx_wsjtx_build_heartbeat(const char *id, const char *version, const char *revision)
{
  GByteArray *a = begin_msg(FTX_WSJTX_MSG_HEARTBEAT, id);
  wr_u32(a, FTX_WSJTX_SCHEMA);                /* maximum schema number accepted */
  wr_utf8(a, version ? version : "");
  wr_utf8(a, revision ? revision : "");
  return a;
}

GByteArray *ftx_wsjtx_build_status(const char *id, const FtxWsjtxStatus *s)
{
  GByteArray *a = begin_msg(FTX_WSJTX_MSG_STATUS, id);
  wr_u64(a, s->dial_hz);
  wr_utf8(a, s->mode ? s->mode : "");
  wr_utf8(a, s->dx_call ? s->dx_call : "");
  wr_utf8(a, s->report ? s->report : "");
  wr_utf8(a, s->tx_mode ? s->tx_mode : "");
  wr_bool(a, s->tx_enabled);
  wr_bool(a, s->transmitting);
  wr_bool(a, s->decoding);
  wr_u32(a, s->rx_df);
  wr_u32(a, s->tx_df);
  wr_utf8(a, s->de_call ? s->de_call : "");
  wr_utf8(a, s->de_grid ? s->de_grid : "");
  wr_utf8(a, s->dx_grid ? s->dx_grid : "");
  wr_bool(a, s->watchdog);
  wr_utf8(a, s->sub_mode ? s->sub_mode : "");
  wr_bool(a, s->fast_mode);
  wr_u8(a, s->special_op);
  wr_u32(a, s->freq_tol);
  wr_u32(a, s->tr_period);
  wr_utf8(a, s->config_name ? s->config_name : "");
  wr_utf8(a, s->tx_message ? s->tx_message : "");
  return a;
}

GByteArray *ftx_wsjtx_build_decode(const char *id, gboolean is_new, guint32 time_ms, gint32 snr, double dt,
                                   guint32 df, const char *mode, const char *msg, gboolean low_conf, gboolean off_air)
{
  GByteArray *a = begin_msg(FTX_WSJTX_MSG_DECODE, id);
  wr_bool(a, is_new);
  wr_u32(a, time_ms);
  wr_i32(a, snr);
  wr_f64(a, dt);
  wr_u32(a, df);
  wr_utf8(a, mode ? mode : "");
  wr_utf8(a, msg ? msg : "");
  wr_bool(a, low_conf);
  wr_bool(a, off_air);
  return a;
}

GByteArray *ftx_wsjtx_build_clear(const char *id) { return begin_msg(FTX_WSJTX_MSG_CLEAR, id); }
GByteArray *ftx_wsjtx_build_close(const char *id) { return begin_msg(FTX_WSJTX_MSG_CLOSE, id); }

GByteArray *ftx_wsjtx_build_qso_logged(const char *id, const FtxWsjtxQso *q)
{
  GByteArray *a = begin_msg(FTX_WSJTX_MSG_QSO_LOGGED, id);
  wr_qdatetime_utc(a, q->off_s);
  wr_utf8(a, q->dx_call ? q->dx_call : "");
  wr_utf8(a, q->dx_grid ? q->dx_grid : "");
  wr_u64(a, q->freq_hz);
  wr_utf8(a, q->mode ? q->mode : "");
  wr_utf8(a, q->rst_sent ? q->rst_sent : "");
  wr_utf8(a, q->rst_rcvd ? q->rst_rcvd : "");
  wr_utf8(a, q->tx_power ? q->tx_power : "");
  wr_utf8(a, q->comments ? q->comments : "");
  wr_utf8(a, q->name ? q->name : "");
  wr_qdatetime_utc(a, q->on_s);
  wr_utf8(a, q->op_call ? q->op_call : "");
  wr_utf8(a, q->my_call ? q->my_call : "");
  wr_utf8(a, q->my_grid ? q->my_grid : "");
  wr_utf8(a, q->exch_sent ? q->exch_sent : "");
  wr_utf8(a, q->exch_rcvd ? q->exch_rcvd : "");
  wr_utf8(a, q->prop_mode ? q->prop_mode : "");
  wr_utf8(a, "");                             /* satellite  (3.0.x) */
  wr_utf8(a, "");                             /* sat mode   (3.0.x) */
  wr_utf8(a, "");                             /* freq Rx    (3.0.x) */
  return a;
}

GByteArray *ftx_wsjtx_build_logged_adif(const char *id, const char *adif_text)
{
  GByteArray *a = begin_msg(FTX_WSJTX_MSG_LOGGED_ADIF, id);
  wr_utf8(a, adif_text ? adif_text : "");
  return a;
}

/* LogBook::QSOToADIF: "<call:5>G7BTP <gridsquare:4>IO93 <mode:4>MFSK <submode:3>FT4 …"
 * — FT4 (and FST4, Q65) go as MFSK + submode, the optional fields only when set. */
static void adif_field(GString *t, const char *name, const char *val)
{
  g_string_append_printf(t, "%s<%s:%d>%s", t->len ? " " : "", name, (int)strlen(val), val);
}
char *ftx_wsjtx_adif_record(const FtxWsjtxQso *q, const char *band)
{
  GString *t = g_string_new(NULL);
  const char *mode = q->mode ? q->mode : "";
  adif_field(t, "call", q->dx_call ? q->dx_call : "");
  adif_field(t, "gridsquare", q->dx_grid ? q->dx_grid : "");
  if (strcmp(mode, "FT4") != 0 && strcmp(mode, "FST4") != 0 && strcmp(mode, "Q65") != 0) {
    adif_field(t, "mode", mode);
  } else {
    adif_field(t, "mode", "MFSK");
    adif_field(t, "submode", mode);
  }
  adif_field(t, "rst_sent", q->rst_sent ? q->rst_sent : "");
  adif_field(t, "rst_rcvd", q->rst_rcvd ? q->rst_rcvd : "");
  GDateTime *on = g_date_time_new_from_unix_utc(q->on_s), *off = g_date_time_new_from_unix_utc(q->off_s);
  char *s;
  s = g_date_time_format(on, "%Y%m%d");  adif_field(t, "qso_date", s);     g_free(s);
  s = g_date_time_format(on, "%H%M%S");  adif_field(t, "time_on", s);      g_free(s);
  s = g_date_time_format(off, "%Y%m%d"); adif_field(t, "qso_date_off", s); g_free(s);
  s = g_date_time_format(off, "%H%M%S"); adif_field(t, "time_off", s);     g_free(s);
  g_date_time_unref(on); g_date_time_unref(off);
  adif_field(t, "band", band ? band : "");
  char mhz[32];
  g_ascii_formatd(mhz, sizeof mhz, "%.6f", (double)q->freq_hz / 1e6);
  adif_field(t, "freq", mhz);
  adif_field(t, "station_callsign", q->my_call ? q->my_call : "");
  if (q->my_grid && q->my_grid[0]) adif_field(t, "my_gridsquare", q->my_grid);
  if (q->tx_power && q->tx_power[0]) adif_field(t, "tx_pwr", q->tx_power);
  if (q->comments && q->comments[0]) adif_field(t, "comment", q->comments);
  if (q->name && q->name[0]) adif_field(t, "name", q->name);
  if (q->op_call && q->op_call[0]) adif_field(t, "operator", q->op_call);
  if (q->prop_mode && q->prop_mode[0]) adif_field(t, "prop_mode", q->prop_mode);
  return g_string_free(t, FALSE);
}

char *ftx_wsjtx_adif_text(const FtxWsjtxQso *q, const char *band, const char *programid)
{
  char *rec = ftx_wsjtx_adif_record(q, band);
  const char *pid = programid ? programid : "FTX for Linux";
  char *t = g_strdup_printf("\n<adif_ver:5>3.1.0\n<programid:%d>%s\n<EOH>\n%s <EOR>", (int)strlen(pid), pid, rec);
  g_free(rec);
  return t;
}

/* ---- readers ------------------------------------------------------------------ */

typedef struct { const guint8 *d; gsize n, i; } Cur;
static gboolean rd_u8(Cur *c, guint8 *o) { if (c->i + 1 > c->n) return FALSE; *o = c->d[c->i++]; return TRUE; }
static gboolean rd_u16(Cur *c, guint16 *o)
{
  if (c->i + 2 > c->n) return FALSE;
  *o = (guint16)((c->d[c->i] << 8) | c->d[c->i + 1]); c->i += 2; return TRUE;
}
static gboolean rd_u32(Cur *c, guint32 *o)
{
  if (c->i + 4 > c->n) return FALSE;
  *o = ((guint32)c->d[c->i] << 24) | ((guint32)c->d[c->i + 1] << 16) | ((guint32)c->d[c->i + 2] << 8) | c->d[c->i + 3];
  c->i += 4; return TRUE;
}
static gboolean rd_utf8(Cur *c, char **o)
{
  guint32 n;
  if (!rd_u32(c, &n)) return FALSE;
  if (n == 0xffffffffU) { *o = g_strdup(""); return TRUE; }
  if (c->i + n > c->n) return FALSE;
  *o = g_strndup((const char *)c->d + c->i, n);
  c->i += n;
  return TRUE;
}
static gboolean rd_qcolor(Cur *c, gboolean *valid, guint8 rgb[3])
{
  guint8 spec; guint16 a, r, g, b, pad;
  if (!rd_u8(c, &spec)) return FALSE;
  *valid = spec != 0;
  rgb[0] = rgb[1] = rgb[2] = 0;
  if (spec == 0) return TRUE;
  if (!rd_u16(c, &a) || !rd_u16(c, &r) || !rd_u16(c, &g) || !rd_u16(c, &b) || !rd_u16(c, &pad)) return FALSE;
  rgb[0] = (guint8)(r >> 8); rgb[1] = (guint8)(g >> 8); rgb[2] = (guint8)(b >> 8);
  return TRUE;
}

gboolean ftx_wsjtx_parse_header(const guint8 *data, gsize len, guint32 *type, char **id, gsize *payload_off)
{
  Cur c = { data, len, 0 };
  guint32 magic, schema;
  if (!rd_u32(&c, &magic) || magic != FTX_WSJTX_MAGIC) return FALSE;
  if (!rd_u32(&c, &schema) || schema < 2) return FALSE;
  if (!rd_u32(&c, type)) return FALSE;
  char *s = NULL;
  if (!rd_utf8(&c, &s)) return FALSE;
  if (id) *id = s; else g_free(s);
  if (payload_off) *payload_off = c.i;
  return TRUE;
}

gboolean ftx_wsjtx_parse_highlight(const guint8 *data, gsize len, char **id, char **call,
                                   gboolean *bg_valid, guint8 bg_rgb[3],
                                   gboolean *fg_valid, guint8 fg_rgb[3], gboolean *last_only)
{
  guint32 type; gsize off; char *hid = NULL;
  if (!ftx_wsjtx_parse_header(data, len, &type, &hid, &off)) return FALSE;
  if (type != FTX_WSJTX_MSG_HIGHLIGHT) { g_free(hid); return FALSE; }
  Cur c = { data, len, off };
  char *cs = NULL; gboolean bv = FALSE, fv = FALSE; guint8 brgb[3], frgb[3]; guint8 last = 0;
  if (!rd_utf8(&c, &cs) || !rd_qcolor(&c, &bv, brgb) || !rd_qcolor(&c, &fv, frgb) || !rd_u8(&c, &last)) {
    g_free(hid); g_free(cs); return FALSE;
  }
  if (id) *id = hid; else g_free(hid);
  if (call) *call = cs; else g_free(cs);
  if (bg_valid) *bg_valid = bv;
  if (bg_rgb) memcpy(bg_rgb, brgb, 3);
  if (fg_valid) *fg_valid = fv;
  if (fg_rgb) memcpy(fg_rgb, frgb, 3);
  if (last_only) *last_only = last != 0;
  return TRUE;
}

/* ---- the client ----------------------------------------------------------------- */

struct _FtxWsjtxUdp {
  char *id, *version, *revision;
  GSocket        *sock;
  GSource        *src;
  GSocketAddress *server;
  guint           pulse_id;
  gboolean        running;
  guint           sent, received, errors;
  FtxWsjtxHighlightCb hl_cb; gpointer hl_user;
  FtxWsjtxHaltCb      halt_cb; gpointer halt_user;
};

FtxWsjtxUdp *ftx_wsjtx_udp_new(const char *id, const char *version, const char *revision)
{
  FtxWsjtxUdp *u = g_new0(FtxWsjtxUdp, 1);
  u->id = g_strdup(id ? id : "FTX for Linux");
  u->version = g_strdup(version ? version : "");
  u->revision = g_strdup(revision ? revision : "");
  return u;
}

static void send_bytes(FtxWsjtxUdp *u, GByteArray *a)
{
  if (u->running && u->sock && u->server) {
    GError *err = NULL;
    if (g_socket_send_to(u->sock, u->server, (const gchar *)a->data, a->len, NULL, &err) < 0) {
      if (u->errors++ == 0) g_warning("wsjtx udp: send: %s", err ? err->message : "?");
      g_clear_error(&err);
    } else {
      u->sent++;
    }
  }
  g_byte_array_unref(a);
}

static gboolean on_readable(GSocket *sock, GIOCondition cond, gpointer user)
{
  FtxWsjtxUdp *u = user;
  (void)cond;
  guint8 buf[4096];
  GError *err = NULL;
  gssize n = g_socket_receive_from(sock, NULL, (gchar *)buf, sizeof buf, NULL, &err);
  if (n < 0) { g_clear_error(&err); return G_SOURCE_CONTINUE; }
  u->received++;
  guint32 type; gsize off; char *id = NULL;
  if (!ftx_wsjtx_parse_header(buf, (gsize)n, &type, &id, &off)) return G_SOURCE_CONTINUE;
  g_free(id);
  if (type == FTX_WSJTX_MSG_HIGHLIGHT) {
    char *call = NULL; gboolean bv, fv, last; guint8 brgb[3], frgb[3];
    if (ftx_wsjtx_parse_highlight(buf, (gsize)n, NULL, &call, &bv, brgb, &fv, frgb, &last)) {
      if (u->hl_cb) u->hl_cb(call, bv, brgb, last, u->hl_user);
      g_free(call);
    }
  } else if (type == FTX_WSJTX_MSG_HALT_TX) {
    Cur c = { buf, (gsize)n, off };
    guint8 auto_only = 0;
    rd_u8(&c, &auto_only);
    if (u->halt_cb) u->halt_cb(auto_only != 0, u->halt_user);
  }
  return G_SOURCE_CONTINUE;
}

static gboolean on_pulse(gpointer user)
{
  FtxWsjtxUdp *u = user;
  send_bytes(u, ftx_wsjtx_build_heartbeat(u->id, u->version, u->revision));
  return G_SOURCE_CONTINUE;
}

gboolean ftx_wsjtx_udp_start(FtxWsjtxUdp *u, const char *host, guint16 port, GError **error)
{
  ftx_wsjtx_udp_stop(u);
  GInetAddress *ia = g_inet_address_new_from_string(host && *host ? host : "127.0.0.1");
  if (!ia) {
    /* a host name: resolve once, synchronously (the logger is on the LAN) */
    GResolver *r = g_resolver_get_default();
    GList *l = g_resolver_lookup_by_name(r, host, NULL, error);
    g_object_unref(r);
    if (!l) return FALSE;
    ia = g_object_ref(l->data);
    g_resolver_free_addresses(l);
  }
  u->server = g_inet_socket_address_new(ia, port ? port : FTX_WSJTX_DEFAULT_PORT);
  GSocketFamily fam = g_inet_address_get_family(ia);
  g_object_unref(ia);
  u->sock = g_socket_new(fam, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, error);
  if (!u->sock) { g_clear_object(&u->server); return FALSE; }
  g_socket_set_blocking(u->sock, FALSE);
  u->src = g_socket_create_source(u->sock, G_IO_IN, NULL);
  g_source_set_callback(u->src, (GSourceFunc)on_readable, u, NULL);
  g_source_attach(u->src, NULL);
  u->running = TRUE;
  on_pulse(u);                                  /* the initial heartbeat negotiates the schema */
  u->pulse_id = g_timeout_add_seconds(FTX_WSJTX_PULSE_S, on_pulse, u);
  return TRUE;
}

void ftx_wsjtx_udp_stop(FtxWsjtxUdp *u)
{
  if (u->running) send_bytes(u, ftx_wsjtx_build_close(u->id));
  u->running = FALSE;
  if (u->pulse_id) { g_source_remove(u->pulse_id); u->pulse_id = 0; }
  if (u->src) { g_source_destroy(u->src); g_source_unref(u->src); u->src = NULL; }
  if (u->sock) { g_socket_close(u->sock, NULL); g_clear_object(&u->sock); }
  g_clear_object(&u->server);
}

gboolean ftx_wsjtx_udp_running(const FtxWsjtxUdp *u) { return u && u->running; }

void ftx_wsjtx_udp_free(FtxWsjtxUdp *u)
{
  if (!u) return;
  ftx_wsjtx_udp_stop(u);
  g_free(u->id); g_free(u->version); g_free(u->revision);
  g_free(u);
}

void ftx_wsjtx_udp_set_highlight_cb(FtxWsjtxUdp *u, FtxWsjtxHighlightCb cb, gpointer user) { u->hl_cb = cb; u->hl_user = user; }
void ftx_wsjtx_udp_set_halt_cb(FtxWsjtxUdp *u, FtxWsjtxHaltCb cb, gpointer user) { u->halt_cb = cb; u->halt_user = user; }

void ftx_wsjtx_udp_status(FtxWsjtxUdp *u, const FtxWsjtxStatus *s)
{
  if (!u->running) return;
  send_bytes(u, ftx_wsjtx_build_status(u->id, s));
}

void ftx_wsjtx_udp_decode(FtxWsjtxUdp *u, gboolean is_new, guint32 time_ms, gint32 snr, double dt, guint32 df,
                          const char *mode, const char *msg, gboolean low_conf, gboolean off_air)
{
  if (!u->running) return;
  send_bytes(u, ftx_wsjtx_build_decode(u->id, is_new, time_ms, snr, dt, df, mode, msg, low_conf, off_air));
}

void ftx_wsjtx_udp_clear(FtxWsjtxUdp *u)
{
  if (!u->running) return;
  send_bytes(u, ftx_wsjtx_build_clear(u->id));
}

void ftx_wsjtx_udp_qso_logged(FtxWsjtxUdp *u, const FtxWsjtxQso *q, const char *band)
{
  if (!u->running) return;
  send_bytes(u, ftx_wsjtx_build_qso_logged(u->id, q));
  char *adif = ftx_wsjtx_adif_text(q, band, u->id);
  send_bytes(u, ftx_wsjtx_build_logged_adif(u->id, adif));
  g_free(adif);
}

void ftx_wsjtx_udp_counts(const FtxWsjtxUdp *u, guint *sent, guint *received, guint *errors)
{
  if (sent) *sent = u->sent;
  if (received) *received = u->received;
  if (errors) *errors = u->errors;
}
