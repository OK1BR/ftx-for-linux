/* pskreporter.c — see pskreporter.h.  Ported from WSJT-X v3.0.2
 * Network/PSKReporterIPFIX.cpp and Network/PSKReporter.cpp.
 *
 * Copyright (C) 2001-2026 by Joe Taylor, K1JT, Bill Somerville, G4WJS,
 * Steve Franke, K9AN, Nico Palermo, IV3NWV, Uwe Risse, DG2YCB, Brian Moran,
 * N9ADG, Roger Rehr, W3SZ, John Nelson, G4KLA, Charlie Suckling, DL3WDG,
 * Terrell Deppe, KJ5HST (WSJT-X).  C port Copyright (C) 2026 Richard
 * Fakenberg, OK1BR.  Part of FTX for Linux.  GPL-3.0-or-later.  No warranty.
 */
#include "pskreporter.h"

#include <gio/gio.h>
#include <string.h>

/* ---- PSKReporterIPFIX.cpp ---------------------------------------------------- */

enum {
  CALLSIGN_LIMIT = 32, LOCATOR_LIMIT = 16, MODE_LIMIT = 16,
  PROGRAM_INFO_LIMIT = 80, ANTENNA_LIMIT = 128, RIG_INFORMATION_LIMIT = 128,
};

/* PSK Reporter uses enterprise-specific IPFIX elements under PEN 30351.
 * Template 0x50e3 describes sender/spot records; template 0x50e2 describes
 * receiver metadata, with receiverCallsign as the options-template scope. */
#define PSK_REPORTER_PEN     30351u
#define SENDER_TEMPLATE_ID   0x50e3
#define RECEIVER_TEMPLATE_ID 0x50e2

static int num_pad_bytes(int len) { return (4 - len % 4) % 4; }

static void put_u8(GByteArray *a, guint8 v) { g_byte_array_append(a, &v, 1); }

static void put_u16(GByteArray *a, guint16 v)
{
  const guint8 b[2] = { (guint8)(v >> 8), (guint8)v };
  g_byte_array_append(a, b, 2);
}

static void put_u32(GByteArray *a, guint32 v)
{
  const guint8 b[4] = { (guint8)(v >> 24), (guint8)(v >> 16), (guint8)(v >> 8), (guint8)v };
  g_byte_array_append(a, b, 4);
}

/* writeUtfString over boundedUtf8: a length byte and the UTF-8 text, cut at
 * a character boundary when it is longer than max_bytes */
static void put_string(GByteArray *a, const char *value, int max_bytes)
{
  const char *s = value ? value : "";
  gsize n = strlen(s);
  if (n > (gsize)max_bytes) {
    int bounded = MAX(0, max_bytes);
    while (bounded > 0 && ((guint8)s[bounded] & 0xc0) == 0x80) --bounded;
    n = (gsize)bounded;
  }
  put_u8(a, (guint8)n);
  g_byte_array_append(a, (const guint8 *)s, (guint)n);
}

/* setLength: pad to a multiple of four and store the size in the set's (or
 * the message's) length field at offset 2 */
static void set_length(GByteArray *a)
{
  static const guint8 zero[4] = { 0, 0, 0, 0 };
  const int pad = num_pad_bytes((int)a->len);
  if (pad) g_byte_array_append(a, zero, (guint)pad);
  a->data[2] = (guint8)(a->len >> 8);
  a->data[3] = (guint8)a->len;
}

static void put_field(GByteArray *a, guint16 id, guint16 len)
{
  put_u16(a, (guint16)(0x8000 + id));
  put_u16(a, len);
  put_u32(a, PSK_REPORTER_PEN);
}

