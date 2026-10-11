/* decode_list.c — see decode_list.h.  Copyright (C) 2026 Richard Fakenberg,
 * OK1BR.  GPL-3.0-or-later. */
#include "decode_list.h"

#include <stdio.h>

struct _FtxDecodeList {
  GtkWidget parent_instance;
  GtkWidget *box, *list, *sw;
  int  last_nutc;
  gboolean last_ft4;
  int  periods;
};

G_DEFINE_FINAL_TYPE(FtxDecodeList, ftx_decode_list, GTK_TYPE_WIDGET)

enum { SIG_ACTIVATED, N_SIGNALS };
static guint signals[N_SIGNALS];

static void on_row_activated(GtkListBox *lb, GtkListBoxRow *row, gpointer user)
{
  (void)lb;
  FtxDecodeRow *d = g_object_get_data(G_OBJECT(row), "decode");
  if (d) g_signal_emit(user, signals[SIG_ACTIVATED], 0, d);
}

static gboolean scroll_to_end(gpointer user)
{
  FtxDecodeList *l = user;
  GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(l->sw));
  gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj));
  g_object_unref(l);
  return G_SOURCE_REMOVE;
}

static gboolean at_end(FtxDecodeList *l)
{
  GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(l->sw));
  return gtk_adjustment_get_value(adj) + gtk_adjustment_get_page_size(adj) >= gtk_adjustment_get_upper(adj) - 4;
}

static GtkWidget *separator_row(void)
{
  GtkWidget *row = gtk_list_box_row_new();
  gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
  gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
  gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
  g_object_set_data(G_OBJECT(row), "separator", GINT_TO_POINTER(1));
  return row;
}

void ftx_decode_list_add(FtxDecodeList *l, const FtxDecodeRow *d)
{
  gboolean stick = at_end(l);
  if (l->periods == 0) {
    l->periods = 1;
  } else if (d->nutc != l->last_nutc || d->ft4 != l->last_ft4) {
    gtk_list_box_append(GTK_LIST_BOX(l->list), separator_row());
    l->periods++;
  }
  l->last_nutc = d->nutc;
  l->last_ft4 = d->ft4;

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class(box, "decode");
  static const char *const cls[] = { NULL, "cq", "tome", "b4", "tx" };
  if (d->kind > 0 && d->kind <= FTX_DECODE_TX && cls[d->kind]) gtk_widget_add_css_class(box, cls[d->kind]);
  char buf[96], dts[G_ASCII_DTOSTR_BUF_SIZE];
  g_ascii_formatd(dts, sizeof dts, "%+4.1f", d->dt);        /* a dot whatever the locale */
  if (d->kind == FTX_DECODE_TX) snprintf(buf, sizeof buf, "%06d  Tx      %4d  %s", d->nutc, d->freq, d->msg);
  else snprintf(buf, sizeof buf, "%06d %3d %s %4d  %s", d->nutc, d->snr, dts, d->freq, d->msg);
  GtkWidget *lab = gtk_label_new(buf);
  gtk_label_set_xalign(GTK_LABEL(lab), 0);
  gtk_label_set_single_line_mode(GTK_LABEL(lab), TRUE);
  gtk_label_set_ellipsize(GTK_LABEL(lab), PANGO_ELLIPSIZE_END);   /* the panes share the width; long lines shorten, the window need not grow */
  gtk_widget_add_css_class(lab, "mono");
  gtk_widget_set_hexpand(lab, TRUE);
  gtk_box_append(GTK_BOX(box), lab);
  if (d->nap != 0) {   /* AP decode: jt9's "aN", and "?" when its quality is low */
    char a[8];
    snprintf(a, sizeof a, "a%d", d->nap % 10);
    GtkWidget *t = gtk_label_new(a); gtk_widget_add_css_class(t, "tag"); gtk_widget_add_css_class(t, "dim");
    gtk_widget_set_tooltip_text(t, "A-priori decode: my call or the DX call were assumed");
    gtk_box_append(GTK_BOX(box), t);
    if (d->qual < 0.17f) {
      GtkWidget *q = gtk_label_new("?"); gtk_widget_add_css_class(q, "tag"); gtk_widget_add_css_class(q, "tag-me");
      gtk_widget_set_tooltip_text(q, "Low confidence — may be a false decode");
      gtk_box_append(GTK_BOX(box), q);
    }
  }
  if (d->kind == FTX_DECODE_B4) {
    GtkWidget *t = gtk_label_new("B4"); gtk_widget_add_css_class(t, "tag"); gtk_box_append(GTK_BOX(box), t);
  } else if (d->kind == FTX_DECODE_TO_ME) {
    GtkWidget *t = gtk_label_new("→ me"); gtk_widget_add_css_class(t, "tag"); gtk_widget_add_css_class(t, "tag-me");
    gtk_box_append(GTK_BOX(box), t);
  }
  GtkWidget *row = gtk_list_box_row_new();
  gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
  g_object_set_data_full(G_OBJECT(row), "decode", g_memdup2(d, sizeof *d), g_free);
  gtk_list_box_append(GTK_LIST_BOX(l->list), row);
  if (stick) g_idle_add_full(G_PRIORITY_LOW, scroll_to_end, g_object_ref(l), NULL);
}

