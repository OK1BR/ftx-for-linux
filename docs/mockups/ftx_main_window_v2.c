/* Mockup v2 — FTX for Linux main window, the second layout pass
 * (2026-10-11, after the first live afternoon: Richard — "zatím se mi to zdá
 * takové moc nedodělané").  Throwaway design reference, synthetic data, no
 * engine, no radio.  Compile standalone:
 *   cc ftx_main_window_v2.c -o ftx-mock-v2 $(pkg-config --cflags --libs libadwaita-1) -lm
 *   MOCK_STATE=idle ./ftx-mock-v2      (default: mid-QSO, our Tx3 on the air)
 * Design size = the window as it is tiled on Richard's desk: 1519 x 1364.
 * Copyright (C) 2026 Richard Fakenberg, OK1BR.  GPL-3.0-or-later. */
#include <adwaita.h>
#include <math.h>
#include <string.h>

/* ---- synthetic band ------------------------------------------------------- */
#define NSIGS 14
#define WF_ROWS 420
#define ROWS_PER_S 8
#define PERIOD_ROWS (15 * ROWS_PER_S)
#define FMAX_HZ 3000.0
#define FMIN_HZ 0.0

typedef struct { double f, snr, dt; const char *msg, *call; int kind; /* 0 plain 1 CQ 2 to me 3 worked-b4 */ } Sig;
static const Sig sigs[NSIGS] = {
  {  466, -2, 0.2, "N1PJT HB9CQK -10", "HB9CQK", 0 }, {  641, -12, 0.3, "N1JFU EA6EE R-07", "EA6EE", 0 },
  {  723, -7, 0.1, "A92EE F5PSR -14", "F5PSR", 3 }, { 1197, -2, -0.8, "CQ F5RXL IN94", "F5RXL", 1 },
  { 1434, -15, 0.0, "CQ DX LU2XYZ GF05", "LU2XYZ", 1 }, { 1512, -17, 0.6, "N1API F2VX 73", "F2VX", 0 },
  { 1648, -16, 0.1, "OK1BR EA3AGB -15", "EA3AGB", 2 }, { 2039, -20, 0.1, "K1JT HA5WA 73", "HA5WA", 0 },
  { 2157, 12, -0.1, "WM3PEN EA6VQ -09", "EA6VQ", 0 }, { 2238, -14, 0.3, "N1API HA6FQ -23", "HA6FQ", 0 },
  { 2522, -7, 0.2, "K1BZM EA3CJ JN01", "EA3CJ", 3 }, { 2571, 16, 0.3, "W1FC F5BZB -08", "F5BZB", 0 },
  { 2733, -6, 0.4, "W1DIG SV9CVY -14", "SV9CVY", 0 }, { 2852, -11, 0.2, "XE2X HA2NP RR73", "HA2NP", 0 },
};
static double rx_hz = 1648, tx_hz = 1648;
static gboolean idle_state;
/* 13:34:47 = 2 s into the odd period 133445; mid-QSO: our Tx3 is on the air */
#define MOCK_UTC_SECS (13 * 3600 + 34 * 60 + 47)
static const struct { const char *utc, *msg; int kind; } qso_rows[] = {
  { "133400", "CQ EA3AGB JN01", 1 }, { "133415", "EA3AGB OK1BR JO70", 4 },
  { "133430", "OK1BR EA3AGB -15", 2 }, { "133445", "EA3AGB OK1BR R-16", 4 },
};
static float wf[WF_ROWS][600];   /* 5 Hz bins over 0-3000 Hz */
static int wf_head;
static unsigned rng = 12345;
static double frand(void) { rng = rng * 1103515245u + 12345u; return ((rng >> 8) & 0xffff) / 65536.0; }
static double gauss(void) { double u = frand() + 1e-9, v = frand(); return sqrt(-2 * log(u)) * cos(2 * M_PI * v); }

static void wf_push_row(double t) {
  float *row = wf[wf_head];
  for (int i = 0; i < 600; i++) row[i] = (float)(0.45 * gauss());
  for (int s = 0; s < NSIGS; s++) {
    double tone = (int)(fabs(sin(t * 3.1 + s)) * 8) * 6.25;
    double f = sigs[s].f + tone;
    double a = 3.5 + (sigs[s].snr + 24) / 6.0;
    for (int i = 0; i < 600; i++) { double d = (i * 5.0 - f) / 5.5; row[i] += (float)(a * exp(-d * d)); }
  }
  wf_head = (wf_head + 1) % WF_ROWS;
}

