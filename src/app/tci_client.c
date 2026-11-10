/* tci_client.c — TCI WebSocket client for RX audio (see tci_client.h).
 * Copyright (C) 2026 Richard Fakenberg, OK1BR.  GPL-3.0-or-later. */
#include "tci_client.h"

#include <libwebsockets.h>
#include <stdlib.h>
#include <string.h>

#define FTX_TCI_ERROR       (g_quark_from_static_string("ftx-tci-error"))
#define HANDSHAKE_TIMEOUT_S 5
#define STOP_FLUSH_MS       300

/* Binary Stream header: 16 x u32 (TCI spec / sdr-for-linux tci_server.c):
 * [0] receiver [1] sample rate [2] format (3 = float32, 0 = int16) [3] codec
 * [4] crc [5] length in scalar samples [6] type (1 = RX audio) [7] channels */
#define STREAM_HDR_BYTES   64
#define STREAM_TYPE_RXAUD  1
#define STREAM_TYPE_TXAUD  2
#define STREAM_FMT_INT16   0
#define STREAM_FMT_FLOAT   3

#define TCI_DEFAULT_PORT 40001
#define TCI_DEFAULT_TCP  "ws://127.0.0.1:40001"

struct endpoint {
  gboolean unix_sock;
  char     path[108];         /* sun_path */
  char     host[128];
  guint16  port;
};

struct _FtxTciClient {
  GPtrArray *order;           /* addresses to try, in order (char*)          */
  char      *bad;             /* an address that did not parse, or NULL      */
  char      *endpoint;        /* the address in use / last tried             */
  char      *dial;            /* what lws dials: "+/path" or the host        */

  FtxTciAudioCb  audio_cb;  gpointer audio_cb_data;
  FtxTciVfoCb    vfo_cb;    gpointer vfo_cb_data;
  FtxTciModeCb   mode_cb;   gpointer mode_cb_data;
  FtxTciTxCb     tx_cb;     gpointer tx_cb_data;
  FtxTciClosedCb closed_cb; gpointer closed_cb_data;

  struct lws_context *ctx;
  struct lws         *wsi;
  GThread            *thread;

  GMutex   lock;              /* guards everything below                     */
  GCond    cond;
  gboolean up, ready, failed;
  GQueue   out;               /* outgoing messages (struct outmsg*)          */
  double   vfo_hz;
  int      drive;
  gboolean trx, tune;
  guint    rate_req, rate_ack;
  char     device[64], protocol[64], mode[32];

  /* LWS thread only */
  GString    *txt;
  GByteArray *bin;
  guint       blocks;
  gint64      last_block_us;  /* arrival of the last audio block            */
  gboolean    lost;           /* stream watchdog fired                       */
  float      *mono;           /* de-interleaved block                        */
  gsize       mono_cap;
  gboolean    warned_fmt, warned_short, warned_rx, warned_bogus;

  volatile gint run;
  gboolean      started;
};

/* one outgoing WebSocket message: text command or binary Stream block */
struct outmsg {
  guint8  *data;              /* LWS_PRE bytes of headroom, then the payload */
  gsize    len;
  gboolean binary;
};

static void outmsg_free(gpointer p)
{
  struct outmsg *m = p;
  g_free(m->data);
  g_free(m);
}

static void cli_queue_raw(FtxTciClient *c, const void *payload, gsize len, gboolean binary)
{
  struct outmsg *m = g_new0(struct outmsg, 1);
  m->data = g_malloc(LWS_PRE + len);
  memcpy(m->data + LWS_PRE, payload, len);
  m->len = len;
  m->binary = binary;
  g_mutex_lock(&c->lock);
  g_queue_push_tail(&c->out, m);
  g_mutex_unlock(&c->lock);
  if (c->ctx) lws_cancel_service(c->ctx);
}

/* text command; takes ownership of msg */
static void cli_queue(FtxTciClient *c, char *msg)
{
  cli_queue_raw(c, msg, strlen(msg), FALSE);
  g_free(msg);
}

