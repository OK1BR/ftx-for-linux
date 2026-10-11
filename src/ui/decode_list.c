/* decode_list.c — see decode_list.h.  Copyright (C) 2026 Richard Fakenberg,
 * OK1BR.  GPL-3.0-or-later. */
#include "decode_list.h"

#include <stdio.h>

struct _FtxDecodeList {
  GtkWidget parent_instance;
  GtkWidget *box, *list, *sw, *note;
  GArray   *hist;            /* every line in arrival order: the model the rows are rebuilt from */
  int  last_nutc;
  gboolean last_ft4;
  int  periods;
  int  cur_count;            /* lines of the current period (newest first: they sit at the top) */
  gboolean newest_first, follow, rebuilding;
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

/* the newest period's end: the top when newest first, else the bottom */
static gboolean scroll_to_newest(gpointer user)
{
  FtxDecodeList *l = user;
  GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(l->sw));
  if (l->newest_first) gtk_adjustment_set_value(adj, gtk_adjustment_get_lower(adj));
  else gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj));
  g_object_unref(l);
  return G_SOURCE_REMOVE;
}

static void queue_scroll(FtxDecodeList *l)
{
  g_idle_add_full(G_PRIORITY_LOW, scroll_to_newest, g_object_ref(l), NULL);
}

static gboolean at_newest(FtxDecodeList *l)
{
  GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(l->sw));
  if (l->newest_first) return gtk_adjustment_get_value(adj) <= gtk_adjustment_get_lower(adj) + 4;
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

static GtkWidget *make_row(const FtxDecodeRow *d)
{
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
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
  gtk_label_set_ellipsize(GTK_LABEL(lab), PANGO_ELLIPSIZE_END);   /* a narrow pane shortens the line, the window need not grow */
  gtk_widget_add_css_class(lab, "mono");
  gtk_box_append(GTK_BOX(box), lab);        /* the tags follow the text, not the far edge of the pane */
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
  return row;
}

/* Place one line: newest first keeps the current period's block at the top
 * (separator at index 0 on a period change, lines at cur_count so the block
 * reads in the decoder's order as the stages 41/47/50 arrive), else append. */
static void insert_row(FtxDecodeList *l, const FtxDecodeRow *d)
{
  gboolean stick = !l->rebuilding && at_newest(l);
  if (l->periods == 0) {
    l->periods = 1;
    l->cur_count = 0;
  } else if (d->nutc != l->last_nutc || d->ft4 != l->last_ft4) {
    if (l->newest_first) gtk_list_box_insert(GTK_LIST_BOX(l->list), separator_row(), 0);
    else gtk_list_box_append(GTK_LIST_BOX(l->list), separator_row());
    l->periods++;
    l->cur_count = 0;
  }
  l->last_nutc = d->nutc;
  l->last_ft4 = d->ft4;
  GtkWidget *row = make_row(d);
  if (l->newest_first) gtk_list_box_insert(GTK_LIST_BOX(l->list), row, l->cur_count);
  else gtk_list_box_append(GTK_LIST_BOX(l->list), row);
  l->cur_count++;
  if (!l->rebuilding && (l->follow || stick)) queue_scroll(l);
}

void ftx_decode_list_add(FtxDecodeList *l, const FtxDecodeRow *d)
{
  g_array_append_vals(l->hist, d, 1);
  insert_row(l, d);
}

static void rebuild(FtxDecodeList *l)
{
  gtk_list_box_remove_all(GTK_LIST_BOX(l->list));
  l->periods = 0;
  l->cur_count = 0;
  l->rebuilding = TRUE;
  for (guint i = 0; i < l->hist->len; i++) insert_row(l, &g_array_index(l->hist, FtxDecodeRow, i));
  l->rebuilding = FALSE;
  queue_scroll(l);
}

void ftx_decode_list_set_order(FtxDecodeList *l, gboolean newest_first)
{
  if (l->newest_first == newest_first) return;
  l->newest_first = newest_first;
  rebuild(l);
}

void ftx_decode_list_set_follow(FtxDecodeList *l, gboolean follow)
{
  l->follow = follow;
  if (follow) queue_scroll(l);
}

/* Drop the oldest period: the first block when oldest first (rows from index
 * 0 up to and including the separator), the last block when newest first
 * (rows from the end up to and including the separator above it); the
 * history loses its first run of equal nutc/ft4 either way. */
void ftx_decode_list_prune(FtxDecodeList *l, int keep_periods)
{
  while (l->periods > keep_periods) {
    GtkWidget *row;
    while ((row = l->newest_first ? gtk_widget_get_last_child(l->list)
                                  : GTK_WIDGET(gtk_list_box_get_row_at_index(GTK_LIST_BOX(l->list), 0))) != NULL) {
      gboolean sep = g_object_get_data(G_OBJECT(row), "separator") != NULL;
      gtk_list_box_remove(GTK_LIST_BOX(l->list), row);
      if (sep) break;
    }
    if (l->hist->len) {
      const FtxDecodeRow *first = &g_array_index(l->hist, FtxDecodeRow, 0);
      int nutc = first->nutc;
      gboolean ft4 = first->ft4;
      guint n = 0;
      while (n < l->hist->len && g_array_index(l->hist, FtxDecodeRow, n).nutc == nutc
             && g_array_index(l->hist, FtxDecodeRow, n).ft4 == ft4) n++;
      g_array_remove_range(l->hist, 0, n);
    }
    l->periods--;
  }
}

void ftx_decode_list_set_note(FtxDecodeList *l, const char *text)
{
  gtk_label_set_text(GTK_LABEL(l->note), text ? text : "");
}

void ftx_decode_list_clear(FtxDecodeList *l)
{
  gtk_list_box_remove_all(GTK_LIST_BOX(l->list));
  g_array_set_size(l->hist, 0);
  l->periods = 0;
  l->cur_count = 0;
}

static void ftx_decode_list_dispose(GObject *obj)
{
  FtxDecodeList *l = FTX_DECODE_LIST(obj);
  g_clear_pointer(&l->box, gtk_widget_unparent);
  g_clear_pointer(&l->hist, g_array_unref);
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
  l->hist = g_array_new(FALSE, FALSE, sizeof(FtxDecodeRow));
  l->follow = TRUE;
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
  l->note = gtk_label_new("");
  gtk_widget_add_css_class(l->note, "dim");
  gtk_widget_add_css_class(l->note, "mono");
  gtk_label_set_ellipsize(GTK_LABEL(l->note), PANGO_ELLIPSIZE_END);
  gtk_box_append(GTK_BOX(hdr), l->note);
  GtkWidget *cols = gtk_label_new("UTC     dB   DT  Freq  Message");
  gtk_label_set_xalign(GTK_LABEL(cols), 0);
  gtk_label_set_ellipsize(GTK_LABEL(cols), PANGO_ELLIPSIZE_END);
  gtk_widget_add_css_class(cols, "mono");
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