/* "Mono white", the palette Richard runs (the family table of sdr-for-linux) */
static void mono_white(double v, double *g) {
  static const double st[][2] = { {0.00, 0}, {0.30, 6}, {0.58, 90}, {0.80, 210}, {1.00, 255} };
  v = v < 0 ? 0 : v > 1 ? 1 : v;
  int s = 0; while (s < 3 && v > st[s + 1][0]) s++;
  double f = (v - st[s][0]) / (st[s + 1][0] - st[s][0]);
  *g = (st[s][1] + f * (st[s + 1][1] - st[s][1])) / 255.0;
}
static double x_of(double hz, int w) { return (hz - FMIN_HZ) / (FMAX_HZ - FMIN_HZ) * w; }

static void draw_waterfall(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer u) {
  (void)a; (void)u;
  const int axis_h = 24;
  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_rectangle(cr, 0, 0, w, h); cairo_fill(cr);
  cairo_select_font_face(cr, "Adwaita Mono", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

  /* frequency axis with the Rx (green) and Tx (red) markers as flags in it */
  cairo_set_font_size(cr, 11);
  for (int hz = 0; hz <= FMAX_HZ; hz += 100) {
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
  /* rows */
  int wy0 = axis_h + 1, wh = h - wy0;
  int rows = wh < WF_ROWS ? wh : WF_ROWS;
  for (int r = 0; r < rows; r++) {
    float *row = wf[(wf_head - 1 - r + WF_ROWS) % WF_ROWS];
    for (int i = 0; i < 600; i++) {
      double g; mono_white((row[i] + 0.6) / 8.5, &g);
      if (g < 0.02) continue;
      cairo_set_source_rgb(cr, g, g, g);
      cairo_rectangle(cr, i * w / 600.0, wy0 + r, w / 600.0 + 0.6, 1); cairo_fill(cr);
    }
  }
  /* period boundaries with the HHMMSS tag of the period that starts there */
  cairo_set_font_size(cr, 10);
  int secs = MOCK_UTC_SECS, into = secs % 15;
  for (int p = 0; ; p++) {
    int r = into * ROWS_PER_S + p * PERIOD_ROWS;
    if (r >= rows) break;
    double y = wy0 + r;
    cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
    cairo_rectangle(cr, 0, y, w, 1); cairo_fill(cr);
    int ps = secs - into - p * 15;
    char t[16]; snprintf(t, sizeof t, "%02d%02d%02d", (ps / 3600 + 24) % 24, (ps / 60) % 60, (ps % 60 + 60) % 60);
    if (r >= 14) {
      cairo_set_source_rgba(cr, 0, 0, 0, 0.55); cairo_rectangle(cr, w - 52, y - 15, 50, 13); cairo_fill(cr);
      cairo_set_source_rgba(cr, 1, 1, 1, 0.85); cairo_move_to(cr, w - 49, y - 5); cairo_show_text(cr, t);
    }
    /* labels of the decodes of the completed periods, anchored 10 s into the period */
    if (p >= 1) {
      int ra = r - 10 * ROWS_PER_S;
      cairo_set_font_size(cr, 10.5);
      double last_right = -1e9; int lane = 0;
      for (int s = 0; s < NSIGS; s++) {
        double x = x_of(sigs[s].f + 25, w);
        cairo_text_extents_t te; cairo_text_extents(cr, sigs[s].call, &te);
        double left = x - te.width / 2 - 3;
        lane = left < last_right + 4 ? (lane + 1) % 3 : 0;      /* a label that would overlap drops a lane */
        if (lane == 0) last_right = left + te.width + 6;
        double ly = wy0 + ra + 10 + lane * 13;
        cairo_set_source_rgba(cr, 0, 0, 0, 0.6); cairo_rectangle(cr, left, ly - 10, te.width + 6, 13); cairo_fill(cr);
        if (sigs[s].kind == 2) cairo_set_source_rgb(cr, 1.0, 0.62, 0.2);
        else if (sigs[s].kind == 1) cairo_set_source_rgb(cr, 0.55, 0.95, 0.55);
        else cairo_set_source_rgba(cr, 1, 1, 1, 0.8);
        cairo_move_to(cr, x - te.width / 2, ly); cairo_show_text(cr, sigs[s].call);
      }
      cairo_set_font_size(cr, 10);
    }
  }
  /* Rx / Tx brackets, one FT8 bandwidth wide, with flags in the axis strip
   * (the floating Rx/Tx readout box is gone — the numbers live in the console) */
  double xr = x_of(rx_hz, w), xw = x_of(rx_hz + 50, w) - xr, xt = x_of(tx_hz, w);
  cairo_set_line_width(cr, 2);
  cairo_set_source_rgba(cr, 0.3, 1.0, 0.4, 0.95);
  cairo_move_to(cr, xr, axis_h); cairo_line_to(cr, xr, h); cairo_stroke(cr);
  cairo_set_source_rgba(cr, 0.3, 1.0, 0.4, 0.18); cairo_rectangle(cr, xr, axis_h, xw, h); cairo_fill(cr);
  cairo_set_source_rgba(cr, 0.3, 1.0, 0.4, 1);
  cairo_move_to(cr, xr - 6, 2); cairo_line_to(cr, xr + 6, 2); cairo_line_to(cr, xr, 12); cairo_close_path(cr); cairo_fill(cr);
  cairo_set_source_rgba(cr, 1.0, 0.3, 0.25, 0.95);
  cairo_move_to(cr, xt + xw, axis_h); cairo_line_to(cr, xt + xw, h); cairo_stroke(cr);
  cairo_set_source_rgba(cr, 1.0, 0.3, 0.25, 1);
  cairo_move_to(cr, xt + xw - 6, 2); cairo_line_to(cr, xt + xw + 6, 2); cairo_line_to(cr, xt + xw, 12); cairo_close_path(cr); cairo_fill(cr);
}

static gboolean tick(gpointer area) { wf_push_row(g_get_monotonic_time() / 1e6); gtk_widget_queue_draw(area); return G_SOURCE_CONTINUE; }

/* ---- the period line: full width, 8 px, fills over the 15 s ----------------- */
static void draw_period_line(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer u) {
  (void)a; (void)u;
  int secs = MOCK_UTC_SECS, into = secs % 15;
  gboolean odd = (secs / 15) % 2, ours = odd;          /* we transmit in the odd period */
  cairo_set_source_rgba(cr, 1, 1, 1, 0.10); cairo_rectangle(cr, 0, 0, w, h); cairo_fill(cr);
  if (ours && !idle_state) cairo_set_source_rgb(cr, 0.78, 0.2, 0.12);     /* our slot, TX enabled */
  else cairo_set_source_rgba(cr, 0.43, 0.88, 0.54, 0.8);                  /* receiving */
  cairo_rectangle(cr, 0, 0, w * (into + 0.4) / 15.0, h); cairo_fill(cr);
}

/* ---- decode lists --------------------------------------------------------- */
static GtkWidget *decode_row(const char *utc, int snr, double dt, int hz, const char *msg, int kind) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_add_css_class(box, "decode");
  if (kind == 1) gtk_widget_add_css_class(box, "cq");
  if (kind == 2) gtk_widget_add_css_class(box, "tome");
  if (kind == 3) gtk_widget_add_css_class(box, "b4");
  if (kind == 4) gtk_widget_add_css_class(box, "tx");
  char buf[96], dts[G_ASCII_DTOSTR_BUF_SIZE];
  g_ascii_formatd(dts, sizeof dts, "%+4.1f", dt);
  if (kind == 4) snprintf(buf, sizeof buf, "%s  Tx      %4d  %-22s", utc, hz, msg);
  else snprintf(buf, sizeof buf, "%s %3d %s %4d  %-22s", utc, snr, dts, hz, msg);
  GtkWidget *l = gtk_label_new(buf);
  gtk_label_set_xalign(GTK_LABEL(l), 0);
  gtk_widget_add_css_class(l, "mono");
  gtk_box_append(GTK_BOX(box), l);          /* the tags sit right after the text, not at the far edge */
  if (kind == 3) { GtkWidget *t = gtk_label_new("B4"); gtk_widget_add_css_class(t, "tag"); gtk_box_append(GTK_BOX(box), t); }
  if (kind == 2) { GtkWidget *t = gtk_label_new("→ me"); gtk_widget_add_css_class(t, "tag"); gtk_widget_add_css_class(t, "tag-me"); gtk_box_append(GTK_BOX(box), t); }
  return box;
}

static GtkWidget *decode_pane(const char *title, const char *right, int rxonly) {
  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(hdr, "panehdr");
  GtkWidget *t = gtk_label_new(title); gtk_widget_add_css_class(t, "heading"); gtk_label_set_xalign(GTK_LABEL(t), 0); gtk_widget_set_hexpand(t, TRUE);
  GtkWidget *rt = gtk_label_new(right); gtk_widget_add_css_class(rt, "dim"); gtk_widget_add_css_class(rt, "mono");
  gtk_box_append(GTK_BOX(hdr), t); gtk_box_append(GTK_BOX(hdr), rt);
  GtkWidget *cols = gtk_label_new("UTC     dB   DT  Freq  Message");
  gtk_label_set_xalign(GTK_LABEL(cols), 0);
  gtk_widget_add_css_class(cols, "mono"); gtk_widget_add_css_class(cols, "colhdr");
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
        if (qso_rows[p].kind == 4 && !rxonly) continue;
        gtk_list_box_append(GTK_LIST_BOX(list), decode_row(qso_rows[p].utc, (int)sigs[s].snr + (p - 1), sigs[s].dt, (int)sigs[s].f, qso_rows[p].msg, qso_rows[p].kind));
      } else
        gtk_list_box_append(GTK_LIST_BOX(list), decode_row(utcs[p], (int)sigs[s].snr + (p - 1), sigs[s].dt, (int)sigs[s].f, sigs[s].msg, sigs[s].kind));
    }
    if (p < 2) gtk_list_box_append(GTK_LIST_BOX(list), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
  }
  if (rxonly && !idle_state) gtk_list_box_append(GTK_LIST_BOX(list), decode_row(qso_rows[3].utc, 0, 0, (int)tx_hz, qso_rows[3].msg, 4));
  GtkWidget *sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), list);
  gtk_widget_set_vexpand(sw, TRUE);
  gtk_box_append(GTK_BOX(v), sw);
  return v;
}