static void descriptor_sets(GByteArray *sets)
{
  {
    /* Sender Information template: callsign, frequency, SNR, mode, locator,
     * information source, and spot timestamp. */
    GByteArray *d = g_byte_array_new();
    put_u16(d, 2);
    put_u16(d, 0);
    put_u16(d, SENDER_TEMPLATE_ID);
    put_u16(d, 7);
    put_field(d, 1, 0xffff);     /* senderCallsign                      */
    put_field(d, 5, 5);          /* frequency, 5-byte unsigned integer  */
    put_field(d, 6, 1);          /* sNR                                 */
    put_field(d, 10, 0xffff);    /* mode                                */
    put_field(d, 3, 0xffff);     /* senderLocator                       */
    put_field(d, 11, 1);         /* informationSource                   */
    put_u16(d, 150);             /* dateTimeSeconds                     */
    put_u16(d, 4);
    set_length(d);
    g_byte_array_append(sets, d->data, d->len);
    g_byte_array_unref(d);
  }
  {
    /* Receiver Information options template. Scope Field Count must remain 1:
     * receiverCallsign scopes receiver locator, software, antenna, and rig. */
    GByteArray *d = g_byte_array_new();
    put_u16(d, 3);
    put_u16(d, 0);
    put_u16(d, RECEIVER_TEMPLATE_ID);
    put_u16(d, 5);
    put_u16(d, 1);
    put_field(d, 2, 0xffff);     /* receiverCallsign, scope field       */
    put_field(d, 4, 0xffff);     /* receiverLocator                     */
    put_field(d, 8, 0xffff);     /* decodingSoftware                    */
    put_field(d, 9, 0xffff);     /* antennaInformation                  */
    put_field(d, 13, 0xffff);    /* rigInformation                      */
    set_length(d);
    g_byte_array_append(sets, d->data, d->len);
    g_byte_array_unref(d);
  }
}

static GByteArray *receiver_set(const FtxPskReceiver *r)
{
  GByteArray *d = g_byte_array_new();
  put_u16(d, RECEIVER_TEMPLATE_ID);
  put_u16(d, 0);
  put_string(d, r->callsign, CALLSIGN_LIMIT);
  put_string(d, r->locator, LOCATOR_LIMIT);
  put_string(d, r->program_info, PROGRAM_INFO_LIMIT);
  put_string(d, r->antenna, ANTENNA_LIMIT);
  put_string(d, r->rig_information, RIG_INFORMATION_LIMIT);
  set_length(d);
  return d;
}

static int sender_set_length(int record_bytes)
{
  const int len = 2 * 2 + record_bytes;
  return len + num_pad_bytes(len);
}

static int message_length(int set_bytes)
{
  const int len = 2 * 2 + 3 * 4 + set_bytes;
  return len + num_pad_bytes(len);
}

/* Field order must match the sender template above. */
static void spot_record(GByteArray *d, const FtxPskSpot *s)
{
  put_string(d, s->callsign, CALLSIGN_LIMIT);
  put_u8(d, (guint8)((s->frequency >> 32) & 0xff));
  put_u32(d, (guint32)(s->frequency & 0xffffffffu));
  put_u8(d, (guint8)(gint8)s->snr);
  put_string(d, s->mode, MODE_LIMIT);
  put_string(d, s->locator, LOCATOR_LIMIT);
  put_u8(d, 1);                                   /* informationSource: automatically extracted */
  put_u32(d, (guint32)s->time_s);
}

/* message(base sets + senderSet(records)); records NULL or empty = no sender set */
static GByteArray *message(const GByteArray *base_sets, const GByteArray *records,
                           guint32 sequence_number, guint32 observation_id, guint32 export_time)
{
  GByteArray *d = g_byte_array_new();
  put_u16(d, 10);
  put_u16(d, 0);
  put_u32(d, export_time);
  put_u32(d, sequence_number);
  put_u32(d, observation_id);
  g_byte_array_append(d, base_sets->data, base_sets->len);
  if (records && records->len) {
    GByteArray *s = g_byte_array_new();
    put_u16(s, SENDER_TEMPLATE_ID);
    put_u16(s, 0);
    g_byte_array_append(s, records->data, records->len);
    set_length(s);
    g_byte_array_append(d, s->data, s->len);
    g_byte_array_unref(s);
  }
  set_length(d);
  return d;
}

