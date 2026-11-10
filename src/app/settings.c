/* settings.c — see settings.h.  Copyright (C) 2026 Richard Fakenberg, OK1BR.
 * GPL-3.0-or-later. */
#include "settings.h"

#include <stddef.h>
#include <string.h>

enum { T_INT, T_BOOL, T_DOUBLE, T_STR };

/* One line per key: the file layout is the struct layout. */
static const struct key {
  const char *group, *name;
  int type;
  size_t off, size;
} keys[] = {
#define K(g, n, t, f) { g, n, t, offsetof(FtxSettings, f), sizeof(((FtxSettings *)0)->f) }
  K("station", "call", T_STR, call), K("station", "grid", T_STR, grid),
  K("station", "skip_tx1", T_BOOL, skip_tx1), K("station", "rr73", T_BOOL, rr73),
  K("station", "dclick_tx", T_BOOL, dclick_tx),
  K("station", "watchdog_min", T_INT, watchdog_min),
  K("radio", "tci_server", T_STR, tci_server),
  K("radio", "connect_at_start", T_BOOL, connect_at_start),
  K("radio", "tx_level_pct", T_INT, tx_level_pct), K("radio", "check_mode", T_BOOL, check_mode),
#define KF(i, n) K("frequencies", "ft8_" n, T_INT, freq_ft8[i]), K("frequencies", "ft4_" n, T_INT, freq_ft4[i])
  KF(0, "160m"), KF(1, "80m"), KF(2, "60m"), KF(3, "40m"), KF(4, "30m"), KF(5, "20m"),
  KF(6, "17m"), KF(7, "15m"), KF(8, "12m"), KF(9, "10m"), KF(10, "6m"),
#undef KF
  K("decoder", "depth", T_INT, depth), K("decoder", "ap", T_BOOL, ap),
  K("decoder", "nfa", T_INT, nfa), K("decoder", "nfb", T_INT, nfb),
  K("decoder", "clock_warn_dt", T_DOUBLE, clock_warn_dt), K("decoder", "show_dt_health", T_BOOL, show_dt_health),
  K("display", "palette", T_INT, palette), K("display", "rows_per_s", T_INT, rows_per_s),
  K("display", "filter_op", T_INT, filter_op),
  K("display", "wf_labels", T_BOOL, wf_labels),
  K("display", "hl_cq", T_BOOL, hl_cq), K("display", "dim_b4", T_BOOL, dim_b4),
  K("display", "tag_dxcc", T_BOOL, tag_dxcc),
  K("display", "country", T_INT, country),
  K("display", "keep_periods", T_INT, keep_periods),
  K("display", "newest_first", T_BOOL, newest_first), K("display", "follow_new", T_BOOL, follow_new),
  K("logging", "all_txt", T_BOOL, all_txt),
  K("logging", "udp_enable", T_BOOL, udp_enable), K("logging", "udp_host", T_STR, udp_host),
  K("logging", "udp_port", T_INT, udp_port), K("logging", "auto_log", T_BOOL, auto_log),
  K("logging", "psk_enable", T_BOOL, psk_enable), K("logging", "psk_antenna", T_STR, psk_antenna),
  K("logging", "psk_tcp", T_BOOL, psk_tcp),
  K("state", "ft4", T_BOOL, ft4), K("state", "rx_hz", T_INT, rx_hz), K("state", "tx_hz", T_INT, tx_hz),
  K("state", "hold_tx", T_BOOL, hold_tx), K("state", "tx_eq_rx", T_BOOL, tx_eq_rx),
  K("state", "auto_seq", T_BOOL, auto_seq), K("state", "report", T_INT, report),
  K("window", "width", T_INT, win_w), K("window", "height", T_INT, win_h),
  K("window", "maximized", T_BOOL, win_max), K("window", "paned", T_INT, paned_pos),
  K("window", "lists", T_INT, lists_pos),
#undef K
};
G_STATIC_ASSERT(FTX_NBANDS == 11);   /* the KF lines above name every band */

void ftx_settings_defaults(FtxSettings *s)
{
  memset(s, 0, sizeof *s);
  s->skip_tx1 = FALSE; s->rr73 = TRUE; s->dclick_tx = TRUE; s->watchdog_min = 6;
  s->tci_server[0] = '\0'; s->connect_at_start = TRUE;
  s->tx_level_pct = 90; s->check_mode = TRUE;
  for (int b = 0; b < FTX_NBANDS; b++) { s->freq_ft8[b] = (int)ftx_bands[b].ft8_hz; s->freq_ft4[b] = (int)ftx_bands[b].ft4_hz; }
  s->depth = 3; s->ap = TRUE; s->nfa = 200; s->nfb = 4000; s->clock_warn_dt = 0.5; s->show_dt_health = TRUE;
  s->palette = 0; s->rows_per_s = 8; s->wf_labels = TRUE; s->filter_op = 60;
  s->hl_cq = TRUE; s->dim_b4 = TRUE; s->tag_dxcc = TRUE; s->country = 1; s->keep_periods = 20; s->newest_first = FALSE; s->follow_new = TRUE;
  s->all_txt = FALSE; s->udp_enable = TRUE; g_strlcpy(s->udp_host, "127.0.0.1", sizeof s->udp_host); s->udp_port = 2237; s->auto_log = TRUE;
  s->psk_enable = FALSE; s->psk_antenna[0] = '\0'; s->psk_tcp = FALSE;
  s->rx_hz = 1500; s->tx_hz = 1500; s->hold_tx = TRUE; s->tx_eq_rx = FALSE; s->auto_seq = TRUE; s->report = -10;
  s->win_w = 1180; s->win_h = 960; s->paned_pos = 320; s->lists_pos = 820;
}