/* ---- the console: QSO group + TX group ------------------------------------- */
static GtkWidget *caption(const char *text) {
  GtkWidget *l = gtk_label_new(text); gtk_widget_add_css_class(l, "cap"); gtk_label_set_xalign(GTK_LABEL(l), 0);
  return l;
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

/* everything that is rarely touched lives in one popover under the "TxN · Hz" button */
static GtkWidget *tx_popover(void) {
  GtkWidget *pop = gtk_popover_new();
  GtkWidget *pv = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_size_request(pv, 440, -1);
  GtkWidget *th = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *tt = gtk_label_new("Next message"); gtk_widget_add_css_class(tt, "heading"); gtk_label_set_xalign(GTK_LABEL(tt), 0); gtk_widget_set_hexpand(tt, TRUE);
  GtkWidget *al = gtk_label_new("Auto sequence"); gtk_widget_add_css_class(al, "dim");
  GtkWidget *sw = gtk_switch_new(); gtk_switch_set_active(GTK_SWITCH(sw), TRUE); gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(th), tt); gtk_box_append(GTK_BOX(th), al); gtk_box_append(GTK_BOX(th), sw);
  gtk_box_append(GTK_BOX(pv), th);
  GtkWidget *list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
  gtk_widget_add_css_class(list, "boxed-list"); gtk_widget_add_css_class(list, "txlist");
  gtk_list_box_append(GTK_LIST_BOX(list), tx_row("Tx1", "EA3AGB OK1BR JO70", NULL, FALSE));
  gtk_list_box_append(GTK_LIST_BOX(list), tx_row("Tx2", "EA3AGB OK1BR -16", NULL, FALSE));
  gtk_list_box_append(GTK_LIST_BOX(list), tx_row("Tx3", "EA3AGB OK1BR R-16", "on air", TRUE));
  gtk_list_box_append(GTK_LIST_BOX(list), tx_row("Tx4", "EA3AGB OK1BR RR73", NULL, FALSE));
  gtk_list_box_append(GTK_LIST_BOX(list), tx_row("Tx5", "EA3AGB OK1BR 73", NULL, FALSE));
  gtk_list_box_append(GTK_LIST_BOX(list), tx_row("Tx6", "CQ OK1BR JO70", NULL, FALSE));
  gtk_box_append(GTK_BOX(pv), list);
  GtkWidget *fr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *fs = gtk_spin_button_new_with_range(200, 5000, 1); gtk_spin_button_set_value(GTK_SPIN_BUTTON(fs), tx_hz); gtk_widget_add_css_class(fs, "mono");
  GtkWidget *fv = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2); gtk_box_append(GTK_BOX(fv), caption("Tx frequency, Hz")); gtk_box_append(GTK_BOX(fv), fs);
  GtkWidget *hold = gtk_check_button_new_with_label("Hold Tx frequency"); gtk_check_button_set_active(GTK_CHECK_BUTTON(hold), TRUE);
  GtkWidget *lock = gtk_check_button_new_with_label("Tx = Rx");
  GtkWidget *cv = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_widget_set_valign(cv, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(cv), hold); gtk_box_append(GTK_BOX(cv), lock);
  gtk_box_append(GTK_BOX(fr), fv); gtk_box_append(GTK_BOX(fr), cv);
  gtk_box_append(GTK_BOX(pv), fr);
  GtkWidget *dx = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *dv = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2); gtk_box_append(GTK_BOX(dv), caption("DX call by hand (a sked)")); gtk_box_append(GTK_BOX(dv), mono_entry("EA3AGB", 10));
  GtkWidget *gv = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2); gtk_box_append(GTK_BOX(gv), caption("DX grid")); gtk_box_append(GTK_BOX(gv), mono_entry("JN01", 6));
  gtk_box_append(GTK_BOX(dx), dv); gtk_box_append(GTK_BOX(dx), gv);
  gtk_box_append(GTK_BOX(pv), dx);
  gtk_popover_set_child(GTK_POPOVER(pop), pv);
  return pop;
}

