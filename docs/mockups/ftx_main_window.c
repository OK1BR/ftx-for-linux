/* Mockup — FTX for Linux main window (libadwaita + Cairo). Throwaway design
 * reference: synthetic waterfall, decode lists and QSO panel, no engine, no
 * radio.  Compile standalone:
 *   cc ftx_main_window.c -o ftx-mock-main $(pkg-config --cflags --libs libadwaita-1) -lm
 * Copyright (C) 2026 Richard Fakenberg, OK1BR.  GPL-3.0-or-later. */
#include <adwaita.h>
#include <math.h>
#include <string.h>

/* ---- synthetic band ------------------------------------------------------- */
#define NSIGS 14
#define WF_ROWS 160
#define FMAX_HZ 4000.0
#define FMIN_HZ 0.0

typedef struct { double f, snr, dt; const char *msg; int kind; /* 0 plain 1 CQ 2 to me 3 worked-b4 */ } Sig;
static const Sig sigs[NSIGS] = {
  {  466, -2, 0.2, "N1PJT HB9CQK -10", 0 }, {  641, -12, 0.3, "N1JFU EA6EE R-07", 0 },
  {  723, -7, 0.1, "A92EE F5PSR -14", 3 }, { 1197, -2, -0.8, "CQ F5RXL IN94", 1 },
  { 1434, -15, 0.0, "CQ DX LU2XYZ GF05", 1 }, { 1512, -17, 0.6, "N1API F2VX 73", 0 },
  { 1648, -16, 0.1, "OK1BR EA3AGB -15", 2 }, { 2039, -20, 0.1, "K1JT HA5WA 73", 0 },
  { 2157, 12, -0.1, "WM3PEN EA6VQ -09", 0 }, { 2238, -14, 0.3, "N1API HA6FQ -23", 0 },
  { 2522, -7, 0.2, "K1BZM EA3CJ JN01", 3 }, { 2571, 16, 0.3, "W1FC F5BZB -08", 0 },
  { 2733, -6, 0.4, "W1DIG SV9CVY -14", 0 }, { 2852, -11, 0.2, "XE2X HA2NP RR73", 0 },
};
static double rx_hz = 1648, tx_hz = 1648;
/* The mockup lives at a fixed synthetic UTC so the decode rows (1334xx), the
 * period tags on the waterfall and the status clock all agree: 13:34:47 =
 * 2 s into the odd period 133445, our Tx3 is on the air. */
#define MOCK_UTC_SECS (13 * 3600 + 34 * 60 + 47)
/* what the QSO partner on 1648 Hz sent in the last periods (kind 4 = our own
 * transmission, printed in the Rx-frequency pane the way WSJT-X does) */
static const struct { const char *utc, *msg; int kind; } qso_rows[] = {
  { "133400", "CQ EA3AGB JN01", 1 }, { "133415", "EA3AGB OK1BR JO70", 4 },
  { "133430", "OK1BR EA3AGB -15", 2 }, { "133445", "EA3AGB OK1BR R-16", 4 },
};
static float wf[WF_ROWS][800];   /* waterfall rows, 5 Hz bins over 0-4000 Hz */
static int wf_head;
static unsigned rng = 12345;
static double frand(void) { rng = rng * 1103515245u + 12345u; return ((rng >> 8) & 0xffff) / 65536.0; }
static double gauss(void) { double u = frand() + 1e-9, v = frand(); return sqrt(-2 * log(u)) * cos(2 * M_PI * v); }

/* one new waterfall row: noise + FT8 "tones" wandering over 8 tones x 6.25 Hz */
static void wf_push_row(double t) {
  float *row = wf[wf_head];
  for (int i = 0; i < 800; i++) row[i] = (float)(0.0 + 0.9 * gauss() * 0.5);
  for (int s = 0; s < NSIGS; s++) {
    double tone = (int)(fabs(sin(t * 3.1 + s)) * 8) * 6.25;   /* pseudo symbol */
    double f = sigs[s].f + tone;
    double a = 3.5 + (sigs[s].snr + 24) / 6.0;
    for (int i = 0; i < 800; i++) {
      double d = (i * 5.0 - f) / 5.5;
      row[i] += (float)(a * exp(-d * d));
    }
  }
  wf_head = (wf_head + 1) % WF_ROWS;
}