void ftx_decode_list_prune(FtxDecodeList *l, int keep_periods)
{
  while (l->periods > keep_periods) {
    GtkWidget *row;
    while ((row = GTK_WIDGET(gtk_list_box_get_row_at_index(GTK_LIST_BOX(l->list), 0))) != NULL) {
      gboolean sep = g_object_get_data(G_OBJECT(row), "separator") != NULL;
      gtk_list_box_remove(GTK_LIST_BOX(l->list), row);
      if (sep) break;
    }
    l->periods--;
  }
}

void ftx_decode_list_clear(FtxDecodeList *l)
{
  gtk_list_box_remove_all(GTK_LIST_BOX(l->list));
  l->periods = 0;
}

static void ftx_decode_list_dispose(GObject *obj)
{
  FtxDecodeList *l = FTX_DECODE_LIST(obj);
  g_clear_pointer(&l->box, gtk_widget_unparent);
  G_OBJECT_CLASS(ftx_decode_list_parent_class)->dispose(obj);
}

static void ftx_decode_list_class_init(FtxDecodeListClass *klass)
{
  G_OBJECT_CLASS(klass)->dispose = ftx_decode_list_dispose;
  gtk_widget_class_set_layout_manager_type(GTK_WIDGET_CLASS(klass), GTK_TYPE_BIN_LAYOUT);
  signals[SIG_ACTIVATED] = g_signal_new("decode-activated", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
                                        NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);
}

static void ftx_decode_list_init(FtxDecodeList *l)
{
  l->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_parent(l->box, GTK_WIDGET(l));
  GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(hdr, "panehdr");
  GtkWidget *title = gtk_label_new("");
  gtk_widget_add_css_class(title, "heading");
  gtk_label_set_xalign(GTK_LABEL(title), 0);
  gtk_widget_set_hexpand(title, TRUE);
  gtk_box_append(GTK_BOX(hdr), title);
  g_object_set_data(G_OBJECT(l), "title", title);
  GtkWidget *cols = gtk_label_new("UTC     dB   DT  Freq  Message");
  gtk_label_set_xalign(GTK_LABEL(cols), 0);
  gtk_label_set_ellipsize(GTK_LABEL(cols), PANGO_ELLIPSIZE_END);
  gtk_widget_add_css_class(cols, "mono");
  gtk_widget_add_css_class(cols, "dim");
  gtk_widget_add_css_class(cols, "colhdr");
  gtk_box_append(GTK_BOX(l->box), hdr);
  gtk_box_append(GTK_BOX(l->box), cols);
  l->list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(l->list), GTK_SELECTION_SINGLE);
  gtk_widget_add_css_class(l->list, "decodes");
  g_signal_connect(l->list, "row-activated", G_CALLBACK(on_row_activated), l);
  l->sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(l->sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(l->sw), l->list);
  gtk_widget_set_vexpand(l->sw, TRUE);
  gtk_box_append(GTK_BOX(l->box), l->sw);
}

GtkWidget *ftx_decode_list_new(const char *title)
{
  FtxDecodeList *l = g_object_new(FTX_TYPE_DECODE_LIST, NULL);
  gtk_label_set_text(GTK_LABEL(g_object_get_data(G_OBJECT(l), "title")), title);
  return GTK_WIDGET(l);
}