static char cfg_path[1024], data_dir[1024];

const char *ftx_settings_path(void)
{
  if (!cfg_path[0]) {
    char *p = g_build_filename(g_get_user_config_dir(), "ftx-for-linux", "config.ini", NULL);
    g_strlcpy(cfg_path, p, sizeof cfg_path);
    g_free(p);
  }
  return cfg_path;
}

const char *ftx_settings_data_dir(void)
{
  if (!data_dir[0]) {
    char *p = g_build_filename(g_get_user_data_dir(), "ftx-for-linux", NULL);
    g_strlcpy(data_dir, p, sizeof data_dir);
    g_free(p);
  }
  g_mkdir_with_parents(data_dir, 0755);
  return data_dir;
}

gboolean ftx_settings_load(FtxSettings *s)
{
  GKeyFile *kf = g_key_file_new();
  if (!g_key_file_load_from_file(kf, ftx_settings_path(), G_KEY_FILE_NONE, NULL)) {
    g_key_file_free(kf);
    return FALSE;
  }
  for (size_t i = 0; i < G_N_ELEMENTS(keys); i++) {
    const struct key *k = &keys[i];
    if (!g_key_file_has_key(kf, k->group, k->name, NULL)) continue;
    char *dst = (char *)s + k->off;
    switch (k->type) {
    case T_INT:    *(int *)dst = g_key_file_get_integer(kf, k->group, k->name, NULL); break;
    case T_BOOL:   *(gboolean *)dst = g_key_file_get_boolean(kf, k->group, k->name, NULL); break;
    case T_DOUBLE: *(double *)dst = g_key_file_get_double(kf, k->group, k->name, NULL); break;
    case T_STR: {
      char *v = g_key_file_get_string(kf, k->group, k->name, NULL);
      if (v) { g_strlcpy(dst, v, k->size); g_free(v); }
      break;
    }
    }
  }
  /* configs up to 0.1.0 carry [radio] tci_host / tci_port: the default pair
   * now means "socket first, then TCP" (an empty tci_server), anything else
   * is that TCP address */
  if (!g_key_file_has_key(kf, "radio", "tci_server", NULL) && g_key_file_has_key(kf, "radio", "tci_host", NULL)) {
    char *h = g_key_file_get_string(kf, "radio", "tci_host", NULL);
    int port = g_key_file_get_integer(kf, "radio", "tci_port", NULL);
    if (port < 1 || port > 65535) port = 40001;
    if (h) g_strstrip(h);
    if (h && h[0] && (strcmp(h, "127.0.0.1") != 0 || port != 40001))
      g_snprintf(s->tci_server, sizeof s->tci_server, "ws://%s:%d", h, port);
    g_free(h);
  }
  g_key_file_free(kf);
  return TRUE;
}

gboolean ftx_settings_save(const FtxSettings *s)
{
  GKeyFile *kf = g_key_file_new();
  g_key_file_load_from_file(kf, ftx_settings_path(), G_KEY_FILE_KEEP_COMMENTS, NULL);  /* keep foreign keys */
  for (size_t i = 0; i < G_N_ELEMENTS(keys); i++) {
    const struct key *k = &keys[i];
    const char *src = (const char *)s + k->off;
    switch (k->type) {
    case T_INT:    g_key_file_set_integer(kf, k->group, k->name, *(const int *)src); break;
    case T_BOOL:   g_key_file_set_boolean(kf, k->group, k->name, *(const gboolean *)src); break;
    case T_DOUBLE: g_key_file_set_double(kf, k->group, k->name, *(const double *)src); break;
    case T_STR:    g_key_file_set_string(kf, k->group, k->name, src); break;
    }
  }
  g_key_file_remove_key(kf, "radio", "tci_host", NULL);    /* replaced by tci_server */
  g_key_file_remove_key(kf, "radio", "tci_port", NULL);
  char *dir = g_path_get_dirname(ftx_settings_path());
  g_mkdir_with_parents(dir, 0700);
  g_free(dir);
  GError *err = NULL;
  gboolean ok = g_key_file_save_to_file(kf, ftx_settings_path(), &err);
  if (!ok) { g_warning("settings: %s", err->message); g_error_free(err); }
  g_key_file_free(kf);
  return ok;
}