static void heat(double v, double *r, double *g, double *b) {   /* 0..1 → dark blue → cyan → yellow → white */
  v = v < 0 ? 0 : v > 1 ? 1 : v;
  if (v < 0.35) { double k = v / 0.35; *r = 0.02; *g = 0.05 + 0.25 * k; *b = 0.12 + 0.55 * k; }
  else if (v < 0.7) { double k = (v - 0.35) / 0.35; *r = 0.02 + 0.9 * k; *g = 0.3 + 0.6 * k; *b = 0.67 - 0.5 * k; }
  else { double k = (v - 0.7) / 0.3; *r = 0.92 + 0.08 * k; *g = 0.9 + 0.1 * k; *b = 0.17 + 0.8 * k; }
}

static double x_of(double hz, int w) { return (hz - FMIN_HZ) / (FMAX_HZ - FMIN_HZ) * w; }

static void draw_waterfall(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer u) {
  (void)a; (void)u;
  const int axis_h = 22, spec_h = 72, gap = 6;
  cairo_set_source_rgb(cr, 0.04, 0.045, 0.06);
  cairo_rectangle(cr, 0, 0, w, h); cairo_fill(cr);
  cairo_select_font_face(cr, "Adwaita Mono", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

  /* frequency axis */
  cairo_set_font_size(cr, 11);
  for (int hz = 0; hz <= 4000; hz += 100) {
    double x = x_of(hz, w);
    int major = hz % 500 == 0;
    cairo_set_source_rgba(cr, 1, 1, 1, major ? 0.55 : 0.22);
    cairo_rectangle(cr, x, axis_h - (major ? 8 : 4), 1, major ? 8 : 4); cairo_fill(cr);
    if (major) {
      char t[16]; snprintf(t, sizeof t, "%d", hz);
      cairo_text_extents_t te; cairo_text_extents(cr, t, &te);
      cairo_move_to(cr, x - te.width / 2, 11); cairo_show_text(cr, t);
    }
  }
  /* spectrum strip: average of the last 4 rows */
  int y0 = axis_h + 2;
  cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
  for (int k = 1; k < 4; k++) { cairo_rectangle(cr, 0, y0 + spec_h * k / 4.0, w, 1); cairo_fill(cr); }
  cairo_set_source_rgba(cr, 0.55, 0.85, 1.0, 0.9);
  cairo_set_line_width(cr, 1.2);
  for (int i = 0; i < 800; i++) {
    double v = 0;
    for (int k = 1; k <= 4; k++) v += wf[(wf_head - k + WF_ROWS) % WF_ROWS][i];
    v /= 4;
    double y = y0 + spec_h - (v / 9.0) * spec_h;
    if (y < y0) y = y0;
    if (i == 0) cairo_move_to(cr, 0, y); else cairo_line_to(cr, i * w / 800.0, y);
  }
  cairo_stroke(cr);
  /* waterfall */
  int wy0 = y0 + spec_h + gap, wh = h - wy0;
  int rows = wh < WF_ROWS ? wh : WF_ROWS;
  double rh = (double)wh / rows;
  for (int r = 0; r < rows; r++) {
    float *row = wf[(wf_head - 1 - r + WF_ROWS) % WF_ROWS];
    for (int i = 0; i < 800; i++) {
      double rr, gg, bb; heat(row[i] / 7.0, &rr, &gg, &bb);
      cairo_set_source_rgb(cr, rr, gg, bb);
      cairo_rectangle(cr, i * w / 800.0, wy0 + r * rh, w / 800.0 + 0.6, rh + 0.6); cairo_fill(cr);
    }
  }
  /* period boundaries (every 15 s = 30 rows at 2 rows/s) with HHMMSS tags */
  cairo_set_font_size(cr, 10);
  int secs = MOCK_UTC_SECS;
  int into = secs % 15;
  for (int p = 0; p * 30 - into * 2 < rows; p++) {
    int r = p * 30 + into * 2;
    if (r <= 0 || r >= rows) continue;
    gboolean tag_fits = r >= 8;   /* tag sits above its line, inside the period it starts */
    double y = wy0 + r * rh;
    cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
    cairo_rectangle(cr, 0, y, w, 1); cairo_fill(cr);
    int ps = secs - into - p * 15;
    char t[16]; snprintf(t, sizeof t, "%02d%02d%02d", (ps / 3600 + 24) % 24, (ps / 60) % 60, (ps % 60 + 60) % 60);
    if (!tag_fits) continue;
    cairo_set_source_rgba(cr, 0, 0, 0, 0.55); cairo_rectangle(cr, w - 52, y - 15, 50, 13); cairo_fill(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.85); cairo_move_to(cr, w - 49, y - 5); cairo_show_text(cr, t);
  }
  /* decode labels on the signals of the last period */
  cairo_set_font_size(cr, 10.5);
  for (int s = 0; s < NSIGS; s++) {
    double x = x_of(sigs[s].f + 25, w);
    const char *call = sigs[s].msg;
    char lab[16]; const char *sp = strchr(call, ' ');
    if (sigs[s].kind == 1) { const char *q = strchr(sp + 1, ' '); snprintf(lab, sizeof lab, "CQ %.*s", q ? (int)(q - sp - 1) : 6, sp + 1); }
    else { const char *q = strchr(sp + 1, ' '); snprintf(lab, sizeof lab, "%.*s", q ? (int)(q - sp - 1) : 8, sp + 1); }
    cairo_text_extents_t te; cairo_text_extents(cr, lab, &te);
    double ly = wy0 + (into * 2 + 10) * rh + 10 + (s % 3) * 13;
    cairo_set_source_rgba(cr, 0, 0, 0, 0.6); cairo_rectangle(cr, x - te.width / 2 - 3, ly - 10, te.width + 6, 13); cairo_fill(cr);
    if (sigs[s].kind == 2) cairo_set_source_rgb(cr, 1.0, 0.62, 0.2);
    else if (sigs[s].kind == 1) cairo_set_source_rgb(cr, 0.55, 0.95, 0.55);
    else cairo_set_source_rgba(cr, 1, 1, 1, 0.8);
    cairo_move_to(cr, x - te.width / 2, ly); cairo_show_text(cr, lab);
  }
  /* Rx / Tx markers: brackets 50 Hz wide (FT8 occupied bandwidth) */
  double xr = x_of(rx_hz, w), xw = x_of(rx_hz + 50, w) - xr;
  cairo_set_source_rgba(cr, 0.3, 1.0, 0.4, 0.95); cairo_set_line_width(cr, 2);
  cairo_move_to(cr, xr, axis_h + 1); cairo_line_to(cr, xr, h); cairo_stroke(cr);
  cairo_set_source_rgba(cr, 0.3, 1.0, 0.4, 0.18); cairo_rectangle(cr, xr, axis_h + 1, xw, h); cairo_fill(cr);
  double xt = x_of(tx_hz, w);
  cairo_set_source_rgba(cr, 1.0, 0.3, 0.25, 0.95);
  cairo_move_to(cr, xt + xw, axis_h + 1); cairo_line_to(cr, xt + xw, h); cairo_stroke(cr);
  cairo_set_font_size(cr, 11);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.6); cairo_rectangle(cr, xr + xw + 3, axis_h + 3, 52, 28); cairo_fill(cr);
  cairo_set_source_rgba(cr, 0.3, 1.0, 0.4, 1); cairo_move_to(cr, xr + xw + 6, axis_h + 14); cairo_show_text(cr, "Rx 1648");
  cairo_set_source_rgba(cr, 1.0, 0.35, 0.3, 1); cairo_move_to(cr, xr + xw + 6, axis_h + 27); cairo_show_text(cr, "Tx 1648");
}

