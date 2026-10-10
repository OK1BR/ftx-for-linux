/* Mockup — FTX for Linux preferences (libadwaita AdwPreferencesDialog in a
 * window).  Throwaway design reference, nothing is stored.  Compile:
 *   cc ftx_preferences.c -o ftx-mock-prefs $(pkg-config --cflags --libs libadwaita-1)
 * Copyright (C) 2026 Richard Fakenberg, OK1BR.  GPL-3.0-or-later. */
#include <adwaita.h>

static GtkWidget *entry_row(const char *title, const char *subtitle, const char *value) {
  GtkWidget *r = adw_entry_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
  gtk_editable_set_text(GTK_EDITABLE(r), value);
  (void)subtitle;
  return r;
}
static GtkWidget *switch_row(const char *title, const char *subtitle, gboolean on) {
  GtkWidget *r = adw_switch_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
  if (subtitle) adw_action_row_set_subtitle(ADW_ACTION_ROW(r), subtitle);
  adw_switch_row_set_active(ADW_SWITCH_ROW(r), on);
  return r;
}
static GtkWidget *spin_row(const char *title, const char *subtitle, double lo, double hi, double v) {
  double step = hi - lo <= 5 ? 0.1 : 1;
  GtkWidget *r = adw_spin_row_new_with_range(lo, hi, step);
  if (step < 1) adw_spin_row_set_digits(ADW_SPIN_ROW(r), 1);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
  if (subtitle) adw_action_row_set_subtitle(ADW_ACTION_ROW(r), subtitle);
  adw_spin_row_set_value(ADW_SPIN_ROW(r), v);
  return r;
}
static GtkWidget *combo_row(const char *title, const char *subtitle, const char *const *items, int sel) {
  GtkWidget *r = adw_combo_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
  if (subtitle) adw_action_row_set_subtitle(ADW_ACTION_ROW(r), subtitle);
  adw_combo_row_set_model(ADW_COMBO_ROW(r), G_LIST_MODEL(gtk_string_list_new(items)));
  adw_combo_row_set_selected(ADW_COMBO_ROW(r), sel);
  return r;
}
static GtkWidget *group(const char *title, const char *desc) {
  GtkWidget *g = adw_preferences_group_new();
  adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(g), title);
  if (desc) adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(g), desc);
  return g;
}
#define ADD(g, w) adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), w)



/* Under Broadway (used for the README screenshots) the daemon reports a small
 * default screen until a browser connects, and GTK clamps the *default* size to
 * it.  A hard size request is not clamped, so pin the mockup to its design size
 * there; on a real desktop the window stays freely resizable. */
static void pin_size_under_broadway(GtkWindow *win, int w, int h) {
  if (g_strcmp0(g_getenv("GDK_BACKEND"), "broadway") == 0)
    gtk_widget_set_size_request(GTK_WIDGET(win), w, h);
}