static void packet_free(gpointer data)
{
  FtxPskPacket *k = data;
  g_byte_array_unref(k->payload);
  g_free(k);
}

static void add_packet(GPtrArray *packets, GByteArray *payload, int spot_count)
{
  FtxPskPacket *k = g_new0(FtxPskPacket, 1);
  k->payload = payload;
  k->spot_count = spot_count;
  g_ptr_array_add(packets, k);
}

GPtrArray *ftx_psk_build_packets(const FtxPskReceiver *receiver, const FtxPskSpot *spots, guint nspots,
                                 gboolean include_descriptors, guint32 sequence_number,
                                 guint32 observation_id, guint32 export_time, int max_payload_bytes)
{
  GPtrArray *packets = g_ptr_array_new_with_free_func(packet_free);
  GByteArray *first_base_sets = g_byte_array_new();
  if (include_descriptors) descriptor_sets(first_base_sets);
  GByteArray *rset = receiver_set(receiver);
  g_byte_array_append(first_base_sets, rset->data, rset->len);

  if (nspots == 0) {
    add_packet(packets, message(first_base_sets, NULL, sequence_number, observation_id, export_time), 0);
  } else {
    GByteArray *records = g_byte_array_new();
    GByteArray *record = g_byte_array_new();
    int record_count = 0;
    const GByteArray *base_sets = first_base_sets;
    for (guint i = 0; i < nspots; i++) {
      g_byte_array_set_size(record, 0);
      spot_record(record, &spots[i]);
      const int candidate_record_bytes = (int)(records->len + record->len);
      const int candidate_payload_size = message_length((int)base_sets->len + sender_set_length(candidate_record_bytes));
      if (record_count > 0 && candidate_payload_size > max_payload_bytes) {
        add_packet(packets, message(base_sets, records, sequence_number, observation_id, export_time), record_count);
        sequence_number += (guint32)record_count;
        g_byte_array_set_size(records, 0);
        record_count = 0;
        /* Follow-on split packets keep receiver data but do not repeat
         * descriptors until the caller schedules the next descriptor send. */
        base_sets = rset;
      }
      g_byte_array_append(records, record->data, record->len);
      ++record_count;
    }
    if (record_count > 0)
      add_packet(packets, message(base_sets, records, sequence_number, observation_id, export_time), record_count);
    g_byte_array_unref(record);
    g_byte_array_unref(records);
  }
  g_byte_array_unref(rset);
  g_byte_array_unref(first_base_sets);
  return packets;
}

/* ---- PSKReporter.cpp ---------------------------------------------------------- */

#define RETRY_US        (30 * G_TIME_SPAN_SECOND)   /* after a failed connection or a closed stream */
#define CACHE_FORGET_S  600

/* WSJT-X's eclipse.txt @ v3.0.2 (from https://eclipse.gsfc.nasa.gov/SEdecade/):
 * all spots go through +/- 6 hours around a solar eclipse, for HamSCI. */
static const char *const eclipse_dates[] = {
  "2023-10-14T18:00:40Z", "2024-04-08T18:18:29Z", "2024-10-02T18:46:13Z", "2025-03-29T10:48:36Z",
  "2025-09-21T19:43:04Z", "2026-02-17T12:13:05Z", "2026-08-12T17:47:05Z", "2027-02-06T16:00:47Z",
  "2027-08-02T10:04:49Z", "2028-01-26T15:08:58Z", "2028-07-22T02:56:39Z", "2029-01-14T17:13:47Z",
  "2029-06-12T04:06:13Z", "2029-07-11T15:37:18Z", "2029-12-05T15:03:57Z", "2030-06-01T06:29:13Z",
  "2030-11-25T06:51:37Z",
};