static gboolean tick(gpointer area) { wf_push_row(g_get_monotonic_time() / 1e6); gtk_widget_queue_draw(area); return G_SOURCE_CONTINUE; }

/* ---- decode lists --------------------------------------------------------- */
static GtkWidget *decode_row(const char *utc, int snr, double dt, int hz, const char *msg, int kind) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class(box, "decode");
  if (kind == 1) gtk_widget_add_css_class(box, "cq");
  if (kind == 2) gtk_widget_add_css_class(box, "tome");
  if (kind == 3) gtk_widget_add_css_class(box, "b4");
  if (kind == 4) gtk_widget_add_css_class(box, "tx");
  char buf[96], dts[G_ASCII_DTOSTR_BUF_SIZE];
  g_ascii_formatd(dts, sizeof dts, "%+4.1f", dt);   /* WSJT-X prints a dot, whatever the locale */
  if (kind == 4) snprintf(buf, sizeof buf, "%s  Tx      %4d  %s", utc, hz, msg);
  else snprintf(buf, sizeof buf, "%s %3d %s %4d  %s", utc, snr, dts, hz, msg);
  GtkWidget *l = gtk_label_new(buf);
  gtk_label_set_xalign(GTK_LABEL(l), 0);
  gtk_widget_add_css_class(l, "mono");
  gtk_widget_set_hexpand(l, TRUE);
  gtk_box_append(GTK_BOX(box), l);
  if (kind == 3) { GtkWidget *t = gtk_label_new("B4"); gtk_widget_add_css_class(t, "tag"); gtk_box_append(GTK_BOX(box), t); }
  if (kind == 2) { GtkWidget *t = gtk_label_new("→ me"); gtk_widget_add_css_class(t, "tag"); gtk_widget_add_css_class(t, "tag-me"); gtk_box_append(GTK_BOX(box), t); }
  return box;
}