void ftx_tci_client_send(FtxTciClient *c, const char *cmd)
{
  if (!c->thread) return;
  cli_queue_raw(c, cmd, strlen(cmd), FALSE);
}

void ftx_tci_client_send_tx_audio(FtxTciClient *c, const float *mono, guint nframes, guint rate)
{
  if (!c->thread || nframes == 0) return;
  gsize len = STREAM_HDR_BYTES + (gsize)nframes * sizeof(float);
  guint8 *buf = g_malloc0(len);
  guint32 h[16] = { 0 };
  h[1] = rate;
  h[2] = STREAM_FMT_FLOAT;
  h[5] = nframes;
  h[6] = STREAM_TYPE_TXAUD;
  h[7] = 1;
  memcpy(buf, h, sizeof h);
  memcpy(buf + STREAM_HDR_BYTES, mono, (gsize)nframes * sizeof(float));
  cli_queue_raw(c, buf, len, TRUE);
  g_free(buf);
}

/* one complete "name:args" command (no ';') */
static void handle_command(FtxTciClient *c, char *cmd)
{
  char *args = strchr(cmd, ':');
  if (args) *args++ = '\0';
  for (char *p = cmd; *p; p++) *p = (char)g_ascii_tolower(*p);

  FtxTciVfoCb vfo_cb = NULL; gpointer vfo_user = NULL; double vfo_hz = 0;
  FtxTciTxCb tx_cb = NULL; gpointer tx_user = NULL; gboolean tx_val = FALSE;
  FtxTciModeCb mode_cb = NULL; gpointer mode_user = NULL; char mode_val[16] = "";

  g_mutex_lock(&c->lock);
  if (strcmp(cmd, "ready") == 0) {
    c->ready = TRUE;
    g_cond_broadcast(&c->cond);
  } else if (strcmp(cmd, "protocol") == 0 && args) {
    g_strlcpy(c->protocol, args, sizeof c->protocol);
  } else if (strcmp(cmd, "device") == 0 && args) {
    g_strlcpy(c->device, args, sizeof c->device);
  } else if (strcmp(cmd, "modulation") == 0 && args) {
    char *comma = strchr(args, ',');       /* modulation:<rx>,<mode> */
    if (comma && strtol(args, NULL, 10) == 0 && strcmp(c->mode, comma + 1) != 0) {
      g_strlcpy(c->mode, comma + 1, sizeof c->mode);
      mode_cb = c->mode_cb; mode_user = c->mode_cb_data; g_strlcpy(mode_val, c->mode, sizeof mode_val);
    }
  } else if (strcmp(cmd, "vfo") == 0 && args) {
    char *c1 = strchr(args, ',');
    char *c2 = c1 ? strchr(c1 + 1, ',') : NULL;
    if (c2 && strtol(args, NULL, 10) == 0 && strtol(c1 + 1, NULL, 10) == 0) {
      double hz = g_ascii_strtod(c2 + 1, NULL);
      if (hz > 0 && hz != c->vfo_hz) {
        c->vfo_hz = hz;
        vfo_cb = c->vfo_cb; vfo_user = c->vfo_cb_data; vfo_hz = hz;
      }
    }
  } else if (strcmp(cmd, "drive") == 0 && args) {
    char *comma = strchr(args, ',');            /* drive:<trx>,<n> */
    if (comma && strtol(args, NULL, 10) == 0) c->drive = (int)strtol(comma + 1, NULL, 10);
  } else if (strcmp(cmd, "audio_samplerate") == 0 && args) {
    long r = strtol(args, NULL, 10);
    if (r > 0) c->rate_ack = (guint)r;
  } else if ((strcmp(cmd, "trx") == 0 || strcmp(cmd, "tune") == 0) && args) {
    char *comma = strchr(args, ',');
    if (comma && strtol(args, NULL, 10) == 0) {
      const gboolean v = g_ascii_strncasecmp(comma + 1, "true", 4) == 0;
      const gboolean was = c->trx || c->tune;
      if (cmd[1] == 'r') c->trx = v; else c->tune = v;
      const gboolean now = c->trx || c->tune;
      if (now != was) { tx_cb = c->tx_cb; tx_user = c->tx_cb_data; tx_val = now; }
    }
  }
  g_mutex_unlock(&c->lock);
  if (vfo_cb) vfo_cb(vfo_hz, vfo_user);
  if (mode_cb) mode_cb(mode_val, mode_user);
  if (tx_cb) tx_cb(tx_val, tx_user);
}