static GtkWidget *console(void) {
  GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_add_css_class(h, "console");

  /* QSO group: whom we work, the two reports, Log QSO */
  GtkWidget *q = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_add_css_class(q, "group");
  gtk_widget_set_size_request(q, 540, -1);
  GtkWidget *q1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *call = gtk_label_new(idle_state ? "—" : "EA3AGB"); gtk_widget_add_css_class(call, "dxcall"); gtk_label_set_xalign(GTK_LABEL(call), 0);
  GtkWidget *info = gtk_label_new(idle_state ? "no QSO — double-click a decode, or Call CQ" : "JN01 · 1 430 km · 231° · Spain");
  gtk_widget_add_css_class(info, "dim"); gtk_label_set_xalign(GTK_LABEL(info), 0); gtk_widget_set_valign(info, GTK_ALIGN_BASELINE_FILL); gtk_widget_set_hexpand(info, TRUE);
  gtk_label_set_ellipsize(GTK_LABEL(info), PANGO_ELLIPSIZE_END);
  gtk_box_append(GTK_BOX(q1), call); gtk_box_append(GTK_BOX(q1), info);
  GtkWidget *q2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *sp = gtk_spin_button_new_with_range(-50, 49, 1); gtk_spin_button_set_value(GTK_SPIN_BUTTON(sp), idle_state ? 8 : -16);
  gtk_widget_add_css_class(sp, "mono"); gtk_editable_set_width_chars(GTK_EDITABLE(sp), 3);
  GtkWidget *rc = gtk_label_new(idle_state ? "—" : "−15"); gtk_widget_add_css_class(rc, "rcvd");
  gtk_box_append(GTK_BOX(q2), caption("Report to send")); gtk_box_append(GTK_BOX(q2), sp);
  GtkWidget *gap = gtk_label_new(""); gtk_widget_set_size_request(gap, 10, -1); gtk_box_append(GTK_BOX(q2), gap);
  gtk_box_append(GTK_BOX(q2), caption("received")); gtk_box_append(GTK_BOX(q2), rc);
  GtkWidget *spc = gtk_label_new(""); gtk_widget_set_hexpand(spc, TRUE); gtk_box_append(GTK_BOX(q2), spc);
  GtkWidget *logb = gtk_button_new_with_label("Log QSO"); gtk_widget_add_css_class(logb, "suggested-action");
  gtk_widget_set_sensitive(logb, !idle_state);
  gtk_box_append(GTK_BOX(q2), logb);
  gtk_box_append(GTK_BOX(q), q1); gtk_box_append(GTK_BOX(q), q2);

  /* TX group: the badge no theme can recolour, the message, the period and the four buttons */
  GtkWidget *t = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_add_css_class(t, "group");
  if (!idle_state) gtk_widget_add_css_class(t, "onair");
  gtk_widget_set_hexpand(t, TRUE);
  GtkWidget *t1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *badge = gtk_label_new(idle_state ? "RX" : "ON AIR"); gtk_widget_add_css_class(badge, "badge"); gtk_widget_add_css_class(badge, idle_state ? "rx" : "tx");
  gtk_widget_set_valign(badge, GTK_ALIGN_CENTER);
  GtkWidget *msg = gtk_label_new(idle_state ? "CQ OK1BR JO70" : "EA3AGB OK1BR R-16"); gtk_widget_add_css_class(msg, "nextmsg"); gtk_label_set_xalign(GTK_LABEL(msg), 0); gtk_widget_set_hexpand(msg, TRUE);
  GtkWidget *mb = gtk_menu_button_new(); gtk_menu_button_set_label(GTK_MENU_BUTTON(mb), idle_state ? "Tx6 · 1648 Hz" : "Tx3 · 1648 Hz");
  gtk_widget_add_css_class(mb, "flat"); gtk_widget_add_css_class(mb, "mono"); gtk_widget_set_valign(mb, GTK_ALIGN_CENTER);
  gtk_menu_button_set_popover(GTK_MENU_BUTTON(mb), tx_popover());
  gtk_box_append(GTK_BOX(t1), badge); gtk_box_append(GTK_BOX(t1), msg); gtk_box_append(GTK_BOX(t1), mb);
  GtkWidget *t2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *st = gtk_label_new(idle_state ? "Receiving · even period 133445 · 13 s left · our slot next" : "Transmitting · odd period 133445 · 13 s left · auto sequence");
  gtk_widget_add_css_class(st, "dim"); gtk_label_set_xalign(GTK_LABEL(st), 0); gtk_widget_set_hexpand(st, TRUE); gtk_label_set_ellipsize(GTK_LABEL(st), PANGO_ELLIPSIZE_END);
  gtk_box_append(GTK_BOX(t2), st);
  GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_set_homogeneous(GTK_BOX(btns), TRUE);
  GtkWidget *cq = gtk_button_new_with_label("Call CQ");
  GtkWidget *en = gtk_toggle_button_new_with_label("Enable TX"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(en), !idle_state); gtk_widget_add_css_class(en, "txkey");
  GtkWidget *halt = gtk_button_new_with_label("Halt TX"); gtk_widget_add_css_class(halt, "destructive-action");
  GtkWidget *tune = gtk_toggle_button_new_with_label("Tune");
  GtkWidget *bs[] = { cq, en, halt, tune };
  for (int i = 0; i < 4; i++) { gtk_widget_set_size_request(bs[i], 112, -1); gtk_box_append(GTK_BOX(btns), bs[i]); }
  gtk_box_append(GTK_BOX(t2), btns);
  gtk_box_append(GTK_BOX(t), t1); gtk_box_append(GTK_BOX(t), t2);

  gtk_box_append(GTK_BOX(h), q); gtk_box_append(GTK_BOX(h), t);
  return h;
}