static GtkWidget *decode_pane(const char *title, int rxonly) {
  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(hdr, "panehdr");
  GtkWidget *t = gtk_label_new(title); gtk_widget_add_css_class(t, "heading"); gtk_label_set_xalign(GTK_LABEL(t), 0); gtk_widget_set_hexpand(t, TRUE);
  gtk_box_append(GTK_BOX(hdr), t);
  GtkWidget *cols = gtk_label_new("UTC     dB   DT  Freq  Message");
  gtk_widget_add_css_class(cols, "mono"); gtk_widget_add_css_class(cols, "dim");
  gtk_box_append(GTK_BOX(v), hdr);
  gtk_box_append(GTK_BOX(v), cols);
  GtkWidget *list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
  gtk_widget_add_css_class(list, "decodes");
  const char *utcs[] = { "133400", "133415", "133430" };
  for (int p = 0; p < 3; p++) {
    for (int s = 0; s < NSIGS; s++) {
      gboolean partner = fabs(sigs[s].f - rx_hz) < 60;
      if (rxonly && !partner) continue;
      if (!rxonly && (s * 7 + p) % 5 == 0) continue;
      if (partner) {
        if (qso_rows[p].kind == 4 && !rxonly) continue;   /* band activity never lists our own TX */
        gtk_list_box_append(GTK_LIST_BOX(list), decode_row(qso_rows[p].utc, (int)sigs[s].snr + (p - 1), sigs[s].dt, (int)sigs[s].f, qso_rows[p].msg, qso_rows[p].kind));
      } else
        gtk_list_box_append(GTK_LIST_BOX(list), decode_row(utcs[p], (int)sigs[s].snr + (p - 1), sigs[s].dt, (int)sigs[s].f, sigs[s].msg, sigs[s].kind));
    }
    if (p < 2) { GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL); gtk_list_box_append(GTK_LIST_BOX(list), sep); }
  }
  if (rxonly)   /* the transmission in progress is printed as soon as it starts */
    gtk_list_box_append(GTK_LIST_BOX(list), decode_row(qso_rows[3].utc, 0, 0, (int)tx_hz, qso_rows[3].msg, 4));
  GtkWidget *sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), list);
  gtk_widget_set_vexpand(sw, TRUE);
  gtk_box_append(GTK_BOX(v), sw);
  return v;
}