static gboolean eclipse_active(gint64 now_s)
{
  static gint64 t[G_N_ELEMENTS(eclipse_dates)];
  static gsize once = 0;
  if (g_once_init_enter(&once)) {
    for (gsize i = 0; i < G_N_ELEMENTS(eclipse_dates); i++) {
      GDateTime *d = g_date_time_new_from_iso8601(eclipse_dates[i], NULL);
      t[i] = d ? g_date_time_to_unix(d) : 0;
      if (d) g_date_time_unref(d);
    }
    g_once_init_leave(&once, 1);
  }
  for (gsize i = 0; i < G_N_ELEMENTS(t); i++)
    if (t[i] && ABS(now_s - t[i]) <= 3600 * 6) return TRUE;
  return FALSE;
}

struct out_item { GBytes *bytes; int spots; };

struct _FtxPskReporter {
  int      refs;                 /* the owner + every async operation under way */
  gboolean dead;                 /* freed by the owner                          */
  char    *program_info, *host;
  guint16  port;
  gboolean use_tcp;              /* Options.use_tcpip                           */
  char    *rx_call, *rx_grid, *rx_ant, *rig;
  guint32  sequence_number, observation_id;
  int      send_descriptors;
  guint    flush_counter;
  GArray  *spots;                /* FtxPskSpot, pending                         */
  GHashTable *cache;             /* call → gint64*: when it was last queued     */
  guint    report_timer, descriptor_timer;

  guint    gen;                  /* bumps when the socket is dropped: late async results are ignored */
  gboolean connecting, connected, closing;
  gboolean sock_tcp;             /* the transport of the socket (being) opened  */
  GSocket *sock;                 /* UDP, connected to the collector             */
  GSocketConnection *conn;       /* TCP                                         */
  GSource *watch;                /* TCP: the collector closing the stream       */
  GQueue   out;                  /* struct out_item*, waiting for the TCP stream */
  gboolean writing;
  GCancellable *cancel;
  gint64   retry_at_us;          /* monotonic; no new connection before         */

  guint    spots_sent, packets_sent, errors;
  char    *error;
  gint64   last_send_s;
  FtxPskClock   clock;    gpointer clock_user;
  FtxPskStateCb state_cb; gpointer state_user;
};

struct attempt {                 /* one asynchronous operation */
  FtxPskReporter *p;
  guint gen;
  GSocketAddressEnumerator *en;
  char *err;
};

static void do_send(FtxPskReporter *p, gboolean send_residue);

static gint64 now_s(const FtxPskReporter *p)
{
  return p->clock ? p->clock(p->clock_user) : g_get_real_time() / G_USEC_PER_SEC;
}

static void notify(FtxPskReporter *p) { if (p->state_cb && !p->dead) p->state_cb(p->state_user); }

static void reporter_unref(FtxPskReporter *p)
{
  if (--p->refs > 0) return;
  g_free(p->program_info); g_free(p->host);
  g_free(p->rx_call); g_free(p->rx_grid); g_free(p->rx_ant); g_free(p->rig);
  g_array_unref(p->spots);
  g_hash_table_unref(p->cache);
  g_clear_object(&p->cancel);
  g_free(p->error);
  g_free(p);
}

static struct attempt *attempt_new(FtxPskReporter *p)
{
  struct attempt *a = g_new0(struct attempt, 1);
  a->p = p;
  a->gen = p->gen;
  p->refs++;
  return a;
}

static void attempt_free(struct attempt *a)
{
  g_clear_object(&a->en);
  g_free(a->err);
  reporter_unref(a->p);
  g_free(a);
}

static gboolean attempt_stale(const struct attempt *a) { return a->p->dead || a->gen != a->p->gen; }

static void out_item_free(gpointer data)
{
  struct out_item *o = data;
  g_bytes_unref(o->bytes);
  g_free(o);
}

