/* main.c — FTX for Linux: the GTK4/libadwaita application.
 *
 * Part of FTX for Linux.  Copyright (C) 2026 Richard Fakenberg, OK1BR.
 * GPL-3.0-or-later.  No warranty.
 */
#include <adwaita.h>
#include <locale.h>

#include "app/settings.h"
#include "window.h"

static FtxSettings settings;

static void act_quit(GSimpleAction *a, GVariant *p, gpointer app)
{
  (void)a; (void)p;
  GtkWindow *win = gtk_application_get_active_window(GTK_APPLICATION(app));
  if (win) gtk_window_close(win);
  else g_application_quit(G_APPLICATION(app));
}

static void activate(GtkApplication *app, gpointer user)
{
  (void)user;
  GtkWindow *existing = gtk_application_get_active_window(app);
  if (existing) { gtk_window_present(existing); return; }
  setlocale(LC_NUMERIC, "C");     /* decimal points everywhere, as WSJT-X and jt9 print them */
  adw_style_manager_set_color_scheme(adw_style_manager_get_default(), ADW_COLOR_SCHEME_FORCE_DARK);
  GtkCssProvider *css = gtk_css_provider_new();
  gtk_css_provider_load_from_resource(css, "/cz/ok1br/ftx_for_linux/style.css");
  gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(css);
  GtkWidget *win = ftx_window_new(ADW_APPLICATION(app), &settings);
  gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char **argv)
{
  ftx_settings_defaults(&settings);
  ftx_settings_load(&settings);
  AdwApplication *app = adw_application_new("cz.ok1br.ftx_for_linux", G_APPLICATION_DEFAULT_FLAGS);
  const GActionEntry entries[] = { { "quit", act_quit, NULL, NULL, NULL, { 0 } } };
  g_action_map_add_action_entries(G_ACTION_MAP(app), entries, G_N_ELEMENTS(entries), app);
  const char *quit_accels[] = { "<Control>q", NULL }, *prefs_accels[] = { "<Control>comma", NULL };
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.quit", quit_accels);
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.preferences", prefs_accels);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
