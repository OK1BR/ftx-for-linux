/* qso_panel.c — see qso_panel.h.  The layout follows
 * docs/mockups/ftx_main_window_v2.c (Richard 2026-10-11: "mockupy vypadají
 * dobře").  Copyright (C) 2026 Richard Fakenberg, OK1BR.  GPL-3.0-or-later. */
#include "qso_panel.h"

#include <stdio.h>
#include <string.h>

struct _FtxQsoPanel {
  GtkWidget parent_instance;
  GtkWidget *box;
  GtkWidget *dx_call, *dx_grid, *report, *received;
  int report_db;
  GtkWidget *call_lab, *info_lab;
  GtkWidget *txgroup, *badge, *card_state, *card_msg, *card_menu;
  GtkWidget *enable, *halt, *tune, *cq, *auto_sw;
  GtkWidget *tx_row[6], *tx_radio[6], *tx_label[6], *tx_tag[6];
  GtkWidget *tx_hz, *hold, *tx_eq_rx, *log;
  int updating;
};

G_DEFINE_FINAL_TYPE(FtxQsoPanel, ftx_qso_panel, GTK_TYPE_WIDGET)

enum { SIG_CHANGED, SIG_ACTION, N_SIGNALS };
static guint signals[N_SIGNALS];

static void changed(FtxQsoPanel *p)
{
  if (p->updating == 0) g_signal_emit(p, signals[SIG_CHANGED], 0);
}

static void on_any_changed(GtkWidget *w, gpointer user) { (void)w; changed(user); }
static void on_notify(GObject *o, GParamSpec *ps, gpointer user) { (void)o; (void)ps; changed(user); }

static void on_radio_toggled(GtkCheckButton *b, gpointer user)
{
  if (gtk_check_button_get_active(b)) changed(user);
}

static void on_call_commit(GtkWidget *w, gpointer user)
{
  FtxQsoPanel *p = user;
  GtkEditable *e = GTK_EDITABLE(w);
  char *up = g_ascii_strup(gtk_editable_get_text(e), -1);
  g_strstrip(up);
  if (strcmp(up, gtk_editable_get_text(e)) != 0) {
    p->updating++;
    gtk_editable_set_text(e, up);
    p->updating--;
  }
  g_free(up);
  changed(p);
}

static void on_focus_leave(GtkEventControllerFocus *c, gpointer user)
{
  on_call_commit(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(c)), user);
}

static void action(GtkWidget *w, gpointer user)
{
  FtxQsoPanel *p = user;
  int a = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "action"));
  if ((a == FTX_QSO_ACTION_ENABLE_TX || a == FTX_QSO_ACTION_TUNE) && p->updating) return;
  g_signal_emit(p, signals[SIG_ACTION], 0, a);
}

static void on_row_clicked(GtkGestureClick *g, int n, double x, double y, gpointer user)
{
  (void)g; (void)n; (void)x; (void)y;
  gtk_check_button_set_active(GTK_CHECK_BUTTON(user), TRUE);
}

static GtkWidget *caption(const char *text)
{
  GtkWidget *l = gtk_label_new(text);
  gtk_widget_add_css_class(l, "cap");
  gtk_label_set_xalign(GTK_LABEL(l), 0);
  return l;
}

static GtkWidget *field(const char *label, GtkWidget *w)
{
  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_box_append(GTK_BOX(v), caption(label));
  gtk_box_append(GTK_BOX(v), w);
  return v;
}

static GtkWidget *mono_entry(int chars)
{
  GtkWidget *e = gtk_entry_new();
  gtk_editable_set_width_chars(GTK_EDITABLE(e), chars);
  gtk_widget_add_css_class(e, "mono");
  return e;
}

static GtkWidget *button(FtxQsoPanel *p, const char *label, FtxQsoAction a, const char *cls)
{
  GtkWidget *b = gtk_button_new_with_label(label);
  if (cls) gtk_widget_add_css_class(b, cls);
  g_object_set_data(G_OBJECT(b), "action", GINT_TO_POINTER(a));
  g_signal_connect(b, "clicked", G_CALLBACK(action), p);
  return b;
}