static void drain_text(FtxTciClient *c)
{
  char *s = c->txt->str, *semi;
  gsize used = 0;
  while ((semi = strchr(s, ';')) != NULL) {
    *semi = '\0';
    handle_command(c, s);
    used = (gsize)(semi + 1 - c->txt->str);
    s = semi + 1;
  }
  if (used) g_string_erase(c->txt, 0, (gssize)used);
}

/* one complete binary WebSocket message = one Stream block */
static void handle_block(FtxTciClient *c)
{
  const gsize len = c->bin->len;
  if (len < STREAM_HDR_BYTES) {
    if (!c->warned_short) { c->warned_short = TRUE; g_warning("tci: binary message shorter than a Stream header"); }
    return;
  }
  guint32 h[16];
  memcpy(h, c->bin->data, sizeof h);
  const guint32 scalars = h[5];
  const gsize ssize = h[2] == STREAM_FMT_INT16 ? 2 : 4;
  const gsize need = STREAM_HDR_BYTES + (gsize)scalars * ssize;
  if (scalars == 0 || scalars > (1u << 20)) {
    if (!c->warned_bogus) { c->warned_bogus = TRUE; g_warning("tci: bogus Stream length %u, dropped", scalars); }
    return;
  }
  if (len < need) {
    if (!c->warned_short) { c->warned_short = TRUE; g_warning("tci: Stream block truncated (%u samples), dropped", scalars); }
    return;
  }
  if (h[0] != 0) {
    if (!c->warned_rx) { c->warned_rx = TRUE; g_message("tci: Stream blocks for receiver %u ignored", h[0]); }
    return;
  }
  if (h[6] != STREAM_TYPE_RXAUD) return;              /* IQ or other types: not ours */
  if (h[2] != STREAM_FMT_FLOAT && h[2] != STREAM_FMT_INT16) {
    if (!c->warned_fmt) { c->warned_fmt = TRUE; g_warning("tci: audio Stream format %u not supported", h[2]); }
    return;
  }
  const guint ch = h[7] ? h[7] : 1;
  const guint nframes = scalars / ch;
  c->last_block_us = g_get_monotonic_time();
  if (c->blocks++ == 0)
    g_message("tci: RX audio up — %u Hz, %s, %u channel(s), %u frames/block", h[1],
              h[2] == STREAM_FMT_FLOAT ? "float32" : "int16", ch, nframes);
  if (!c->audio_cb) return;
  if (c->mono_cap < nframes) {
    c->mono = g_realloc(c->mono, nframes * sizeof(float));
    c->mono_cap = nframes;
  }
  const guint8 *data = c->bin->data + STREAM_HDR_BYTES;
  if (h[2] == STREAM_FMT_FLOAT) {
    const float *p = (const float *)(const void *)data;
    for (guint i = 0; i < nframes; i++) c->mono[i] = p[i * ch];   /* channel A (mono duplicated or complex I) */
  } else {
    const gint16 *p = (const gint16 *)(const void *)data;
    for (guint i = 0; i < nframes; i++) c->mono[i] = p[i * ch] / 32768.0f;
  }
  c->audio_cb(c->mono, nframes, h[1], c->audio_cb_data);
}