/* close whatever is open; operations under way finish into a stale generation */
static void drop_socket(FtxPskReporter *p)
{
  p->gen++;
  if (p->cancel) { g_cancellable_cancel(p->cancel); g_clear_object(&p->cancel); }
  if (p->watch) { g_source_destroy(p->watch); g_source_unref(p->watch); p->watch = NULL; }
  if (p->sock) { g_socket_close(p->sock, NULL); g_clear_object(&p->sock); }
  if (p->conn) { g_io_stream_close(G_IO_STREAM(p->conn), NULL, NULL); g_clear_object(&p->conn); }
  g_queue_clear_full(&p->out, out_item_free);
  p->connecting = p->connected = p->closing = p->writing = FALSE;
}

/* handle_socket_error's default branch: the pending spots are given up */
static void fail(FtxPskReporter *p, const char *what)
{
  if (p->errors++ == 0 || !p->error || strcmp(p->error, what) != 0) g_warning("PSK Reporter: %s", what);
  g_free(p->error);
  p->error = g_strdup(what);
  g_array_set_size(p->spots, 0);
  drop_socket(p);
  p->retry_at_us = g_get_monotonic_time() + RETRY_US;
  notify(p);
}

static void sent(FtxPskReporter *p, int spots)
{
  p->packets_sent++;
  p->spots_sent += (guint)spots;
  p->last_send_s = g_get_real_time() / G_USEC_PER_SEC;
  g_clear_pointer(&p->error, g_free);
}

static void on_connected(FtxPskReporter *p)
{
  p->connecting = FALSE;
  p->connected = TRUE;
  g_clear_pointer(&p->error, g_free);
  notify(p);
}

/* ---- TCP ---- */

static gboolean on_tcp_readable(GSocket *sock, GIOCondition cond, gpointer user)
{
  FtxPskReporter *p = user;
  (void)cond;
  gchar buf[256];
  GError *err = NULL;
  const gssize n = g_socket_receive(sock, buf, sizeof buf, NULL, &err);
  if (n > 0) return G_SOURCE_CONTINUE;                       /* the collector has nothing to say; ignore */
  if (n < 0 && g_error_matches(err, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)) { g_clear_error(&err); return G_SOURCE_CONTINUE; }
  /* RemoteHostClosedError: disconnect; the next report opens a new stream */
  if (n == 0) g_message("PSK Reporter: the collector closed the connection");
  else g_message("PSK Reporter: the connection broke: %s", err->message);
  g_clear_error(&err);
  drop_socket(p);
  p->retry_at_us = g_get_monotonic_time() + RETRY_US;
  notify(p);
  return G_SOURCE_REMOVE;
}

static void tcp_pump(FtxPskReporter *p);

static void tcp_written(GObject *src, GAsyncResult *res, gpointer user)
{
  struct attempt *a = user;
  FtxPskReporter *p = a->p;
  GError *err = NULL;
  const gboolean ok = g_output_stream_write_all_finish(G_OUTPUT_STREAM(src), res, NULL, &err);
  if (attempt_stale(a)) { g_clear_error(&err); attempt_free(a); return; }
  p->writing = FALSE;
  struct out_item *o = g_queue_pop_head(&p->out);
  if (!ok) {
    if (o) out_item_free(o);
    fail(p, err ? err->message : "write failed");
    g_clear_error(&err);
  } else {
    if (o) { sent(p, o->spots); out_item_free(o); }
    if (!g_queue_is_empty(&p->out)) tcp_pump(p);
    else if (p->closing) drop_socket(p);                     /* disconnectFromHost once everything is out */
    notify(p);
  }
  attempt_free(a);
}

static void tcp_pump(FtxPskReporter *p)
{
  if (p->writing || !p->conn) return;
  struct out_item *o = g_queue_peek_head(&p->out);
  if (!o) return;
  p->writing = TRUE;
  gsize len = 0;
  gconstpointer data = g_bytes_get_data(o->bytes, &len);
  g_output_stream_write_all_async(g_io_stream_get_output_stream(G_IO_STREAM(p->conn)), data, len,
                                  G_PRIORITY_DEFAULT, p->cancel, tcp_written, attempt_new(p));
}