/* everything that is rarely touched: the next message and the sequencer, the
 * Tx frequency with Hold and Tx = Rx, the DX call and grid by hand (a sked) */
static GtkWidget *build_popover(FtxQsoPanel *p)
{
  GtkWidget *pop = gtk_popover_new();
  GtkWidget *pv = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_size_request(pv, 440, -1);

  GtkWidget *th = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *tt = gtk_label_new("Next message");
  gtk_widget_add_css_class(tt, "heading");
  gtk_label_set_xalign(GTK_LABEL(tt), 0);
  gtk_widget_set_hexpand(tt, TRUE);
  GtkWidget *al = gtk_label_new("Auto sequence");
  gtk_widget_add_css_class(al, "dim");
  p->auto_sw = gtk_switch_new();
  gtk_widget_set_valign(p->auto_sw, GTK_ALIGN_CENTER);
  g_signal_connect(p->auto_sw, "notify::active", G_CALLBACK(on_notify), p);
  gtk_box_append(GTK_BOX(th), tt);
  gtk_box_append(GTK_BOX(th), al);
  gtk_box_append(GTK_BOX(th), p->auto_sw);
  gtk_box_append(GTK_BOX(pv), th);

  GtkWidget *list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
  gtk_widget_add_css_class(list, "boxed-list");
  gtk_widget_add_css_class(list, "txlist");
  for (int i = 0; i < 6; i++) {
    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(h, "txrow");
    p->tx_radio[i] = gtk_check_button_new();
    if (i > 0) gtk_check_button_set_group(GTK_CHECK_BUTTON(p->tx_radio[i]), GTK_CHECK_BUTTON(p->tx_radio[0]));
    gtk_widget_set_valign(p->tx_radio[i], GTK_ALIGN_CENTER);
    g_signal_connect(p->tx_radio[i], "toggled", G_CALLBACK(on_radio_toggled), p);
    p->tx_label[i] = gtk_label_new("");
    gtk_widget_add_css_class(p->tx_label[i], "mono");
    gtk_label_set_xalign(GTK_LABEL(p->tx_label[i]), 0);
    gtk_widget_set_hexpand(p->tx_label[i], TRUE);
    p->tx_tag[i] = gtk_label_new("on air");
    gtk_widget_add_css_class(p->tx_tag[i], "tag");
    gtk_widget_add_css_class(p->tx_tag[i], "tag-now");
    gtk_widget_set_visible(p->tx_tag[i], FALSE);
    char n[8];
    snprintf(n, sizeof n, "Tx%d", i + 1);
    GtkWidget *nl = gtk_label_new(n);
    gtk_widget_add_css_class(nl, "dim");
    gtk_widget_add_css_class(nl, "mono");
    gtk_box_append(GTK_BOX(h), p->tx_radio[i]);
    gtk_box_append(GTK_BOX(h), p->tx_label[i]);
    gtk_box_append(GTK_BOX(h), p->tx_tag[i]);
    gtk_box_append(GTK_BOX(h), nl);
    GtkGesture *g = gtk_gesture_click_new();
    g_signal_connect(g, "released", G_CALLBACK(on_row_clicked), p->tx_radio[i]);
    gtk_widget_add_controller(p->tx_label[i], GTK_EVENT_CONTROLLER(g));
    p->tx_row[i] = h;
    gtk_list_box_append(GTK_LIST_BOX(list), h);
  }
  gtk_box_append(GTK_BOX(pv), list);

  GtkWidget *fr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  p->tx_hz = gtk_spin_button_new_with_range(200, 5000, 1);
  gtk_widget_add_css_class(p->tx_hz, "mono");
  g_signal_connect(p->tx_hz, "value-changed", G_CALLBACK(on_any_changed), p);
  gtk_box_append(GTK_BOX(fr), field("Tx frequency, Hz", p->tx_hz));
  GtkWidget *checks = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_valign(checks, GTK_ALIGN_END);
  p->hold = gtk_check_button_new_with_label("Hold Tx frequency");
  gtk_widget_set_tooltip_text(p->hold, "Keep the Tx frequency when answering a station");
  g_signal_connect(p->hold, "toggled", G_CALLBACK(on_any_changed), p);
  p->tx_eq_rx = gtk_check_button_new_with_label("Tx = Rx");
  g_signal_connect(p->tx_eq_rx, "toggled", G_CALLBACK(on_any_changed), p);
  gtk_box_append(GTK_BOX(checks), p->hold);
  gtk_box_append(GTK_BOX(checks), p->tx_eq_rx);
  gtk_box_append(GTK_BOX(fr), checks);
  gtk_box_append(GTK_BOX(pv), fr);

  GtkWidget *dxrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  p->dx_call = mono_entry(10);
  g_signal_connect(p->dx_call, "activate", G_CALLBACK(on_call_commit), p);
  GtkEventController *fc = gtk_event_controller_focus_new();
  g_signal_connect(fc, "leave", G_CALLBACK(on_focus_leave), p);
  gtk_widget_add_controller(p->dx_call, fc);
  p->dx_grid = mono_entry(6);
  g_signal_connect(p->dx_grid, "activate", G_CALLBACK(on_call_commit), p);
  fc = gtk_event_controller_focus_new();
  g_signal_connect(fc, "leave", G_CALLBACK(on_focus_leave), p);
  gtk_widget_add_controller(p->dx_grid, fc);
  gtk_box_append(GTK_BOX(dxrow), field("DX call by hand (a sked)", p->dx_call));
  gtk_box_append(GTK_BOX(dxrow), field("DX grid", p->dx_grid));
  gtk_box_append(GTK_BOX(pv), dxrow);

  gtk_popover_set_child(GTK_POPOVER(pop), pv);
  return pop;
}