static int client_cb(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
  FtxTciClient *c = lws_context_user(lws_get_context(wsi));
  (void)user;
  if (g_getenv("FTX_TCI_DEBUG")) g_message("tci: lws reason %d", (int)reason);
  switch (reason) {
  case LWS_CALLBACK_CLIENT_ESTABLISHED:
    g_mutex_lock(&c->lock);
    c->wsi = wsi;
    c->up = TRUE;
    g_cond_broadcast(&c->cond);
    g_mutex_unlock(&c->lock);
    return 0;
  case LWS_CALLBACK_CLIENT_RECEIVE:
    if (lws_frame_is_binary(wsi)) {
      g_byte_array_append(c->bin, (const guint8 *)in, (guint)len);
      if (lws_is_final_fragment(wsi) && lws_remaining_packet_payload(wsi) == 0) {
        handle_block(c);
        g_byte_array_set_size(c->bin, 0);
      }
    } else {
      g_string_append_len(c->txt, (const char *)in, (gssize)len);
      drain_text(c);
    }
    return 0;
  case LWS_CALLBACK_CLIENT_WRITEABLE: {
    g_mutex_lock(&c->lock);
    struct outmsg *m = g_queue_pop_head(&c->out);
    gboolean more = !g_queue_is_empty(&c->out);
    g_mutex_unlock(&c->lock);
    if (m) {
      lws_write(wsi, m->data + LWS_PRE, m->len, m->binary ? LWS_WRITE_BINARY : LWS_WRITE_TEXT);
      outmsg_free(m);
    }
    if (more) lws_callback_on_writable(wsi);
    return 0;
  }
  case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
    g_mutex_lock(&c->lock);
    c->failed = TRUE;
    g_cond_broadcast(&c->cond);
    g_mutex_unlock(&c->lock);
    return -1;
  case LWS_CALLBACK_CLIENT_CLOSED:
  case LWS_CALLBACK_WSI_DESTROY: {
    /* the peer closed (or the socket died): either reason may arrive first,
     * report the loss once */
    gboolean was_up;
    g_mutex_lock(&c->lock);
    was_up = c->up;
    c->up = FALSE;
    c->wsi = NULL;
    g_cond_broadcast(&c->cond);
    g_mutex_unlock(&c->lock);
    if (was_up && !c->lost && g_atomic_int_get(&c->run) && c->closed_cb) {
      c->lost = TRUE;
      c->closed_cb(c->closed_cb_data);
    }
    return 0;
  }
  default:
    return 0;
  }
}

static const struct lws_protocols c_protocols[] = {
  { "tci", client_cb, 0, 8192, 0, NULL, 0 },
  { NULL, NULL, 0, 0, 0, NULL, 0 }
};

/* Stream watchdog: audio is continuous once audio_start was acknowledged; a
 * server that died without a TCP close (or whose close lws did not surface)
 * shows up as silence.  Fires closed_cb once. */
#define STREAM_LOST_US (3 * G_TIME_SPAN_SECOND)
static void check_stream(FtxTciClient *c)
{
  if (c->lost || !c->started || c->blocks == 0) return;
  if (g_get_monotonic_time() - c->last_block_us < STREAM_LOST_US) return;
  c->lost = TRUE;
  g_warning("tci: no audio block for %d s — connection considered lost", (int)(STREAM_LOST_US / G_TIME_SPAN_SECOND));
  if (c->closed_cb) c->closed_cb(c->closed_cb_data);
}

static gpointer service_thread(gpointer data)
{
  FtxTciClient *c = data;
  while (g_atomic_int_get(&c->run)) {
    g_mutex_lock(&c->lock);
    gboolean pending = c->up && !g_queue_is_empty(&c->out);
    struct lws *wsi = c->wsi;
    g_mutex_unlock(&c->lock);
    if (pending && wsi) lws_callback_on_writable(wsi);
    lws_service(c->ctx, 0);
    check_stream(c);
    g_usleep(1000);
  }
  return NULL;
}

/* "unix:/path", "ws://host[:port]" or bare "host[:port]" (log-for-linux's
 * logfl_tci_endpoint_parse: no TLS on a TCI link, no IPv6 literals) */