static void tcp_connected(GObject *src, GAsyncResult *res, gpointer user)
{
  struct attempt *a = user;
  FtxPskReporter *p = a->p;
  GError *err = NULL;
  GSocketConnection *c = g_socket_client_connect_finish(G_SOCKET_CLIENT(src), res, &err);
  if (attempt_stale(a)) {
    if (c) { g_io_stream_close(G_IO_STREAM(c), NULL, NULL); g_object_unref(c); }
  } else if (!c) {
    fail(p, err ? err->message : "connection failed");
  } else {
    p->conn = c;
    GSocket *s = g_socket_connection_get_socket(c);
    /* the connect timeout stays on the socket GSocketClient hands over, and a
     * GSocket source fires once that long passes without traffic: off, or the
     * watch below takes 20 quiet seconds for the collector closing the stream */
    g_socket_set_timeout(s, 0);
    p->watch = g_socket_create_source(s, G_IO_IN | G_IO_HUP | G_IO_ERR, NULL);
    g_source_set_callback(p->watch, G_SOURCE_FUNC(on_tcp_readable), p, NULL);
    g_source_attach(p->watch, NULL);
    on_connected(p);
  }
  g_clear_error(&err);
  attempt_free(a);
}

/* ---- UDP ---- */

static void udp_address(GObject *src, GAsyncResult *res, gpointer user);

static void udp_next(struct attempt *a)
{
  g_socket_address_enumerator_next_async(a->en, a->p->cancel, udp_address, a);
}

/* the collector's addresses one after another until a socket connects (the
 * "pseudo connection" of QUdpSocket::connectToHost) */
static void udp_address(GObject *src, GAsyncResult *res, gpointer user)
{
  (void)src;
  struct attempt *a = user;
  FtxPskReporter *p = a->p;
  GError *err = NULL;
  GSocketAddress *addr = g_socket_address_enumerator_next_finish(a->en, res, &err);
  if (attempt_stale(a)) {
    g_clear_object(&addr);
    g_clear_error(&err);
    attempt_free(a);
    return;
  }
  if (!addr) {
    fail(p, err ? err->message : a->err ? a->err : "no address for the collector");
    g_clear_error(&err);
    attempt_free(a);
    return;
  }
  GSocket *s = g_socket_new(g_socket_address_get_family(addr), G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, &err);
  if (s && g_socket_connect(s, addr, NULL, &err)) {
    g_socket_set_blocking(s, FALSE);
    p->sock = s;
    g_object_unref(addr);
    on_connected(p);
    attempt_free(a);
    return;
  }
  g_clear_object(&s);
  g_free(a->err);
  a->err = g_strdup(err ? err->message : "socket failed");
  g_clear_error(&err);
  g_object_unref(addr);
  udp_next(a);
}

/* ---- connection ---- */

static gboolean on_report_timer(gpointer user)
{
  do_send(user, FALSE);
  return G_SOURCE_CONTINUE;
}

/* UDP has no session state, so templates are repeated periodically in case
 * the collector restarted and lost its template cache. */
static gboolean on_descriptor_timer(gpointer user)
{
  FtxPskReporter *p = user;
  if ((p->connected || p->connecting) && !p->sock_tcp) p->send_descriptors = 3;   /* three times */
  return G_SOURCE_CONTINUE;
}

static void reconnect(FtxPskReporter *p)
{
  if (!p->report_timer) p->report_timer = g_timeout_add_seconds(FTX_PSK_MIN_SEND_INTERVAL + 1, on_report_timer, p);
  if (!p->descriptor_timer) p->descriptor_timer = g_timeout_add_seconds(60 * 60, on_descriptor_timer, p);   /* hourly */
  if (g_get_monotonic_time() < p->retry_at_us) return;
  p->sock_tcp = p->use_tcp;
  p->send_descriptors = p->use_tcp ? 1 : 3;
  p->connecting = TRUE;
  g_clear_object(&p->cancel);
  p->cancel = g_cancellable_new();
  GSocketConnectable *to = g_network_address_new(p->host ? p->host : FTX_PSK_HOST, p->port ? p->port : FTX_PSK_PORT);
  struct attempt *a = attempt_new(p);
  if (p->use_tcp) {
    GSocketClient *client = g_socket_client_new();
    g_socket_client_set_timeout(client, FTX_PSK_CONNECT_TIMEOUT);
    g_socket_client_set_enable_proxy(client, FALSE);   /* straight to the collector, as the UDP datagrams go */
    g_socket_client_connect_async(client, to, p->cancel, tcp_connected, a);
    g_object_unref(client);
  } else {
    a->en = g_socket_connectable_enumerate(to);
    udp_next(a);
  }
  g_object_unref(to);
}