static void ftx_qso_panel_init(FtxQsoPanel *p)
{
  p->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_add_css_class(p->box, "console");
  gtk_widget_set_parent(p->box, GTK_WIDGET(p));

  /* QSO group: whom we work, the two reports, Log QSO */
  GtkWidget *q = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_add_css_class(q, "group");
  gtk_widget_set_size_request(q, 540, -1);
  GtkWidget *q1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  p->call_lab = gtk_label_new("—");
  gtk_widget_add_css_class(p->call_lab, "dxcall");
  gtk_label_set_xalign(GTK_LABEL(p->call_lab), 0);
  p->info_lab = gtk_label_new("");
  gtk_widget_add_css_class(p->info_lab, "dim");
  gtk_label_set_xalign(GTK_LABEL(p->info_lab), 0);
  gtk_label_set_ellipsize(GTK_LABEL(p->info_lab), PANGO_ELLIPSIZE_END);
  gtk_widget_set_valign(p->info_lab, GTK_ALIGN_BASELINE_FILL);
  gtk_widget_set_hexpand(p->info_lab, TRUE);
  gtk_box_append(GTK_BOX(q1), p->call_lab);
  gtk_box_append(GTK_BOX(q1), p->info_lab);
  GtkWidget *q2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  /* the report to send is informative only: it is the partner's SNR from the
   * decoder and is never corrected by hand (Richard 2026-10-11) */
  p->report = gtk_label_new("—");
  gtk_widget_add_css_class(p->report, "rcvd");
  p->received = gtk_label_new("—");
  gtk_widget_add_css_class(p->received, "rcvd");
  gtk_box_append(GTK_BOX(q2), caption("Report to send"));
  gtk_box_append(GTK_BOX(q2), p->report);
  GtkWidget *gap = gtk_label_new("");
  gtk_widget_set_size_request(gap, 10, -1);
  gtk_box_append(GTK_BOX(q2), gap);
  gtk_box_append(GTK_BOX(q2), caption("received"));
  gtk_box_append(GTK_BOX(q2), p->received);
  GtkWidget *spc = gtk_label_new("");
  gtk_widget_set_hexpand(spc, TRUE);
  gtk_box_append(GTK_BOX(q2), spc);
  p->log = button(p, "Log QSO", FTX_QSO_ACTION_LOG_QSO, "suggested-action");
  gtk_widget_set_tooltip_text(p->log, "Log the QSO to log-for-linux");
  gtk_box_append(GTK_BOX(q2), p->log);
  gtk_box_append(GTK_BOX(q), q1);
  gtk_box_append(GTK_BOX(q), q2);

  /* Transmit group: the badge, the message, the popover button; the state
   * line and the four buttons */
  p->txgroup = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_add_css_class(p->txgroup, "group");
  gtk_widget_set_hexpand(p->txgroup, TRUE);
  GtkWidget *t1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  p->badge = gtk_label_new("RX");
  gtk_widget_add_css_class(p->badge, "badge");
  gtk_widget_add_css_class(p->badge, "rx");
  gtk_widget_set_valign(p->badge, GTK_ALIGN_CENTER);
  p->card_msg = gtk_label_new("");
  gtk_widget_add_css_class(p->card_msg, "nextmsg");
  gtk_label_set_xalign(GTK_LABEL(p->card_msg), 0);
  gtk_widget_set_hexpand(p->card_msg, TRUE);
  gtk_label_set_ellipsize(GTK_LABEL(p->card_msg), PANGO_ELLIPSIZE_END);
  p->card_menu = gtk_menu_button_new();
  gtk_widget_add_css_class(p->card_menu, "flat");
  gtk_widget_add_css_class(p->card_menu, "mono");
  gtk_widget_set_valign(p->card_menu, GTK_ALIGN_CENTER);
  gtk_widget_set_tooltip_text(p->card_menu, "Next message, Auto sequence, Tx frequency, DX call by hand");
  gtk_menu_button_set_popover(GTK_MENU_BUTTON(p->card_menu), build_popover(p));
  gtk_box_append(GTK_BOX(t1), p->badge);
  gtk_box_append(GTK_BOX(t1), p->card_msg);
  gtk_box_append(GTK_BOX(t1), p->card_menu);
  GtkWidget *t2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  p->card_state = gtk_label_new("");
  gtk_widget_add_css_class(p->card_state, "dim");
  gtk_label_set_xalign(GTK_LABEL(p->card_state), 0);
  gtk_label_set_ellipsize(GTK_LABEL(p->card_state), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand(p->card_state, TRUE);
  gtk_box_append(GTK_BOX(t2), p->card_state);
  GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_set_homogeneous(GTK_BOX(btns), TRUE);
  p->cq = button(p, "Call CQ", FTX_QSO_ACTION_CALL_CQ, NULL);
  p->enable = gtk_toggle_button_new_with_label("Enable TX");
  gtk_widget_add_css_class(p->enable, "txkey");
  g_object_set_data(G_OBJECT(p->enable), "action", GINT_TO_POINTER(FTX_QSO_ACTION_ENABLE_TX));
  g_signal_connect(p->enable, "toggled", G_CALLBACK(action), p);
  p->halt = button(p, "Halt TX", FTX_QSO_ACTION_HALT_TX, NULL);   /* a plain button: the libadwaita destructive-action red was too loud (Richard 2026-10-11) */
  gtk_widget_set_tooltip_text(p->halt, "Stop transmitting and disable TX (Esc)");
  p->tune = gtk_toggle_button_new_with_label("Tune");
  gtk_widget_set_tooltip_text(p->tune, "A steady tone at the Tx frequency while on");
  g_object_set_data(G_OBJECT(p->tune), "action", GINT_TO_POINTER(FTX_QSO_ACTION_TUNE));
  g_signal_connect(p->tune, "toggled", G_CALLBACK(action), p);
  GtkWidget *bs[] = { p->cq, p->enable, p->halt, p->tune };
  for (gsize i = 0; i < G_N_ELEMENTS(bs); i++) {
    gtk_widget_set_size_request(bs[i], 112, -1);
    gtk_box_append(GTK_BOX(btns), bs[i]);
  }
  gtk_box_append(GTK_BOX(t2), btns);
  gtk_box_append(GTK_BOX(p->txgroup), t1);
  gtk_box_append(GTK_BOX(p->txgroup), t2);

  gtk_box_append(GTK_BOX(p->box), q);
  gtk_box_append(GTK_BOX(p->box), p->txgroup);
}

/* ---- API ------------------------------------------------------------------ */

static void partner_refresh(FtxQsoPanel *p)
{
  const char *call = gtk_editable_get_text(GTK_EDITABLE(p->dx_call));
  const char *info = g_object_get_data(G_OBJECT(p->info_lab), "info");
  if (!call[0]) {
    gtk_label_set_text(GTK_LABEL(p->call_lab), "—");
    gtk_label_set_text(GTK_LABEL(p->info_lab), "no QSO — double-click a decode, or Call CQ");
    return;
  }
  gtk_label_set_text(GTK_LABEL(p->call_lab), call);
  gtk_label_set_text(GTK_LABEL(p->info_lab), info ? info : "");
}

void ftx_qso_panel_set_dx(FtxQsoPanel *p, const char *call, const char *grid)
{
  p->updating++;
  gtk_editable_set_text(GTK_EDITABLE(p->dx_call), call ? call : "");
  gtk_editable_set_text(GTK_EDITABLE(p->dx_grid), grid ? grid : "");
  p->updating--;
  partner_refresh(p);
}
const char *ftx_qso_panel_get_dx_call(FtxQsoPanel *p) { return gtk_editable_get_text(GTK_EDITABLE(p->dx_call)); }
const char *ftx_qso_panel_get_dx_grid(FtxQsoPanel *p) { return gtk_editable_get_text(GTK_EDITABLE(p->dx_grid)); }
void ftx_qso_panel_set_report(FtxQsoPanel *p, int r)
{
  char t[8];
  p->report_db = CLAMP(r, -50, 49);
  snprintf(t, sizeof t, "%+d", p->report_db);
  gtk_label_set_text(GTK_LABEL(p->report), t);
}
int  ftx_qso_panel_get_report(FtxQsoPanel *p) { return p->report_db; }
void ftx_qso_panel_set_received(FtxQsoPanel *p, const char *t) { gtk_label_set_text(GTK_LABEL(p->received), t && t[0] ? t : "—"); }
void ftx_qso_panel_set_info(FtxQsoPanel *p, const char *t)
{
  g_object_set_data_full(G_OBJECT(p->info_lab), "info", g_strdup(t ? t : ""), g_free);
  partner_refresh(p);
}

void ftx_qso_panel_set_card(FtxQsoPanel *p, const char *state, const char *message, const char *detail, FtxTxBadge badge)
{
  gtk_label_set_text(GTK_LABEL(p->card_state), state ? state : "");
  gtk_label_set_text(GTK_LABEL(p->card_msg), message ? message : "");
  gtk_menu_button_set_label(GTK_MENU_BUTTON(p->card_menu), detail && detail[0] ? detail : "Tx");
  static const char *const text[] = { "RX", "ARMED", "ON AIR", "TUNE" };
  static const char *const cls[]  = { "rx", "armed", "tx", "tx" };
  gtk_label_set_text(GTK_LABEL(p->badge), text[badge]);
  for (gsize i = 0; i < G_N_ELEMENTS(cls); i++) gtk_widget_remove_css_class(p->badge, cls[i]);
  gtk_widget_add_css_class(p->badge, cls[badge]);
  gtk_widget_remove_css_class(p->txgroup, "onair");
  gtk_widget_remove_css_class(p->txgroup, "armed");
  if (badge == FTX_TX_BADGE_ON_AIR || badge == FTX_TX_BADGE_TUNE) gtk_widget_add_css_class(p->txgroup, "onair");
  else if (badge == FTX_TX_BADGE_ARMED) gtk_widget_add_css_class(p->txgroup, "armed");
}

void ftx_qso_panel_set_message(FtxQsoPanel *p, int i, const char *text, gboolean valid)
{
  if (i < 0 || i > 5) return;
  gtk_label_set_text(GTK_LABEL(p->tx_label[i]), text ? text : "");
  if (valid || !text || !text[0]) gtk_widget_remove_css_class(p->tx_row[i], "invalid");
  else gtk_widget_add_css_class(p->tx_row[i], "invalid");
  gtk_widget_set_tooltip_text(p->tx_label[i], (!valid && text && text[0]) ? "Does not pack as a standard message" : NULL);
}

void ftx_qso_panel_set_next(FtxQsoPanel *p, int i)
{
  if (i < 0 || i > 5) return;
  p->updating++;
  gtk_check_button_set_active(GTK_CHECK_BUTTON(p->tx_radio[i]), TRUE);
  p->updating--;
}

int ftx_qso_panel_get_next(FtxQsoPanel *p)
{
  for (int i = 0; i < 6; i++) if (gtk_check_button_get_active(GTK_CHECK_BUTTON(p->tx_radio[i]))) return i;
  return 0;
}

void ftx_qso_panel_set_on_air(FtxQsoPanel *p, int i)
{
  for (int k = 0; k < 6; k++) gtk_widget_set_visible(p->tx_tag[k], k == i);
}

void ftx_qso_panel_set_tx_hz(FtxQsoPanel *p, int hz) { p->updating++; gtk_spin_button_set_value(GTK_SPIN_BUTTON(p->tx_hz), hz); p->updating--; }
int  ftx_qso_panel_get_tx_hz(FtxQsoPanel *p) { return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(p->tx_hz)); }
void ftx_qso_panel_set_hold(FtxQsoPanel *p, gboolean v) { p->updating++; gtk_check_button_set_active(GTK_CHECK_BUTTON(p->hold), v); p->updating--; }
gboolean ftx_qso_panel_get_hold(FtxQsoPanel *p) { return gtk_check_button_get_active(GTK_CHECK_BUTTON(p->hold)); }
void ftx_qso_panel_set_tx_eq_rx(FtxQsoPanel *p, gboolean v) { p->updating++; gtk_check_button_set_active(GTK_CHECK_BUTTON(p->tx_eq_rx), v); p->updating--; }
gboolean ftx_qso_panel_get_tx_eq_rx(FtxQsoPanel *p) { return gtk_check_button_get_active(GTK_CHECK_BUTTON(p->tx_eq_rx)); }
void ftx_qso_panel_set_auto_seq(FtxQsoPanel *p, gboolean v) { p->updating++; gtk_switch_set_active(GTK_SWITCH(p->auto_sw), v); p->updating--; }
gboolean ftx_qso_panel_get_auto_seq(FtxQsoPanel *p) { return gtk_switch_get_active(GTK_SWITCH(p->auto_sw)); }
void ftx_qso_panel_set_tx_enabled(FtxQsoPanel *p, gboolean v) { p->updating++; gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->enable), v); p->updating--; }
gboolean ftx_qso_panel_get_tx_enabled(FtxQsoPanel *p) { return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->enable)); }