static void activate(GtkApplication *app, gpointer u) {
  (void)u;
  adw_style_manager_set_color_scheme(adw_style_manager_get_default(), ADW_COLOR_SCHEME_FORCE_DARK);
  GtkWidget *win = adw_application_window_new(app);
  gtk_window_set_default_size(GTK_WINDOW(win), 760, 820);
  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(win), content);
  gtk_window_present(GTK_WINDOW(win));

  AdwDialog *d = ADW_DIALOG(adw_preferences_dialog_new());
  adw_dialog_set_title(d, "Preferences");
  adw_dialog_set_content_width(d, 720);
  adw_dialog_set_content_height(d, 800);

  /* Station */
  GtkWidget *p1 = adw_preferences_page_new();
  adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(p1), "Station");
  adw_preferences_page_set_name(ADW_PREFERENCES_PAGE(p1), "station");
  adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(p1), "avatar-default-symbolic");
  GtkWidget *g = group("Operator", "Used in every message and for a-priori decoding.");
  ADD(g, entry_row("Callsign", NULL, "OK1BR"));
  ADD(g, entry_row("Grid locator", NULL, "JO70"));
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(p1), ADW_PREFERENCES_GROUP(g));
  g = group("Messages", NULL);
  ADD(g, switch_row("Send grid in Tx1", "Tx1 = \"DX ME GRID\" (standard); off = \"DX ME\" only", TRUE));
  ADD(g, switch_row("RR73 instead of RRR", "Tx4 ends the QSO with RR73", TRUE));
  ADD(g, spin_row("Watchdog", "stop transmitting after this many unanswered minutes", 1, 30, 6));
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(p1), ADW_PREFERENCES_GROUP(g));
  adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(d), ADW_PREFERENCES_PAGE(p1));

  /* Radio */
  GtkWidget *p2 = adw_preferences_page_new();
  adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(p2), "Radio");
  adw_preferences_page_set_name(ADW_PREFERENCES_PAGE(p2), "radio");
  adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(p2), "network-wireless-symbolic");
  g = group("TCI server", "sdr-for-linux or icom-for-linux. Audio both ways and PTT go over this one connection.");
  ADD(g, entry_row("Host", NULL, "192.168.1.18"));
  ADD(g, spin_row("Port", NULL, 1, 65535, 40001));
  ADD(g, switch_row("Connect at start", NULL, TRUE));
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(p2), ADW_PREFERENCES_GROUP(g));
  g = group("Transmit", "The radio must be in DIGU; FTX asks for the TCI audio source and keys with trx.");
  static const char *const tx_items[] = { "TCI audio + trx (recommended)", "TCI audio, PTT by the radio (VOX)", NULL };
  ADD(g, combo_row("TX path", NULL, tx_items, 0));
  ADD(g, switch_row("Check the mode before keying", "refuse to transmit unless the radio reports DIGU/USB", TRUE));
  ADD(g, spin_row("Split threshold", "Hz above which the dial moves instead of the audio tone (0 = never retune)", 0, 3000, 2000));
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(p2), ADW_PREFERENCES_GROUP(g));
  adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(d), ADW_PREFERENCES_PAGE(p2));

  /* Decoder */
  GtkWidget *p3 = adw_preferences_page_new();
  adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(p3), "Decoder");
  adw_preferences_page_set_name(ADW_PREFERENCES_PAGE(p3), "decoder");
  adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(p3), "system-run-symbolic");
  g = group("Decoding", "The WSJT-X 3.0.2 algorithms, ported. Depth 3 is what jt9 -d 3 does.");
  static const char *const depth[] = { "1 — fast, BP only", "2 — subtraction, 3 passes", "3 — deep (OSD, early decodes)", NULL };
  ADD(g, combo_row("Depth", NULL, depth, 2));
  ADD(g, switch_row("A-priori decoding", "use my call / the DX call to pull weak replies out of the noise", TRUE));
  ADD(g, switch_row("a7 and a8 decodes", "try the messages a station is expected to send next", TRUE));
  ADD(g, spin_row("Decode range", "upper edge in Hz (lower edge 200 Hz)", 1000, 5000, 4000));
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(p3), ADW_PREFERENCES_GROUP(g));
  g = group("Clock", "FT8 needs the computer clock within about a second of UTC.");
  ADD(g, spin_row("Warn when mean DT exceeds", "seconds", 0.1, 2.0, 0.5));
  ADD(g, switch_row("Show DT health in the status bar", NULL, TRUE));
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(p3), ADW_PREFERENCES_GROUP(g));
  adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(d), ADW_PREFERENCES_PAGE(p3));

  /* Logging */
  GtkWidget *p4 = adw_preferences_page_new();
  adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(p4), "Logging");
  adw_preferences_page_set_name(ADW_PREFERENCES_PAGE(p4), "logging");
  adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(p4), "document-save-symbolic");
  g = group("log-for-linux", "WSJT-X UDP protocol (schema 3): QSO Logged on Log QSO, decodes for highlighting worked-before.");
  ADD(g, entry_row("Host", NULL, "127.0.0.1"));
  ADD(g, spin_row("Port", NULL, 1, 65535, 2237));
  ADD(g, switch_row("Send decodes", "the logger answers with Highlight Callsign (worked before, new DXCC)", TRUE));
  ADD(g, switch_row("Also write ALL.TXT", "WSJT-X compatible text log of every decode", FALSE));
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(p4), ADW_PREFERENCES_GROUP(g));
  adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(d), ADW_PREFERENCES_PAGE(p4));

  /* Display */
  GtkWidget *p5 = adw_preferences_page_new();
  adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(p5), "Display");
  adw_preferences_page_set_name(ADW_PREFERENCES_PAGE(p5), "display");
  adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(p5), "preferences-desktop-display-symbolic");
  g = group("Waterfall", NULL);
  static const char *const pal[] = { "Blue → yellow (default)", "Grey", "Fire", NULL };
  ADD(g, combo_row("Palette", NULL, pal, 0));
  ADD(g, spin_row("Rows per second", NULL, 1, 4, 2));
  ADD(g, switch_row("Label decoded signals on the waterfall", NULL, TRUE));
  ADD(g, switch_row("Spectrum strip above the waterfall", NULL, TRUE));
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(p5), ADW_PREFERENCES_GROUP(g));
  g = group("Decode lists", NULL);
  ADD(g, switch_row("Highlight CQ", NULL, TRUE));
  ADD(g, switch_row("Dim stations worked before", "needs log-for-linux", TRUE));
  ADD(g, spin_row("Keep periods", "how many periods stay in Band activity", 1, 60, 20));
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(p5), ADW_PREFERENCES_GROUP(g));
  adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(d), ADW_PREFERENCES_PAGE(p5));

  pin_size_under_broadway(GTK_WINDOW(win), 760, 820);
  if (g_getenv("MOCK_PAGE")) adw_preferences_dialog_set_visible_page_name(ADW_PREFERENCES_DIALOG(d), g_getenv("MOCK_PAGE"));
  adw_dialog_present(d, win);
}

int main(int argc, char **argv) {
  AdwApplication *app = adw_application_new("cz.ok1br.ftx_for_linux.mockup2", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  int st = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return st;
}