static void check_connection(FtxPskReporter *p)
{
  if (p->connected || p->connecting) {
    p->closing = FALSE;                            /* in use again before the last report was out */
    if (p->sock_tcp == p->use_tcp) return;
    drop_socket(p);                                /* the other transport: the pending spots go out on the new socket */
  }
  reconnect(p);
}

static gboolean flushing(FtxPskReporter *p)
{
  return FTX_PSK_FLUSH_INTERVAL && !(++p->flush_counter % FTX_PSK_FLUSH_INTERVAL);
}

static void do_send(FtxPskReporter *p, gboolean send_residue)
{
  check_connection(p);
  if (!p->connected) return;

  const gboolean flush = flushing(p) || send_residue;
  if (!p->spots->len && !flush && !p->send_descriptors) return;

  const gboolean include_descriptors = p->send_descriptors > 0;
  if (include_descriptors) --p->send_descriptors;

  const FtxPskReceiver rx = { p->rx_call, p->rx_grid, p->program_info, p->rx_ant, p->rig };
  GPtrArray *packets = ftx_psk_build_packets(&rx, (const FtxPskSpot *)p->spots->data, p->spots->len,
                                             include_descriptors, p->sequence_number, p->observation_id,
                                             (guint32)now_s(p),
                                             p->sock_tcp ? FTX_PSK_MAX_TCP_PAYLOAD : FTX_PSK_MAX_UDP_PAYLOAD);
  g_array_set_size(p->spots, 0);
  for (guint i = 0; i < packets->len; i++) {
    const FtxPskPacket *k = g_ptr_array_index(packets, i);
    p->sequence_number += (guint32)k->spot_count;
    if (p->sock_tcp) {
      struct out_item *o = g_new0(struct out_item, 1);
      o->bytes = g_bytes_new(k->payload->data, k->payload->len);
      o->spots = k->spot_count;
      g_queue_push_tail(&p->out, o);
    } else {
      GError *err = NULL;
      if (g_socket_send(p->sock, (const gchar *)k->payload->data, k->payload->len, NULL, &err) < 0) {
        fail(p, err ? err->message : "send failed");
        g_clear_error(&err);
        break;
      }
      sent(p, k->spot_count);
    }
  }
  g_ptr_array_unref(packets);
  if (p->connected && p->sock_tcp) tcp_pump(p);
  notify(p);
}

/* ---- public ---- */

FtxPskReporter *ftx_pskreporter_new(const char *program_info)
{
  FtxPskReporter *p = g_new0(FtxPskReporter, 1);
  p->refs = 1;
  p->program_info = g_strdup(program_info ? program_info : "");
  p->observation_id = g_random_int();
  p->spots = g_array_new(FALSE, TRUE, sizeof(FtxPskSpot));
  p->cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  g_queue_init(&p->out);
  return p;
}

static void stop(FtxPskReporter *p)
{
  if (p->report_timer) { g_source_remove(p->report_timer); p->report_timer = 0; }
  if (p->descriptor_timer) { g_source_remove(p->descriptor_timer); p->descriptor_timer = 0; }
  if (p->connected && p->sock_tcp && (p->writing || !g_queue_is_empty(&p->out))) p->closing = TRUE;
  else drop_socket(p);
}