/* ---- QSO / TX panel --------------------------------------------------------- */
static GtkWidget *field(const char *label, GtkWidget *w) {
  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *l = gtk_label_new(label); gtk_widget_add_css_class(l, "caption"); gtk_widget_add_css_class(l, "dim");
  gtk_label_set_xalign(GTK_LABEL(l), 0);
  gtk_box_append(GTK_BOX(v), l); gtk_box_append(GTK_BOX(v), w);
  return v;
}
static GtkWidget *mono_entry(const char *value, int chars) {
  GtkWidget *e = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(e), value);
  gtk_editable_set_width_chars(GTK_EDITABLE(e), chars);
  gtk_widget_add_css_class(e, "mono");
  return e;
}
static GtkWidget *tx_row(const char *tx, const char *msg, const char *tag, gboolean chk) {
  GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_add_css_class(h, "txrow");
  GtkWidget *r = gtk_check_button_new();
  static GtkWidget *group;
  if (group) gtk_check_button_set_group(GTK_CHECK_BUTTON(r), GTK_CHECK_BUTTON(group)); else group = r;
  if (chk) gtk_check_button_set_active(GTK_CHECK_BUTTON(r), TRUE);
  gtk_widget_set_valign(r, GTK_ALIGN_CENTER);
  GtkWidget *m = gtk_label_new(msg); gtk_widget_add_css_class(m, "mono"); gtk_label_set_xalign(GTK_LABEL(m), 0); gtk_widget_set_hexpand(m, TRUE);
  gtk_box_append(GTK_BOX(h), r); gtk_box_append(GTK_BOX(h), m);
  if (tag) { GtkWidget *n = gtk_label_new(tag); gtk_widget_add_css_class(n, "tag"); gtk_widget_add_css_class(n, "tag-now"); gtk_box_append(GTK_BOX(h), n); }
  GtkWidget *l = gtk_label_new(tx); gtk_widget_add_css_class(l, "dim"); gtk_widget_add_css_class(l, "mono");
  gtk_box_append(GTK_BOX(h), l);
  return h;
}
static GtkWidget *qso_panel(void) {
  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_add_css_class(v, "qso");
  gtk_widget_set_size_request(v, 380, -1);

  /* QSO partner: call, grid and the report we send in one row; below it what
   * log-for-linux / the prefix table know about him */
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_box_append(GTK_BOX(row), field("DX call", mono_entry("EA3AGB", 11)));
  gtk_box_append(GTK_BOX(row), field("DX grid", mono_entry("JN01", 6)));
  GtkWidget *sp = gtk_spin_button_new_with_range(-30, 30, 1); gtk_spin_button_set_value(GTK_SPIN_BUTTON(sp), -16);
  gtk_widget_add_css_class(sp, "mono");
  gtk_box_append(GTK_BOX(row), field("Report to send", sp));
  GtkWidget *rcv = gtk_label_new("−15"); gtk_widget_add_css_class(rcv, "mono"); gtk_widget_set_valign(rcv, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(row), field("Received", rcv));
  gtk_box_append(GTK_BOX(v), row);
  GtkWidget *info = gtk_label_new("Spain · EA · ITU 37 · CQ 14 · 1 430 km · 231° · not worked before on 20 m");
  gtk_widget_add_css_class(info, "dim"); gtk_label_set_xalign(GTK_LABEL(info), 0); gtk_label_set_wrap(GTK_LABEL(info), TRUE);
  gtk_box_append(GTK_BOX(v), info);

  /* the one line that matters: what is on the air (or goes out next) and when */
  GtkWidget *next = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_add_css_class(next, "nextcard");
  GtkWidget *nl = gtk_label_new("Transmitting · odd period 133445 · 13 s left"); gtk_widget_add_css_class(nl, "dim"); gtk_label_set_xalign(GTK_LABEL(nl), 0);
  GtkWidget *nh = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *nm = gtk_label_new("EA3AGB OK1BR R-16"); gtk_widget_add_css_class(nm, "nextmsg"); gtk_label_set_xalign(GTK_LABEL(nm), 0); gtk_widget_set_hexpand(nm, TRUE);
  GtkWidget *nt = gtk_label_new("Tx3 · 1648 Hz"); gtk_widget_add_css_class(nt, "dim"); gtk_widget_add_css_class(nt, "mono");
  gtk_box_append(GTK_BOX(nh), nm); gtk_box_append(GTK_BOX(nh), nt);
  gtk_box_append(GTK_BOX(next), nl); gtk_box_append(GTK_BOX(next), nh);
  gtk_box_append(GTK_BOX(v), next);

  GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *en = gtk_toggle_button_new_with_label("Enable TX"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(en), TRUE);
  gtk_widget_add_css_class(en, "txkey"); gtk_widget_set_hexpand(en, TRUE);
  GtkWidget *halt = gtk_button_new_with_label("Halt TX"); gtk_widget_add_css_class(halt, "destructive-action");
  GtkWidget *tune = gtk_button_new_with_label("Tune");
  GtkWidget *cq = gtk_button_new_with_label("Call CQ");
  gtk_box_append(GTK_BOX(btns), en); gtk_box_append(GTK_BOX(btns), halt); gtk_box_append(GTK_BOX(btns), tune); gtk_box_append(GTK_BOX(btns), cq);
  gtk_box_append(GTK_BOX(v), btns);

  /* the six standard messages, auto-sequenced; the radio marks what goes next */
  GtkWidget *th = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *tt = gtk_label_new("Transmit"); gtk_widget_add_css_class(tt, "heading"); gtk_label_set_xalign(GTK_LABEL(tt), 0); gtk_widget_set_hexpand(tt, TRUE);
  GtkWidget *al = gtk_label_new("Auto sequence"); gtk_widget_add_css_class(al, "dim");
  GtkWidget *auto_sw = gtk_switch_new(); gtk_switch_set_active(GTK_SWITCH(auto_sw), TRUE); gtk_widget_set_valign(auto_sw, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(th), tt); gtk_box_append(GTK_BOX(th), al); gtk_box_append(GTK_BOX(th), auto_sw);
  gtk_box_append(GTK_BOX(v), th);
  GtkWidget *list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
  gtk_widget_add_css_class(list, "boxed-list"); gtk_widget_add_css_class(list, "txlist");
  gtk_list_box_append(GTK_LIST_BOX(list), tx_row("Tx1", "EA3AGB OK1BR JO70", NULL, FALSE));
  gtk_list_box_append(GTK_LIST_BOX(list), tx_row("Tx2", "EA3AGB OK1BR -16", NULL, FALSE));
  gtk_list_box_append(GTK_LIST_BOX(list), tx_row("Tx3", "EA3AGB OK1BR R-16", "on air", TRUE));
  gtk_list_box_append(GTK_LIST_BOX(list), tx_row("Tx4", "EA3AGB OK1BR RR73", NULL, FALSE));
  gtk_list_box_append(GTK_LIST_BOX(list), tx_row("Tx5", "EA3AGB OK1BR 73", NULL, FALSE));
  gtk_list_box_append(GTK_LIST_BOX(list), tx_row("Tx6", "CQ OK1BR JO70", NULL, FALSE));
  gtk_box_append(GTK_BOX(v), list);

  GtkWidget *fr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *fl = gtk_label_new("Tx frequency"); gtk_widget_add_css_class(fl, "dim");
  GtkWidget *fs = gtk_spin_button_new_with_range(200, 3000, 1); gtk_spin_button_set_value(GTK_SPIN_BUTTON(fs), 1648); gtk_widget_add_css_class(fs, "mono");
  GtkWidget *hold = gtk_check_button_new_with_label("Hold"); gtk_check_button_set_active(GTK_CHECK_BUTTON(hold), TRUE);
  GtkWidget *lock = gtk_check_button_new_with_label("Tx = Rx");
  GtkWidget *spacer = gtk_label_new(""); gtk_widget_set_hexpand(spacer, TRUE);
  GtkWidget *logb = gtk_button_new_with_label("Log QSO  →  log-for-linux");
  gtk_widget_add_css_class(logb, "suggested-action");
  gtk_box_append(GTK_BOX(fr), fl); gtk_box_append(GTK_BOX(fr), fs); gtk_box_append(GTK_BOX(fr), hold); gtk_box_append(GTK_BOX(fr), lock);
  gtk_box_append(GTK_BOX(fr), spacer); gtk_box_append(GTK_BOX(fr), logb);
  gtk_box_append(GTK_BOX(v), fr);
  return v;
}