/* ---- status bar: health only, the period progress moved to the period line --- */
static GtkWidget *status_item(const char *text, const char *cls) {
  GtkWidget *l = gtk_label_new(text);
  gtk_widget_add_css_class(l, "status");
  if (cls) gtk_widget_add_css_class(l, cls);
  gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
  return l;
}
static GtkWidget *status_bar(void) {
  GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_add_css_class(h, "statusbar");
  int secs = MOCK_UTC_SECS;
  char buf[48]; snprintf(buf, sizeof buf, "%02d:%02d:%02d UTC", secs / 3600, (secs / 60) % 60, secs % 60);
  GtkWidget *clock = status_item(buf, "clock");
  gtk_box_append(GTK_BOX(h), clock);
  gtk_box_append(GTK_BOX(h), status_item("● clock  mean DT −0.07 s", "ok"));
  gtk_box_append(GTK_BOX(h), status_item(idle_state ? "● RX  FT8 · 12 kHz TCI audio" : "● TX  1648 Hz · FT8", idle_state ? "ok" : "tx"));
  gtk_box_append(GTK_BOX(h), status_item("21 decodes · 1.9 s · peak 5 % FS", NULL));
  GtkWidget *sp = gtk_label_new(""); gtk_widget_set_hexpand(sp, TRUE); gtk_box_append(GTK_BOX(h), sp);
  gtk_box_append(GTK_BOX(h), status_item("● TCI sdr-for-linux 127.0.0.1  DIGU", "ok"));
  gtk_box_append(GTK_BOX(h), status_item("● log-for-linux UDP 2237", "ok"));
  return h;
}