static gboolean endpoint_parse(const char *s, struct endpoint *out)
{
  if (!s) return FALSE;
  char *t = g_strstrip(g_strdup(s));
  struct endpoint ep;
  memset(&ep, 0, sizeof ep);
  gboolean ok = FALSE;
  if (g_str_has_prefix(t, "unix:")) {
    const char *p = t + 5;
    if (p[0] == '/' && strlen(p) < sizeof ep.path) {
      ep.unix_sock = TRUE;
      g_strlcpy(ep.path, p, sizeof ep.path);
      ok = TRUE;
    }
  } else if (strstr(t, "://") == NULL || g_str_has_prefix(t, "ws://")) {
    char *hp = g_strdup(g_str_has_prefix(t, "ws://") ? t + 5 : t);
    size_t n = strlen(hp);
    if (n && hp[n - 1] == '/') hp[n - 1] = '\0';
    guint64 port = TCI_DEFAULT_PORT;
    gboolean port_ok = TRUE;
    char *colon = strrchr(hp, ':');
    if (colon) {
      *colon = '\0';
      port_ok = colon[1] && g_ascii_string_to_unsigned(colon + 1, 10, 1, 65535, &port, NULL);
    }
    if (port_ok && hp[0] && strpbrk(hp, "/:@ \t") == NULL && strlen(hp) < sizeof ep.host) {
      g_strlcpy(ep.host, hp, sizeof ep.host);
      ep.port = (guint16)port;
      ok = TRUE;
    }
    g_free(hp);
  }
  g_free(t);
  if (ok) *out = ep;
  return ok;
}

static char *endpoint_to_string(const struct endpoint *ep)
{
  return ep->unix_sock ? g_strdup_printf("unix:%s", ep->path) : g_strdup_printf("ws://%s:%u", ep->host, ep->port);
}

gboolean ftx_tci_server_valid(const char *server)
{
  while (server && g_ascii_isspace(*server)) server++;
  if (!server || !*server) return TRUE;
  struct endpoint ep;
  return endpoint_parse(server, &ep);
}

char *ftx_tci_server_from_host_port(const char *host, int port)
{
  while (host && g_ascii_isspace(*host)) host++;
  const gboolean def_host = !host || !*host || strcmp(host, "127.0.0.1") == 0;
  const gboolean def_port = port <= 0 || port == TCI_DEFAULT_PORT;
  if (def_host && def_port) return NULL;
  if (host && (g_str_has_prefix(host, "unix:") || strstr(host, "://"))) return g_strdup(host);
  return g_strdup_printf("ws://%s:%d", def_host ? "127.0.0.1" : host, def_port ? TCI_DEFAULT_PORT : port);
}

FtxTciClient *ftx_tci_client_new(const char *server)
{
  FtxTciClient *c = g_new0(FtxTciClient, 1);
  c->order = g_ptr_array_new_with_free_func(g_free);
  while (server && g_ascii_isspace(*server)) server++;
  if (server && *server) {
    struct endpoint ep;
    if (endpoint_parse(server, &ep)) g_ptr_array_add(c->order, endpoint_to_string(&ep));
    else c->bad = g_strdup(server);
  } else {
    /* the local radio's socket (sdr-for-linux's own default path; one moved
     * by its preference is typed in as unix:/path), then its TCP port */
    char *sock = g_build_filename(g_get_user_runtime_dir(), "sdr-for-linux", "tci.sock", NULL);
    g_ptr_array_add(c->order, g_strdup_printf("unix:%s", sock));
    g_ptr_array_add(c->order, g_strdup(TCI_DEFAULT_TCP));
    g_free(sock);
  }
  if (c->order->len) c->endpoint = g_strdup(g_ptr_array_index(c->order, 0));
  g_mutex_init(&c->lock);
  g_cond_init(&c->cond);
  g_queue_init(&c->out);
  c->txt = g_string_new(NULL);
  c->bin = g_byte_array_new();
  return c;
}

void ftx_tci_client_free(FtxTciClient *c)
{
  if (!c) return;
  ftx_tci_client_stop(c);
  struct outmsg *m;
  while ((m = g_queue_pop_head(&c->out)) != NULL) outmsg_free(m);
  g_string_free(c->txt, TRUE);
  g_byte_array_free(c->bin, TRUE);
  g_free(c->mono);
  g_mutex_clear(&c->lock);
  g_cond_clear(&c->cond);
  g_ptr_array_free(c->order, TRUE);
  g_free(c->bad);
  g_free(c->endpoint);
  g_free(c->dial);
  g_free(c);
}