/* ---- status bar --------------------------------------------------------------- */
static GtkWidget *clock_label, *period_bar;
static void clock_set(void) {
  int secs = MOCK_UTC_SECS;
  char buf[32]; snprintf(buf, sizeof buf, "%02d:%02d:%02d UTC", secs / 3600, (secs / 60) % 60, secs % 60);
  gtk_label_set_text(GTK_LABEL(clock_label), buf);
  int into = secs % 15;
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(period_bar), into / 15.0);
  char pt[32]; snprintf(pt, sizeof pt, "%s period · %2d s", (secs / 15) % 2 ? "odd" : "even", into);
  gtk_progress_bar_set_text(GTK_PROGRESS_BAR(period_bar), pt);
}

static GtkWidget *status_item(const char *text, const char *cls) {
  GtkWidget *l = gtk_label_new(text);
  gtk_widget_add_css_class(l, "status");
  if (cls) gtk_widget_add_css_class(l, cls);
  return l;
}

static GtkWidget *status_bar(void) {
  GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_add_css_class(h, "statusbar");
  clock_label = status_item("13:34:28 UTC", "mono");
  period_bar = gtk_progress_bar_new(); gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(period_bar), TRUE);
  gtk_widget_set_size_request(period_bar, 230, -1); gtk_widget_set_valign(period_bar, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(h), clock_label);
  gtk_box_append(GTK_BOX(h), period_bar);
  gtk_box_append(GTK_BOX(h), status_item("● clock  mean DT −0.07 s", "ok"));
  gtk_box_append(GTK_BOX(h), status_item("● TX  1648 Hz · FT8 · 12 kHz TCI audio", "tx"));
  gtk_box_append(GTK_BOX(h), status_item("21 decodes · 1.9 s", NULL));
  GtkWidget *sp = gtk_label_new(""); gtk_widget_set_hexpand(sp, TRUE); gtk_box_append(GTK_BOX(h), sp);
  gtk_box_append(GTK_BOX(h), status_item("● TCI sdr-for-linux 192.168.1.18  DIGU", "ok"));
  gtk_box_append(GTK_BOX(h), status_item("● log-for-linux UDP 2237", "ok"));
  clock_set();
  return h;
}

