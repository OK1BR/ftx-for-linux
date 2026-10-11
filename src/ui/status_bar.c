/* status_bar.c — see status_bar.h.  Copyright (C) 2026 Richard Fakenberg,
 * OK1BR.  GPL-3.0-or-later. */
#include "status_bar.h"

#include <math.h>
#include <stdio.h>

struct _FtxStatusBar {
  GtkWidget parent_instance;
  GtkWidget *box, *clock, *health, *audio, *decodes, *tci, *logger;
};

G_DEFINE_FINAL_TYPE(FtxStatusBar, ftx_status_bar, GTK_TYPE_WIDGET)

static void set_state(GtkWidget *l, FtxStatusState st)
{
  gtk_widget_remove_css_class(l, "ok");
  gtk_widget_remove_css_class(l, "tx");
  gtk_widget_remove_css_class(l, "err");
  gtk_widget_remove_css_class(l, "dim");
  static const char *const cls[] = { NULL, "ok", "tx", "err", "dim" };
  if (st > 0 && st <= FTX_STATUS_DIM) gtk_widget_add_css_class(l, cls[st]);
}

static GtkWidget *item(GtkWidget *box, const char *text)
{
  GtkWidget *l = gtk_label_new(text);
  gtk_widget_add_css_class(l, "status");
  gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
  gtk_box_append(GTK_BOX(box), l);
  return l;
}

void ftx_status_bar_set_clock(FtxStatusBar *sb, int hh, int mm, int ss)
{
  char buf[32];
  snprintf(buf, sizeof buf, "%02d:%02d:%02d UTC", hh, mm, ss);
  gtk_label_set_text(GTK_LABEL(sb->clock), buf);
}

void ftx_status_bar_set_clock_health(FtxStatusBar *sb, gboolean show, gboolean have, double mean_dt, double limit)
{
  gtk_widget_set_visible(sb->health, show);
  if (!have) { gtk_label_set_text(GTK_LABEL(sb->health), "● clock  no decodes yet"); set_state(sb->health, FTX_STATUS_DIM); return; }
  char buf[64];
  if (fabs(mean_dt) > limit) {
    snprintf(buf, sizeof buf, "● CLOCK OFF?  mean DT %+.2f s", mean_dt);
    set_state(sb->health, FTX_STATUS_ERROR);
  } else {
    snprintf(buf, sizeof buf, "● clock  mean DT %+.2f s", mean_dt);
    set_state(sb->health, FTX_STATUS_OK);
  }
  gtk_label_set_text(GTK_LABEL(sb->health), buf);
}

void ftx_status_bar_set_audio(FtxStatusBar *sb, const char *text, FtxStatusState st) { gtk_label_set_text(GTK_LABEL(sb->audio), text); set_state(sb->audio, st); }
void ftx_status_bar_set_decodes(FtxStatusBar *sb, const char *text) { gtk_label_set_text(GTK_LABEL(sb->decodes), text); }
void ftx_status_bar_set_tci(FtxStatusBar *sb, const char *text, FtxStatusState st) { gtk_label_set_text(GTK_LABEL(sb->tci), text); set_state(sb->tci, st); }
void ftx_status_bar_set_logger(FtxStatusBar *sb, const char *text, FtxStatusState st) { gtk_label_set_text(GTK_LABEL(sb->logger), text); set_state(sb->logger, st); }

static void ftx_status_bar_dispose(GObject *obj)
{
  FtxStatusBar *sb = FTX_STATUS_BAR(obj);
  g_clear_pointer(&sb->box, gtk_widget_unparent);
  G_OBJECT_CLASS(ftx_status_bar_parent_class)->dispose(obj);
}

static void ftx_status_bar_class_init(FtxStatusBarClass *klass)
{
  G_OBJECT_CLASS(klass)->dispose = ftx_status_bar_dispose;
  gtk_widget_class_set_layout_manager_type(GTK_WIDGET_CLASS(klass), GTK_TYPE_BIN_LAYOUT);
}

static void ftx_status_bar_init(FtxStatusBar *sb)
{
  sb->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_add_css_class(sb->box, "statusbar");
  gtk_widget_set_parent(sb->box, GTK_WIDGET(sb));
  sb->clock = item(sb->box, "--:--:-- UTC");
  gtk_widget_add_css_class(sb->clock, "clock");
  sb->health = item(sb->box, "");
  sb->audio = item(sb->box, "");
  sb->decodes = item(sb->box, "");
  GtkWidget *sp = gtk_label_new("");
  gtk_widget_set_hexpand(sp, TRUE);
  gtk_box_append(GTK_BOX(sb->box), sp);
  sb->tci = item(sb->box, "");
  sb->logger = item(sb->box, "");
}

GtkWidget *ftx_status_bar_new(void) { return g_object_new(FTX_TYPE_STATUS_BAR, NULL); }
