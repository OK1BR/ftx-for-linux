/* preferences.c — see preferences.h.  Copyright (C) 2026 Richard Fakenberg,
 * OK1BR.  GPL-3.0-or-later. */
#include "preferences.h"

#include <string.h>

#include "waterfall.h"

typedef struct {
  FtxSettings    *s;
  FtxPrefsChanged cb;
  gpointer        user;
} Ctx;

static void notify(Ctx *c) { if (c->cb) c->cb(c->user); }

static void on_switch(GObject *row, GParamSpec *ps, gpointer user)
{
  (void)ps;
  Ctx *c = user;
  gboolean *f = g_object_get_data(row, "field");
  *f = adw_switch_row_get_active(ADW_SWITCH_ROW(row));
  notify(c);
}

static void on_spin(GObject *row, GParamSpec *ps, gpointer user)
{
  (void)ps;
  Ctx *c = user;
  double v = adw_spin_row_get_value(ADW_SPIN_ROW(row));
  int *fi = g_object_get_data(row, "field-int");
  double *fd = g_object_get_data(row, "field-double");
  if (fi) *fi = (int)(v + (v < 0 ? -0.5 : 0.5));
  if (fd) *fd = v;
  notify(c);
}

static void on_entry(GtkEditable *e, gpointer user)
{
  Ctx *c = user;
  char *f = g_object_get_data(G_OBJECT(e), "field");
  gsize n = (gsize)GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(e), "field-size"));
  g_strlcpy(f, gtk_editable_get_text(e), n);
  g_strstrip(f);
  notify(c);
}

static void on_combo(GObject *row, GParamSpec *ps, gpointer user)
{
  (void)ps;
  Ctx *c = user;
  int *f = g_object_get_data(row, "field");
  int off = GPOINTER_TO_INT(g_object_get_data(row, "offset"));   /* stored value = index + offset */
  *f = (int)adw_combo_row_get_selected(ADW_COMBO_ROW(row)) + off;
  notify(c);
}

static GtkWidget *group(AdwPreferencesPage *page, const char *title, const char *desc)
{
  GtkWidget *g = adw_preferences_group_new();
  adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(g), title);
  if (desc) adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(g), desc);
  adw_preferences_page_add(page, ADW_PREFERENCES_GROUP(g));
  return g;
}

static void sw(Ctx *c, GtkWidget *g, const char *title, const char *sub, gboolean *field)
{
  GtkWidget *r = adw_switch_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
  if (sub) adw_action_row_set_subtitle(ADW_ACTION_ROW(r), sub);
  adw_switch_row_set_active(ADW_SWITCH_ROW(r), *field);
  g_object_set_data(G_OBJECT(r), "field", field);
  g_signal_connect(r, "notify::active", G_CALLBACK(on_switch), c);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), r);
}

static void spin(Ctx *c, GtkWidget *g, const char *title, const char *sub, double lo, double hi, double step,
                 int digits, int *fi, double *fd)
{
  GtkWidget *r = adw_spin_row_new_with_range(lo, hi, step);
  adw_spin_row_set_digits(ADW_SPIN_ROW(r), (guint)digits);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
  if (sub) adw_action_row_set_subtitle(ADW_ACTION_ROW(r), sub);
  adw_spin_row_set_value(ADW_SPIN_ROW(r), fi ? *fi : *fd);
  if (fi) g_object_set_data(G_OBJECT(r), "field-int", fi);
  if (fd) g_object_set_data(G_OBJECT(r), "field-double", fd);
  g_signal_connect(r, "notify::value", G_CALLBACK(on_spin), c);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), r);
}

static void entry(Ctx *c, GtkWidget *g, const char *title, char *field, gsize size)
{
  GtkWidget *r = adw_entry_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
  gtk_editable_set_text(GTK_EDITABLE(r), field);
  g_object_set_data(G_OBJECT(r), "field", field);
  g_object_set_data(G_OBJECT(r), "field-size", GSIZE_TO_POINTER(size));
  g_signal_connect(r, "changed", G_CALLBACK(on_entry), c);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), r);
}

static void combo(Ctx *c, GtkWidget *g, const char *title, const char *sub, const char *const *items, int *field)
{
  GtkWidget *r = adw_combo_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
  if (sub) adw_action_row_set_subtitle(ADW_ACTION_ROW(r), sub);
  adw_combo_row_set_model(ADW_COMBO_ROW(r), G_LIST_MODEL(gtk_string_list_new(items)));
  adw_combo_row_set_selected(ADW_COMBO_ROW(r), (guint)MAX(*field, 0));
  g_object_set_data(G_OBJECT(r), "field", field);
  g_signal_connect(r, "notify::selected", G_CALLBACK(on_combo), c);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), r);
}

static AdwPreferencesPage *page(AdwPreferencesDialog *d, const char *title, const char *name, const char *icon)
{
  GtkWidget *p = adw_preferences_page_new();
  adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(p), title);
  adw_preferences_page_set_name(ADW_PREFERENCES_PAGE(p), name);
  adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(p), icon);
  adw_preferences_dialog_add(d, ADW_PREFERENCES_PAGE(p));
  return ADW_PREFERENCES_PAGE(p);
}