void ftx_pskreporter_free(FtxPskReporter *p)
{
  if (!p) return;
  stop(p);
  drop_socket(p);
  p->dead = TRUE;
  reporter_unref(p);
}

void ftx_pskreporter_set_server(FtxPskReporter *p, const char *host, guint16 port)
{
  g_free(p->host);
  p->host = host && host[0] ? g_strdup(host) : NULL;
  p->port = port;
}

void ftx_pskreporter_set_tcp(FtxPskReporter *p, gboolean use_tcp) { p->use_tcp = use_tcp != FALSE; }

void ftx_pskreporter_set_state_cb(FtxPskReporter *p, FtxPskStateCb cb, gpointer user) { p->state_cb = cb; p->state_user = user; }

void ftx_pskreporter_set_clock(FtxPskReporter *p, FtxPskClock clock, gpointer user) { p->clock = clock; p->clock_user = user; }

static void set_str(char **dst, const char *v)
{
  if (g_strcmp0(*dst, v ? v : "") == 0) return;
  g_free(*dst);
  *dst = g_strdup(v ? v : "");
}

void ftx_pskreporter_set_local(FtxPskReporter *p, const char *call, const char *grid,
                               const char *antenna, const char *rig_information)
{
  check_connection(p);
  set_str(&p->rx_call, call);
  set_str(&p->rx_grid, grid);
  set_str(&p->rx_ant, antenna);
  set_str(&p->rig, rig_information);
}

static gboolean cache_stale(gpointer key, gpointer value, gpointer user)
{
  (void)key;
  return *(const gint64 *)user - *(const gint64 *)value > CACHE_FORGET_S;
}

gboolean ftx_pskreporter_add(FtxPskReporter *p, const char *call, const char *grid, guint64 freq_hz,
                             const char *mode, int snr, gint64 time_s)
{
  check_connection(p);
  /* remove any earlier spots of this call to reduce pskreporter load */
  gint64 now = now_s(p);
  gint64 *seen = g_hash_table_lookup(p->cache, call);
  gboolean queue = FALSE;
  /* We allow all spots through +/- 6 hours around an eclipse for the HamSCI group. */
  if (!seen || freq_hz > 49000000 || eclipse_active(now)) {
    queue = TRUE;
    gint64 *t = g_new(gint64, 1);
    *t = now;
    g_hash_table_replace(p->cache, g_strdup(call), t);
  } else if (now - *seen > FTX_PSK_CACHE_TIMEOUT) {
    queue = TRUE;
    *seen = now;
  }
  if (queue) {
    FtxPskSpot s;
    memset(&s, 0, sizeof s);
    g_strlcpy(s.callsign, call, sizeof s.callsign);
    g_strlcpy(s.locator, grid ? grid : "", sizeof s.locator);
    g_strlcpy(s.mode, mode ? mode : "", sizeof s.mode);
    s.snr = snr;
    s.frequency = freq_hz;
    s.time_s = time_s;
    g_array_append_val(p->spots, s);
  }

  gboolean accepted = TRUE;
  while (p->spots->len > FTX_PSK_MAX_PENDING_SPOTS) {
    g_array_remove_index(p->spots, 0);
    accepted = FALSE;
  }
  g_hash_table_foreach_remove(p->cache, cache_stale, &now);
  if (queue) notify(p);
  return accepted;
}

void ftx_pskreporter_send(FtxPskReporter *p, gboolean last)
{
  check_connection(p);
  if (p->connected) do_send(p, TRUE);
  if (last) {
    stop(p);
    notify(p);
  }
}

void ftx_pskreporter_get_state(const FtxPskReporter *p, FtxPskState *st)
{
  st->active = p->report_timer != 0;
  st->connected = p->connected;
  st->tcp = p->sock_tcp;
  st->spots_sent = p->spots_sent;
  st->packets_sent = p->packets_sent;
  st->pending = p->spots->len;
  st->errors = p->errors;
  st->error = p->error;
  st->last_send_s = p->last_send_s;
}