void ftx_qso_panel_set_tune(FtxQsoPanel *p, gboolean v) { p->updating++; gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->tune), v); p->updating--; }
gboolean ftx_qso_panel_get_tune(FtxQsoPanel *p) { return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->tune)); }

void ftx_qso_panel_set_tx_available(FtxQsoPanel *p, gboolean available, const char *why)
{
  GtkWidget *w[] = { p->enable, p->halt, p->tune };
  for (gsize i = 0; i < G_N_ELEMENTS(w); i++) {
    gtk_widget_set_sensitive(w[i], available);
    gtk_widget_set_tooltip_text(w[i], available ? (w[i] == p->tune ? "A steady tone at the Tx frequency while on"
                                                 : w[i] == p->halt ? "Stop transmitting and disable TX (Esc)" : NULL) : why);
  }
}

void ftx_qso_panel_set_log_available(FtxQsoPanel *p, gboolean available, const char *why)
{
  gtk_widget_set_sensitive(p->log, available);
  gtk_widget_set_tooltip_text(p->log, available ? NULL : why);
}

static void ftx_qso_panel_dispose(GObject *obj)
{
  FtxQsoPanel *p = FTX_QSO_PANEL(obj);
  g_clear_pointer(&p->box, gtk_widget_unparent);
  G_OBJECT_CLASS(ftx_qso_panel_parent_class)->dispose(obj);
}

static void ftx_qso_panel_class_init(FtxQsoPanelClass *klass)
{
  G_OBJECT_CLASS(klass)->dispose = ftx_qso_panel_dispose;
  gtk_widget_class_set_layout_manager_type(GTK_WIDGET_CLASS(klass), GTK_TYPE_BIN_LAYOUT);
  signals[SIG_CHANGED] = g_signal_new("changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  signals[SIG_ACTION] = g_signal_new("action", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);
}

GtkWidget *ftx_qso_panel_new(void) { return g_object_new(FTX_TYPE_QSO_PANEL, NULL); }