void ftx_tci_client_set_audio_cb(FtxTciClient *c, FtxTciAudioCb cb, gpointer u) { c->audio_cb = cb; c->audio_cb_data = u; }
void ftx_tci_client_set_vfo_cb(FtxTciClient *c, FtxTciVfoCb cb, gpointer u) { c->vfo_cb = cb; c->vfo_cb_data = u; }
void ftx_tci_client_set_mode_cb(FtxTciClient *c, FtxTciModeCb cb, gpointer u) { c->mode_cb = cb; c->mode_cb_data = u; }
void ftx_tci_client_set_tx_cb(FtxTciClient *c, FtxTciTxCb cb, gpointer u) { c->tx_cb = cb; c->tx_cb_data = u; }
void ftx_tci_client_set_closed_cb(FtxTciClient *c, FtxTciClosedCb cb, gpointer u) { c->closed_cb = cb; c->closed_cb_data = u; }

/* Dial one address and wait for the server's ready;.  On failure the
 * context is torn down again and *why says what happened. */
static gboolean dial(FtxTciClient *c, const char *addr, char **why)
{
  struct endpoint ep;
  if (!endpoint_parse(addr, &ep)) { *why = g_strdup_printf("%s: not an address", addr); return FALSE; }
  g_free(c->endpoint);
  c->endpoint = g_strdup(addr);
  if (ep.unix_sock && !g_file_test(ep.path, G_FILE_TEST_EXISTS)) {
    *why = g_strdup_printf("%s: no such socket", addr);
    return FALSE;
  }
  struct lws_context_creation_info info;
  memset(&info, 0, sizeof info);
  info.port = CONTEXT_PORT_NO_LISTEN;
  info.protocols = c_protocols;
  info.gid = (gid_t)-1;
  info.uid = (uid_t)-1;
  info.user = c;
  c->ctx = lws_create_context(&info);
  if (!c->ctx) { *why = g_strdup("lws_create_context failed"); return FALSE; }
  struct lws_client_connect_info ci;
  memset(&ci, 0, sizeof ci);
  ci.context = c->ctx;
  ci.path = "/";
  ci.protocol = "tci";
  g_free(c->dial);
  if (ep.unix_sock) {
    /* libwebsockets dials a Unix socket when the ADDRESS starts with '+'
     * (sdr-for-linux docs/TCI-SCOPE.md, Transports); the handshake still
     * needs a host name.  The client owns the string for the dial. */
    c->dial = g_strdup_printf("+%s", ep.path);
    ci.address = c->dial;
    ci.port = 0;
    ci.host = ci.origin = "localhost";
  } else {
    c->dial = g_strdup(ep.host);
    ci.address = ci.host = ci.origin = c->dial;
    ci.port = ep.port;
  }
  /* a NULL back from the dial is a failure lws need not report again
   * through the callback */
  if (!lws_client_connect_via_info(&ci)) {
    g_mutex_lock(&c->lock);
    c->failed = TRUE;
    g_mutex_unlock(&c->lock);
  }

  g_atomic_int_set(&c->run, 1);
  c->thread = g_thread_new("ftx-tci", service_thread, c);

  gint64 deadline = g_get_monotonic_time() + HANDSHAKE_TIMEOUT_S * G_TIME_SPAN_SECOND;
  g_mutex_lock(&c->lock);
  while (!c->ready && !c->failed)
    if (!g_cond_wait_until(&c->cond, &c->lock, deadline)) break;
  gboolean ready = c->ready, failed = c->failed;
  g_mutex_unlock(&c->lock);
  if (!ready) {
    *why = g_strdup_printf(failed ? "%s: connection refused" : "%s: handshake timeout (no ready;)", addr);
    ftx_tci_client_stop(c);
    return FALSE;
  }
  return TRUE;
}