/* ---- window -------------------------------------------------------------------- */
static void css_load(void) {
  GtkCssProvider *p = gtk_css_provider_new();
  const char *c =
    ".mono { font-family: \"Adwaita Mono\", monospace; }"
    ".dim { opacity: 0.6; }"
    ".cap { font-size: 11px; opacity: 0.6; }"
    ".dial { font-family: \"Adwaita Mono\", monospace; font-size: 22px; font-weight: 600; letter-spacing: 1px; }"
    ".band { opacity: 0.7; font-size: 13px; }"
    ".panehdr { padding: 8px 10px 4px 10px; }"
    ".colhdr { padding: 2px 8px; font-size: 12px; opacity: 0.7; background: rgba(255,255,255,0.05); }"
    ".decodes { background: transparent; }"
    ".decodes row { padding: 0; min-height: 0; }"
    ".decode { padding: 2px 8px; font-size: 13px; }"
    ".decode.cq { background: rgba(60,180,80,0.16); }"
    ".decode.tome { background: rgba(255,140,30,0.22); }"
    ".decode.b4 { opacity: 0.55; }"
    ".decode.tx { color: #ff9a8a; }"
    ".txlist row { padding: 0; min-height: 0; }"
    ".txrow { padding: 1px 10px; }"
    ".tag { font-size: 10px; padding: 0 5px; border-radius: 4px; background: rgba(255,255,255,0.12); }"
    ".tag-me { background: rgba(255,140,30,0.5); }"
    ".tag-now { background: #c8321e; color: #fff; }"
    ".console { padding: 10px 12px; }"
    ".group { padding: 8px 12px; border-radius: 8px; border: 1px solid rgba(255,255,255,0.16); background: rgba(255,255,255,0.03); }"
    ".group.onair { border-color: rgba(200,50,30,0.9); background: rgba(200,50,30,0.14); }"
    ".dxcall { font-family: \"Adwaita Mono\", monospace; font-size: 24px; font-weight: 700; }"
    ".rcvd { font-family: \"Adwaita Mono\", monospace; font-size: 16px; font-weight: 700; }"
    ".badge { font-size: 11px; font-weight: 700; padding: 4px 9px; border-radius: 4px; letter-spacing: 1px; }"
    ".badge.rx { background: rgba(255,255,255,0.14); }"
    ".badge.tx { background: #c8321e; color: #fff; }"
    ".nextmsg { font-family: \"Adwaita Mono\", monospace; font-size: 19px; font-weight: 700; }"
    "button.txkey { font-weight: 700; }"
    "button.txkey:checked { background: #c8321e; color: #fff; }"
    ".statusbar { padding: 5px 12px; font-size: 12px; border-top: 1px solid rgba(255,255,255,0.08); }"
    ".status.clock { font-family: \"Adwaita Mono\", monospace; font-size: 13px; font-weight: 700; }"
    ".status.ok { color: #6fe08a; }"
    ".status.tx { color: #ff9a8a; }"
    "button.mode:checked { background: #1d6fa5; color: #fff; }";
  gtk_css_provider_load_from_string(p, c);
  gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

/* Under Broadway (the README screenshots) the window is maximized into the
 * browser canvas, which is set to the size the live window has on Richard's
 * desk (1519 x 1364, tiled by Hyprland next to sdr-for-linux). */
static void pin_size_under_broadway(GtkWindow *win, int w, int h) {
  (void)w; (void)h;
  if (g_strcmp0(g_getenv("GDK_BACKEND"), "broadway") == 0) gtk_window_maximize(win);
}

static void activate(GtkApplication *app, gpointer u) {
  (void)u;
  idle_state = g_strcmp0(g_getenv("MOCK_STATE"), "idle") == 0;
  adw_style_manager_set_color_scheme(adw_style_manager_get_default(), ADW_COLOR_SCHEME_FORCE_DARK);
  css_load();
  GtkWidget *win = adw_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(win), "FTX for Linux");
  gtk_window_set_default_size(GTK_WINDOW(win), 1519, 1364);

  GtkWidget *hdr = adw_header_bar_new();
  GtkWidget *title = adw_window_title_new("FTX for Linux", "OK1BR · JO70");
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(hdr), title);
  GtkWidget *dialbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *dial = gtk_label_new("14.074.000"); gtk_widget_add_css_class(dial, "dial");
  GtkWidget *band = gtk_label_new("20 m · DIGU"); gtk_widget_add_css_class(band, "band");
  gtk_box_append(GTK_BOX(dialbox), dial); gtk_box_append(GTK_BOX(dialbox), band);
  adw_header_bar_pack_start(ADW_HEADER_BAR(hdr), dialbox);
  GtkWidget *modes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0); gtk_widget_add_css_class(modes, "linked");
  GtkWidget *ft8 = gtk_toggle_button_new_with_label("FT8"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ft8), TRUE); gtk_widget_add_css_class(ft8, "mode");
  GtkWidget *ft4 = gtk_toggle_button_new_with_label("FT4"); gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(ft4), GTK_TOGGLE_BUTTON(ft8)); gtk_widget_add_css_class(ft4, "mode");
  gtk_box_append(GTK_BOX(modes), ft8); gtk_box_append(GTK_BOX(modes), ft4);
  GtkWidget *menu = gtk_menu_button_new(); gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu), "open-menu-symbolic");
  adw_header_bar_pack_end(ADW_HEADER_BAR(hdr), menu);
  adw_header_bar_pack_end(ADW_HEADER_BAR(hdr), modes);
  GtkWidget *mon = gtk_toggle_button_new_with_label("Monitor"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mon), TRUE); gtk_widget_add_css_class(mon, "mode");
  adw_header_bar_pack_end(ADW_HEADER_BAR(hdr), mon);

  GtkWidget *area = gtk_drawing_area_new();
  gtk_widget_set_size_request(area, -1, 200);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_waterfall, NULL, NULL);
  for (int i = 0; i < WF_ROWS; i++) wf_push_row(i / (double)ROWS_PER_S);
  g_timeout_add(1000 / ROWS_PER_S, tick, area);

  GtkWidget *line = gtk_drawing_area_new();
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(line), 8);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(line), draw_period_line, NULL, NULL);

  GtkWidget *lists = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_paned_set_start_child(GTK_PANED(lists), decode_pane("Band activity", "21 decodes · 133430", 0));
  gtk_paned_set_end_child(GTK_PANED(lists), decode_pane("Rx frequency", "Rx 1648 Hz", 1));
  gtk_paned_set_position(GTK_PANED(lists), 820);
  gtk_widget_set_vexpand(lists, TRUE);

  GtkWidget *lower = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append(GTK_BOX(lower), line);
  gtk_box_append(GTK_BOX(lower), console());
  gtk_box_append(GTK_BOX(lower), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
  gtk_box_append(GTK_BOX(lower), lists);

  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
  gtk_paned_set_start_child(GTK_PANED(paned), area);
  gtk_paned_set_end_child(GTK_PANED(paned), lower);
  gtk_paned_set_position(GTK_PANED(paned), 440);
  gtk_paned_set_resize_start_child(GTK_PANED(paned), FALSE);

  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append(GTK_BOX(v), hdr);
  gtk_box_append(GTK_BOX(v), paned);
  gtk_widget_set_vexpand(paned, TRUE);
  gtk_box_append(GTK_BOX(v), status_bar());
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(win), v);
  pin_size_under_broadway(GTK_WINDOW(win), 1519, 1364);
  gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char **argv) {
  AdwApplication *app = adw_application_new("cz.ok1br.ftx_for_linux.mockup2", G_APPLICATION_NON_UNIQUE);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  int st = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return st;
}