/* ---- window -------------------------------------------------------------------- */
static void css_load(void) {
  GtkCssProvider *p = gtk_css_provider_new();
  const char *c =
    ".mono { font-family: \"Adwaita Mono\", monospace; }"
    ".dim { opacity: 0.6; }"
    ".dial { font-family: \"Adwaita Mono\", monospace; font-size: 22px; font-weight: 600; letter-spacing: 1px; }"
    ".band { opacity: 0.7; font-size: 13px; }"
    ".panehdr { padding: 6px 10px 2px 10px; }"
    ".decodes { background: transparent; }"
    ".decodes row { padding: 0; min-height: 0; }"
    ".decode { padding: 1px 8px; font-size: 13px; }"
    ".decode.cq { background: rgba(60,180,80,0.16); }"
    ".decode.tome { background: rgba(255,140,30,0.22); }"
    ".decode.b4 { opacity: 0.55; }"
    ".decode.tx { color: #ff9a8a; }"
    ".txlist row { padding: 0; min-height: 0; }"
    ".txrow { padding: 1px 10px; }"
    ".tag-now { background: #c8321e; color: #fff; }"
    ".status.tx { color: #ff9a8a; }"
    ".tag { font-size: 10px; padding: 0 5px; border-radius: 4px; background: rgba(255,255,255,0.12); margin-right: 4px; }"
    ".tag-me { background: rgba(255,140,30,0.5); }"
    ".tag-next { background: #1d6fa5; color: #fff; }"
    ".qso { padding: 10px 12px 10px 6px; }"
    ".txmsg { font-family: \"Adwaita Mono\", monospace; }"
    ".nextcard { padding: 8px 12px; border-radius: 8px; background: rgba(29,111,165,0.18); border: 1px solid rgba(29,111,165,0.6); }"
    ".nextmsg { font-family: \"Adwaita Mono\", monospace; font-size: 17px; font-weight: 700; }"
    "button.txkey { font-weight: 700; }"
    "button.txkey:checked { background: #c8321e; color: #fff; }"
    ".statusbar { padding: 4px 12px; font-size: 12px; border-top: 1px solid rgba(255,255,255,0.08); }"
    ".status.ok { color: #6fe08a; }"
    "button.mode:checked { background: #1d6fa5; color: #fff; }";
  gtk_css_provider_load_from_string(p, c);
  gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}



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
  css_load();
  GtkWidget *win = adw_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(win), "FTX for Linux");
  gtk_window_set_default_size(GTK_WINDOW(win), 1480, 920);

  GtkWidget *hdr = adw_header_bar_new();
  GtkWidget *title = adw_window_title_new("FTX for Linux", "OK1BR · JO70 · 20 m");
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(hdr), title);
  /* dial frequency from the radio over TCI, click = tune */
  GtkWidget *dialbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *dial = gtk_label_new("14.074.000"); gtk_widget_add_css_class(dial, "dial");
  GtkWidget *band = gtk_label_new("USB · DIGU"); gtk_widget_add_css_class(band, "band");
  gtk_box_append(GTK_BOX(dialbox), dial); gtk_box_append(GTK_BOX(dialbox), band);
  adw_header_bar_pack_start(ADW_HEADER_BAR(hdr), dialbox);
  GtkWidget *modes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0); gtk_widget_add_css_class(modes, "linked");
  GtkWidget *ft8 = gtk_toggle_button_new_with_label("FT8"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ft8), TRUE); gtk_widget_add_css_class(ft8, "mode");
  GtkWidget *ft4 = gtk_toggle_button_new_with_label("FT4"); gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(ft4), GTK_TOGGLE_BUTTON(ft8)); gtk_widget_add_css_class(ft4, "mode");
  gtk_box_append(GTK_BOX(modes), ft8); gtk_box_append(GTK_BOX(modes), ft4);
  adw_header_bar_pack_end(ADW_HEADER_BAR(hdr), gtk_menu_button_new());
  adw_header_bar_pack_end(ADW_HEADER_BAR(hdr), modes);
  GtkWidget *mon = gtk_toggle_button_new_with_label("Monitor"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mon), TRUE); gtk_widget_add_css_class(mon, "mode");
  adw_header_bar_pack_end(ADW_HEADER_BAR(hdr), mon);

  GtkWidget *area = gtk_drawing_area_new();
  gtk_widget_set_size_request(area, -1, 260);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_waterfall, NULL, NULL);
  for (int i = 0; i < WF_ROWS; i++) wf_push_row(i * 0.5);
  g_timeout_add(500, tick, area);

  GtkWidget *lower = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  GtkWidget *ba = decode_pane("Band activity", 0); gtk_widget_set_hexpand(ba, TRUE);
  GtkWidget *rxp = decode_pane("Rx frequency", 1); gtk_widget_set_hexpand(rxp, TRUE);
  gtk_box_append(GTK_BOX(lower), ba);
  gtk_box_append(GTK_BOX(lower), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
  gtk_box_append(GTK_BOX(lower), rxp);
  gtk_box_append(GTK_BOX(lower), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
  gtk_box_append(GTK_BOX(lower), qso_panel());

  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
  gtk_paned_set_start_child(GTK_PANED(paned), area);
  gtk_paned_set_end_child(GTK_PANED(paned), lower);
  gtk_paned_set_position(GTK_PANED(paned), 300);
  gtk_paned_set_resize_start_child(GTK_PANED(paned), FALSE);

  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append(GTK_BOX(v), hdr);
  gtk_box_append(GTK_BOX(v), paned);
  gtk_widget_set_vexpand(paned, TRUE);
  gtk_box_append(GTK_BOX(v), status_bar());
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(win), v);
  pin_size_under_broadway(GTK_WINDOW(win), 1480, 920);
  gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char **argv) {
  AdwApplication *app = adw_application_new("cz.ok1br.ftx_for_linux.mockup", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  int st = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return st;
}