gboolean ftx_tci_client_start(FtxTciClient *c, guint rate, GError **error)
{
  if (c->thread) {
    g_set_error(error, FTX_TCI_ERROR, 1, "TCI client already started");
    return FALSE;
  }
  if (rate != 8000 && rate != 12000 && rate != 24000 && rate != 48000) {
    g_set_error(error, FTX_TCI_ERROR, 2, "invalid audio rate %u (8/12/24/48 kHz)", rate);
    return FALSE;
  }
  if (c->bad) {
    g_set_error(error, FTX_TCI_ERROR, 5, "TCI server \"%s\" is not unix:/path or ws://host:port", c->bad);
    return FALSE;
  }
  lws_set_log_level(LLL_ERR, NULL);
  GString *whys = g_string_new(NULL);
  gboolean up = FALSE;
  for (guint i = 0; i < c->order->len && !up; i++) {
    char *why = NULL;
    up = dial(c, g_ptr_array_index(c->order, i), &why);
    if (!up) { g_string_append_printf(whys, "%s%s", whys->len ? "; " : "", why); g_free(why); }
  }
  if (!up) {
    g_set_error(error, FTX_TCI_ERROR, 4, "%s", whys->str);
    g_string_free(whys, TRUE);
    return FALSE;
  }
  g_string_free(whys, TRUE);
  /* one command per text frame; the server echoes each */
  g_mutex_lock(&c->lock);
  c->rate_req = rate;
  g_mutex_unlock(&c->lock);
  cli_queue(c, g_strdup_printf("audio_samplerate:%u;", rate));
  cli_queue(c, g_strdup("audio_stream_sample_type:float32;"));
  cli_queue(c, g_strdup("audio_stream_channels:1;"));
  cli_queue(c, g_strdup("audio_start:0;"));
  c->started = TRUE;
  return TRUE;
}

void ftx_tci_client_stop(FtxTciClient *c)
{
  if (!c->thread) return;
  if (c->started) {
    cli_queue(c, g_strdup("audio_stop:0;"));
    for (int ms = 0; ms < STOP_FLUSH_MS; ms++) {
      g_mutex_lock(&c->lock);
      gboolean flushed = g_queue_is_empty(&c->out) || !c->up;
      g_mutex_unlock(&c->lock);
      if (flushed) break;
      g_usleep(1000);
    }
    c->started = FALSE;
  }
  g_atomic_int_set(&c->run, 0);
  lws_cancel_service(c->ctx);
  g_thread_join(c->thread);
  c->thread = NULL;
  lws_context_destroy(c->ctx);
  c->ctx = NULL;
  c->wsi = NULL;
  c->up = c->ready = c->failed = FALSE;
  g_string_set_size(c->txt, 0);
  g_byte_array_set_size(c->bin, 0);
  c->blocks = 0;
  c->lost = FALSE;
  c->last_block_us = 0;
  c->warned_fmt = c->warned_short = c->warned_rx = c->warned_bogus = FALSE;
}

double ftx_tci_client_vfo_hz(FtxTciClient *c) { g_mutex_lock(&c->lock); double v = c->vfo_hz; g_mutex_unlock(&c->lock); return v; }
int    ftx_tci_client_drive(FtxTciClient *c) { g_mutex_lock(&c->lock); int v = c->drive; g_mutex_unlock(&c->lock); return v; }
gboolean ftx_tci_client_tx(FtxTciClient *c) { g_mutex_lock(&c->lock); gboolean v = c->trx || c->tune; g_mutex_unlock(&c->lock); return v; }
gboolean ftx_tci_client_trx(FtxTciClient *c) { g_mutex_lock(&c->lock); gboolean v = c->trx; g_mutex_unlock(&c->lock); return v; }
const char *ftx_tci_client_endpoint(FtxTciClient *c) { return c->endpoint ? c->endpoint : (c->bad ? c->bad : ""); }
const char *ftx_tci_client_device(FtxTciClient *c) { return c->device; }
const char *ftx_tci_client_protocol(FtxTciClient *c) { return c->protocol; }
const char *ftx_tci_client_mode(FtxTciClient *c) { return c->mode; }