AdwDialog *ftx_preferences_new(FtxSettings *s, FtxPrefsChanged changed, gpointer user)
{
  Ctx *c = g_new0(Ctx, 1);
  c->s = s; c->cb = changed; c->user = user;
  AdwDialog *d = ADW_DIALOG(adw_preferences_dialog_new());
  g_object_set_data_full(G_OBJECT(d), "ctx", c, g_free);
  adw_dialog_set_title(d, "Preferences");
  /* wide enough for the five page tabs to stay in the header bar under a
   * large-font theme (libadwaita moves them to a bottom bar when the
   * switcher does not fit); the sdr-for-linux size */
  adw_dialog_set_content_width(d, 1000);
  adw_dialog_set_content_height(d, 720);
  AdwPreferencesDialog *pd = ADW_PREFERENCES_DIALOG(d);

  AdwPreferencesPage *p = page(pd, "Station", "station", "avatar-default-symbolic");
  GtkWidget *g = group(p, "Operator", "Used in every message and for a-priori decoding.");
  entry(c, g, "Callsign", s->call, sizeof s->call);
  entry(c, g, "Grid locator", s->grid, sizeof s->grid);
  g = group(p, "Messages", NULL);
  sw(c, g, "Skip Tx1", "start a QSO with the report (Tx2) instead of the grid (Tx1)", &s->skip_tx1);
  sw(c, g, "RR73 instead of RRR", "Tx4 ends the QSO with RR73", &s->rr73);

  p = page(pd, "Radio", "radio", "network-wireless-symbolic");
  g = group(p, "TCI server", "sdr-for-linux or icom-for-linux.  RX audio (12 kHz) comes over this connection; reconnect to apply a new host or port.");
  entry(c, g, "Host", s->tci_host, sizeof s->tci_host);
  spin(c, g, "Port", NULL, 1, 65535, 1, 0, &s->tci_port, NULL);
  sw(c, g, "Connect at start", NULL, &s->connect_at_start);
  g = group(p, "Transmit", "FTX keys the radio with trx over TCI and streams the FT8/FT4 audio (48 kHz) as the TX source.");
  spin(c, g, "TX audio level", "% of full scale; the radio's drive sets the power", 10, 100, 5, 0, &s->tx_level_pct, NULL);
  sw(c, g, "Check the mode before keying", "refuse to transmit unless the radio reports DIGU or USB", &s->check_mode);

  p = page(pd, "Decoder", "decoder", "system-run-symbolic");
  g = group(p, "Decoding", "The WSJT-X 3.0.2 algorithms, ported.  Depth 3 is what jt9 -d 3 does.");
  static const char *const depth[] = { "1 — fast, BP only", "2 — subtraction, 3 passes", "3 — deep (OSD, early decodes)", NULL };
  s->depth = CLAMP(s->depth, 1, 3);
  int *depth_idx = &s->depth;   /* stored 1..3, shown 0..2 */
  {
    GtkWidget *r = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), "Depth");
    adw_combo_row_set_model(ADW_COMBO_ROW(r), G_LIST_MODEL(gtk_string_list_new(depth)));
    adw_combo_row_set_selected(ADW_COMBO_ROW(r), (guint)(*depth_idx - 1));
    g_object_set_data(G_OBJECT(r), "field", depth_idx);
    g_object_set_data(G_OBJECT(r), "offset", GINT_TO_POINTER(1));
    g_signal_connect(r, "notify::selected", G_CALLBACK(on_combo), c);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), r);
  }
  sw(c, g, "A-priori decoding", "use my call and the DX call to pull weak replies out of the noise; also tries the messages a station is expected to send next (a7, a8)", &s->ap);
  spin(c, g, "Lower edge", "Hz — jt9 uses 200", 100, 1000, 10, 0, &s->nfa, NULL);
  spin(c, g, "Upper edge", "Hz — jt9 uses 4000", 1000, 5000, 10, 0, &s->nfb, NULL);
  g = group(p, "Clock", "FT8 needs the computer clock within about a second of UTC.");
  spin(c, g, "Warn when mean DT exceeds", "seconds", 0.1, 2.0, 0.1, 1, NULL, &s->clock_warn_dt);
  sw(c, g, "Show DT health in the status bar", NULL, &s->show_dt_health);

  p = page(pd, "Logging", "logging", "document-save-symbolic");
  char *dir = g_build_filename(g_get_user_data_dir(), "ftx-for-linux", "ALL.TXT", NULL);
  g = group(p, "ALL.TXT", dir);
  g_free(dir);
  sw(c, g, "Write ALL.TXT", "WSJT-X compatible text log of every decode", &s->all_txt);

  p = page(pd, "Display", "display", "preferences-desktop-display-symbolic");
  g = group(p, "Waterfall", NULL);
  const char *pal[16] = { NULL };
  int npal = MIN(ftx_waterfall_palette_count(), 15);
  for (int i = 0; i < npal; i++) pal[i] = ftx_waterfall_palette_name(i);
  s->palette = CLAMP(s->palette, 0, npal - 1);
  combo(c, g, "Palette", "the same schemes as sdr-for-linux and skimmer-for-linux", pal, &s->palette);
  spin(c, g, "Rows per second", "waterfall speed", 1, 5, 1, 0, &s->rows_per_s, NULL);
  sw(c, g, "Label decoded signals on the waterfall", NULL, &s->wf_labels);
  g = group(p, "Decode lists", NULL);
  sw(c, g, "Highlight CQ", NULL, &s->hl_cq);
  spin(c, g, "Keep periods", "how many periods stay in Band activity", 1, 60, 1, 0, &s->keep_periods, NULL);
  return d;
}
