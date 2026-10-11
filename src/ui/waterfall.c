/* waterfall.c — see waterfall.h.  No spectrum strip: Richard 2026-10-11,
 * "spektrum dej pryč úplně, nech tam jen vodopád".  The Rx/Tx numbers are not
 * drawn here either (v2 layout): flags in the axis, numbers in the console.  Copyright (C) 2026 Richard Fakenberg, OK1BR.
 * GPL-3.0-or-later. */
#include "waterfall.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ROWS 1000
#define AXIS_H   22
#define FONT     "Adwaita Mono"

struct label {
  gint64 seq;              /* row counter when added (for pruning)  */
  int    nutc;
  double hz;
  char   text[24];
  int    kind;
};

struct _FtxWaterfall {
  GtkWidget parent_instance;
  guint   nbins;
  double  bin_hz;
  float  *rows;            /* MAX_ROWS x nbins, ring                */
  gint64 *row_t;           /* UTC us per row                        */
  float  *row_base;        /* noise floor per row (dB)              */
  int     head, count;
  gint64  seq;
  float   base_ema;
  gboolean base_init;
  GArray *labels;
  double  rx_hz, tx_hz, fmin, fmax, period_s, bw_hz, range_db;
  int     palette;
  gboolean show_labels;
  guint8 *pix;
  gsize   pix_cap;
};

G_DEFINE_FINAL_TYPE(FtxWaterfall, ftx_waterfall, GTK_TYPE_WIDGET)

enum { SIG_RX_CLICKED, SIG_TX_CLICKED, N_SIGNALS };
static guint signals[N_SIGNALS];

/* ---- colours ------------------------------------------------------------ */

/* The family's palettes — the table of sdr-for-linux's waterfall.c (also in
 * skimmer-for-linux): {position 0-1, r, g, b} stops, linearly interpolated;
 * "Classic" is index 0, the default.  Same names in every program, so the
 * operator's choice carries over. */
typedef struct { double t, r, g, b; } PalStop;
typedef struct { const char *name; const PalStop *stop; int n; } Palette;

/* Classic: black-blue -> cyan -> green -> yellow -> orange -> red. */
static const PalStop pal_classic[] = {
  {0.00,   0,   0,  12}, {0.20,   0,  18, 110}, {0.40,   0, 130, 180},
  {0.58,   0, 200, 120}, {0.74, 180, 220,   0}, {0.88, 255, 165,   0},
  {1.00, 255,  40,  30},
};
/* Mono white: neutral greyscale, black -> white.  High contrast: the noise
 * floor is held near black, signals ramp hard to pure white. */
static const PalStop pal_mono_white[] = {
  {0.00, 0, 0, 0}, {0.30, 6, 6, 6}, {0.58, 90, 90, 90}, {0.80, 210, 210, 210}, {1.00, 255, 255, 255},
};
/* Mono green: retro phosphor — near-black floor, bright green signals. */
static const PalStop pal_mono_green[] = {
  {0.00, 0, 2, 0}, {0.30, 0, 34, 6}, {0.60, 0, 150, 40}, {0.82, 130, 240, 95}, {1.00, 225, 255, 205},
};
/* Mono amber: warm monochrome — near-black floor, bright amber signals. */
static const PalStop pal_mono_amber[] = {
  {0.00, 2, 1, 0}, {0.30, 40, 18, 0}, {0.60, 165, 90, 4}, {0.82, 248, 190, 55}, {1.00, 255, 245, 215},
};
/* Inferno: perceptually-uniform black -> purple -> red -> orange -> yellow. */
static const PalStop pal_inferno[] = {
  {0.00, 0, 0, 4}, {0.15, 22, 11, 57}, {0.30, 66, 10, 104}, {0.45, 114, 31, 107},
  {0.60, 168, 46, 86}, {0.74, 216, 83, 45}, {0.87, 245, 140, 20}, {1.00, 252, 255, 164},
};
/* Turbo: high-detail improved rainbow. */
static const PalStop pal_turbo[] = {
  {0.00, 48, 18, 59}, {0.13, 62, 74, 211}, {0.25, 40, 150, 240}, {0.38, 43, 210, 180},
  {0.50, 130, 235, 80}, {0.62, 220, 220, 40}, {0.75, 250, 150, 30}, {0.88, 230, 60, 20},
  {1.00, 122, 4, 3},
};

static const Palette palettes[] = {
  { "Classic",    pal_classic,    (int)G_N_ELEMENTS(pal_classic)    },
  { "Mono white", pal_mono_white, (int)G_N_ELEMENTS(pal_mono_white) },
  { "Mono green", pal_mono_green, (int)G_N_ELEMENTS(pal_mono_green) },
  { "Mono amber", pal_mono_amber, (int)G_N_ELEMENTS(pal_mono_amber) },
  { "Inferno",    pal_inferno,    (int)G_N_ELEMENTS(pal_inferno)    },
  { "Turbo",      pal_turbo,      (int)G_N_ELEMENTS(pal_turbo)      },
};

int ftx_waterfall_palette_count(void) { return (int)G_N_ELEMENTS(palettes); }
const char *ftx_waterfall_palette_name(int idx)
{
  return (idx < 0 || idx >= (int)G_N_ELEMENTS(palettes)) ? "" : palettes[idx].name;
}

static void palette_rgb(int pal, double v, guint8 *r, guint8 *g, guint8 *b)
{
  if (pal < 0 || pal >= (int)G_N_ELEMENTS(palettes)) pal = 0;
  const PalStop *stop = palettes[pal].stop;
  const int n = palettes[pal].n;
  v = v < 0 ? 0 : v > 1 ? 1 : v;
  int s = 0;
  while (s < n - 2 && v > stop[s + 1].t) s++;
  double t0 = stop[s].t, t1 = stop[s + 1].t;
  double f = t1 > t0 ? (v - t0) / (t1 - t0) : 0.0;
  f = f < 0 ? 0 : f > 1 ? 1 : f;
  *r = (guint8)(stop[s].r + f * (stop[s + 1].r - stop[s].r) + 0.5);
  *g = (guint8)(stop[s].g + f * (stop[s + 1].g - stop[s].g) + 0.5);
  *b = (guint8)(stop[s].b + f * (stop[s + 1].b - stop[s].b) + 0.5);
}

static double x_of(FtxWaterfall *w, double hz, int W)
{
  return (hz - w->fmin) / (w->fmax - w->fmin) * W;
}

static int cmp_float(const void *a, const void *b)
{
  float x = *(const float *)a, y = *(const float *)b;
  return (x > y) - (x < y);
}

/* ---- data ---------------------------------------------------------------- */

void ftx_waterfall_push_row(FtxWaterfall *w, const float *db, guint nbins, double bin_hz, gint64 t_utc_us)
{
  if (nbins == 0) return;
  if (nbins != w->nbins) {
    w->nbins = nbins;
    w->rows = g_realloc(w->rows, (gsize)MAX_ROWS * nbins * sizeof(float));
    w->head = w->count = 0;
    w->base_init = FALSE;
  }
  w->bin_hz = bin_hz;
  float *row = w->rows + (gsize)w->head * nbins;
  memcpy(row, db, nbins * sizeof(float));
  /* noise floor: the 30th percentile of the displayed span, smoothed */
  int b0 = (int)floor(w->fmin / bin_hz), b1 = (int)ceil(w->fmax / bin_hz);
  b0 = CLAMP(b0, 0, (int)nbins - 1);
  b1 = CLAMP(b1, b0 + 1, (int)nbins - 1);
  int n = b1 - b0 + 1;
  float *tmp = g_newa(float, n);
  memcpy(tmp, row + b0, (gsize)n * sizeof(float));
  qsort(tmp, (size_t)n, sizeof(float), cmp_float);
  float base = tmp[n * 3 / 10];
  w->base_ema = w->base_init ? 0.85f * w->base_ema + 0.15f * base : base;
  w->base_init = TRUE;
  w->row_base[w->head] = w->base_ema;
  w->row_t[w->head] = t_utc_us;
  w->head = (w->head + 1) % MAX_ROWS;
  if (w->count < MAX_ROWS) w->count++;
  w->seq++;
  /* labels of rows that have scrolled out */
  for (guint i = 0; i < w->labels->len;) {
    struct label *l = &g_array_index(w->labels, struct label, i);
    if (w->seq - l->seq > MAX_ROWS) g_array_remove_index_fast(w->labels, i); else i++;
  }
  gtk_widget_queue_draw(GTK_WIDGET(w));
}

void ftx_waterfall_add_label(FtxWaterfall *w, int nutc, double hz, const char *text, FtxWfLabelKind kind)
{
  struct label l;
  l.seq = w->seq;
  l.nutc = nutc;
  l.hz = hz;
  g_strlcpy(l.text, text, sizeof l.text);
  l.kind = kind;
  g_array_append_val(w->labels, l);
  gtk_widget_queue_draw(GTK_WIDGET(w));
}

void ftx_waterfall_clear(FtxWaterfall *w)
{
  w->head = w->count = 0;
  w->base_init = FALSE;
  g_array_set_size(w->labels, 0);
  gtk_widget_queue_draw(GTK_WIDGET(w));
}

#define SETTER(name, field, type) \
  void ftx_waterfall_set_##name(FtxWaterfall *w, type v) { w->field = v; gtk_widget_queue_draw(GTK_WIDGET(w)); }
SETTER(rx_hz, rx_hz, double)
SETTER(tx_hz, tx_hz, double)
SETTER(period, period_s, double)
SETTER(bandwidth, bw_hz, double)
SETTER(palette, palette, int)
SETTER(show_labels, show_labels, gboolean)
#undef SETTER
double ftx_waterfall_get_rx_hz(FtxWaterfall *w) { return w->rx_hz; }
void ftx_waterfall_set_span(FtxWaterfall *w, double fmin, double fmax)
{
  if (fmax - fmin < 100) return;
  w->fmin = fmin; w->fmax = fmax;
  gtk_widget_queue_draw(GTK_WIDGET(w));
}

/* ---- drawing ------------------------------------------------------------- */

static int row_index(FtxWaterfall *w, int r)   /* r = 0 newest */
{
  return (w->head - 1 - r + MAX_ROWS) % MAX_ROWS;
}

static void hhmmss_of(gint64 period_idx, gint64 period_us, char out[8])
{
  gint64 secs = (period_idx * period_us / 1000000) % 86400;
  if (secs < 0) secs += 86400;
  snprintf(out, 8, "%02d%02d%02d", (int)(secs / 3600), (int)((secs / 60) % 60), (int)(secs % 60));
}

struct placed { int pi; double x, half; int anchor; const struct label *l; };
static int cmp_placed(const void *a, const void *b)
{
  const struct placed *p = a, *q = b;
  if (p->pi != q->pi) return p->pi - q->pi;
  return (p->x > q->x) - (p->x < q->x);
}

static void ftx_waterfall_snapshot(GtkWidget *widget, GtkSnapshot *snap)
{
  FtxWaterfall *w = FTX_WATERFALL(widget);
  const int W = gtk_widget_get_width(widget), H = gtk_widget_get_height(widget);
  if (W <= 0 || H <= 0) return;
  guint8 fr, fg, fb;
  palette_rgb(w->palette, 0.0, &fr, &fg, &fb);
  const GdkRGBA bg = { fr / 255.0f, fg / 255.0f, fb / 255.0f, 1.0f };
  gtk_snapshot_append_color(snap, &bg, &GRAPHENE_RECT_INIT(0, 0, W, H));

  const int wy0 = AXIS_H + 2, wh = H - wy0;
  if (wh <= 0) return;

  int b0 = 0, nb = 0;
  if (w->nbins && w->bin_hz > 0) {
    b0 = CLAMP((int)floor(w->fmin / w->bin_hz), 0, (int)w->nbins - 1);
    int b1 = CLAMP((int)ceil(w->fmax / w->bin_hz), b0, (int)w->nbins - 1);
    nb = b1 - b0 + 1;
  }
  const int rows_vis = MIN(w->count, wh);

  /* the rows as one texture, scaled to the widget */
  if (nb > 0 && rows_vis > 0) {
    gsize need = (gsize)nb * rows_vis * 4;
    if (w->pix_cap < need) { w->pix = g_realloc(w->pix, need); w->pix_cap = need; }
    for (int r = 0; r < rows_vis; r++) {
      int idx = row_index(w, r);
      const float *row = w->rows + (gsize)idx * w->nbins;
      const float base = w->row_base[idx];
      guint8 *p = w->pix + (gsize)r * nb * 4;
      for (int k = 0; k < nb; k++, p += 4) {
        palette_rgb(w->palette, (row[b0 + k] - base) / w->range_db, &p[0], &p[1], &p[2]);
        p[3] = 255;
      }
    }
    GBytes *bytes = g_bytes_new(w->pix, need);
    GdkTexture *tex = gdk_memory_texture_new(nb, rows_vis, GDK_MEMORY_R8G8B8A8, bytes, (gsize)nb * 4);
    g_bytes_unref(bytes);
    double xl = x_of(w, (b0 - 0.5) * w->bin_hz, W), xr = x_of(w, (b0 + nb - 0.5) * w->bin_hz, W);
    gtk_snapshot_push_clip(snap, &GRAPHENE_RECT_INIT(0, wy0, W, wh));
    gtk_snapshot_append_scaled_texture(snap, tex, GSK_SCALING_FILTER_LINEAR,
                                       &GRAPHENE_RECT_INIT((float)xl, wy0, (float)(xr - xl), rows_vis));
    gtk_snapshot_pop(snap);
    g_object_unref(tex);
  }

  cairo_t *cr = gtk_snapshot_append_cairo(snap, &GRAPHENE_RECT_INIT(0, 0, W, H));
  cairo_select_font_face(cr, FONT, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

  /* frequency axis */
  cairo_set_font_size(cr, 11);
  int step = (w->fmax - w->fmin) > 2500 ? 100 : 50, major_every = step == 100 ? 500 : 250;
  for (int hz = (int)ceil(w->fmin / step) * step; hz <= w->fmax; hz += step) {
    double x = x_of(w, hz, W);
    gboolean major = hz % major_every == 0;
    cairo_set_source_rgba(cr, 1, 1, 1, major ? 0.55 : 0.22);
    cairo_rectangle(cr, x, AXIS_H - (major ? 8 : 4), 1, major ? 8 : 4);
    cairo_fill(cr);
    if (major) {
      char t[16];
      snprintf(t, sizeof t, "%d", hz);
      cairo_text_extents_t te;
      cairo_text_extents(cr, t, &te);
      cairo_move_to(cr, x - te.width / 2, 11);
      cairo_show_text(cr, t);
    }
  }

  /* period boundaries with HHMMSS tags, and the visible periods' row ranges */
  const gint64 period_us = (gint64)(w->period_s * 1e6);
  struct prange { gint64 idx; int first, last; } pr[80];
  int npr = 0;
  cairo_set_font_size(cr, 10);
  if (rows_vis > 0 && period_us > 0) {
    gint64 cur = w->row_t[row_index(w, 0)] / period_us;
    pr[npr].idx = cur; pr[npr].first = 0; pr[npr].last = 0; npr++;
    for (int r = 1; r < rows_vis; r++) {
      gint64 p = w->row_t[row_index(w, r)] / period_us;
      if (p == cur) { pr[npr - 1].last = r; continue; }
      /* row r-1 starts period cur: a line below it, the tag above the line */
      double y = wy0 + r;
      cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
      cairo_rectangle(cr, 0, y, W, 1);
      cairo_fill(cr);
      if (pr[npr - 1].last - pr[npr - 1].first >= 14) {
        char t[8];
        hhmmss_of(cur, period_us, t);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.55);
        cairo_rectangle(cr, W - 52, y - 15, 50, 13);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.85);
        cairo_move_to(cr, W - 49, y - 5);
        cairo_show_text(cr, t);
      }
      cur = p;
      if (npr < 80) { pr[npr].idx = cur; pr[npr].first = r; pr[npr].last = r; npr++; }
    }
  }

  /* decode labels on their traces, in up to three lanes: a label that would
   * overlap its left neighbour drops a lane (sorted by frequency per period) */
  if (w->show_labels && w->labels->len > 0 && npr > 0) {
    cairo_set_font_size(cr, 10.5);
    struct placed *pl = g_newa(struct placed, w->labels->len);
    guint np = 0;
    for (guint i = 0; i < w->labels->len; i++) {
      const struct label *l = &g_array_index(w->labels, struct label, i);
      int pi = -1;
      for (int k = 0; k < npr; k++) {
        char t[8];
        hhmmss_of(pr[k].idx, period_us, t);
        if (atoi(t) == l->nutc) { pi = k; break; }
      }
      if (pi < 0) continue;
      /* anchored 10 s after the period start (FT4: 4 s) so a label does not
       * move while the period is still filling — decodes arrive before its end */
      gint64 target = pr[pi].idx * period_us + (gint64)(period_us * (w->period_s > 10 ? 10.0 / w->period_s : 0.55));
      int r_anchor = pr[pi].first;
      for (int r = pr[pi].last; r >= pr[pi].first; r--)
        if (w->row_t[row_index(w, r)] >= target) { r_anchor = r; break; }
      cairo_text_extents_t te;
      cairo_text_extents(cr, l->text, &te);
      pl[np].pi = pi;
      pl[np].x = x_of(w, l->hz + w->bw_hz / 2, W);
      pl[np].half = te.width / 2;
      pl[np].anchor = r_anchor;
      pl[np].l = l;
      np++;
    }
    qsort(pl, np, sizeof *pl, cmp_placed);
    double lane_right[80][3];
    for (int k = 0; k < 80; k++) lane_right[k][0] = lane_right[k][1] = lane_right[k][2] = -1e9;
    for (guint i = 0; i < np; i++) {
      const double left = pl[i].x - pl[i].half - 3, right = pl[i].x + pl[i].half + 3;
      int lane = -1;
      for (int k = 0; k < 3; k++) if (lane_right[pl[i].pi][k] + 4 <= left) { lane = k; break; }
      if (lane < 0) {
        lane = 0;
        for (int k = 1; k < 3; k++) if (lane_right[pl[i].pi][k] < lane_right[pl[i].pi][lane]) lane = k;
      }
      lane_right[pl[i].pi][lane] = right;
      double ly = wy0 + pl[i].anchor + 4 + lane * 13;
      cairo_set_source_rgba(cr, 0, 0, 0, 0.6);
      cairo_rectangle(cr, left, ly - 10, right - left, 13);
      cairo_fill(cr);
      if (pl[i].l->kind == FTX_WF_LABEL_TO_ME) cairo_set_source_rgb(cr, 1.0, 0.62, 0.2);
      else if (pl[i].l->kind == FTX_WF_LABEL_CQ) cairo_set_source_rgb(cr, 0.55, 0.95, 0.55);
      else cairo_set_source_rgba(cr, 1, 1, 1, 0.8);
      cairo_move_to(cr, pl[i].x - pl[i].half, ly);
      cairo_show_text(cr, pl[i].l->text);
    }
  }

  /* Rx / Tx brackets, one signal bandwidth wide */
  double xr = x_of(w, w->rx_hz, W), xw = x_of(w, w->rx_hz + w->bw_hz, W) - xr;
  cairo_set_line_width(cr, 2);
  cairo_set_source_rgba(cr, 0.3, 1.0, 0.4, 0.95);
  cairo_move_to(cr, xr, AXIS_H + 1); cairo_line_to(cr, xr, H); cairo_stroke(cr);
  cairo_set_source_rgba(cr, 0.3, 1.0, 0.4, 0.18);
  cairo_rectangle(cr, xr, AXIS_H + 1, xw, H); cairo_fill(cr);
  double xt = x_of(w, w->tx_hz, W);
  cairo_set_source_rgba(cr, 1.0, 0.3, 0.25, 0.95);
  cairo_move_to(cr, xt + xw, AXIS_H + 1); cairo_line_to(cr, xt + xw, H); cairo_stroke(cr);
  cairo_set_source_rgba(cr, 1.0, 0.3, 0.25, 0.10);
  cairo_rectangle(cr, xt, AXIS_H + 1, xw, H); cairo_fill(cr);
  /* Rx / Tx flags in the axis strip (the numbers live in the console and
   * the Rx-frequency pane header) */
  cairo_set_source_rgba(cr, 0, 0, 0, 0.7);
  cairo_move_to(cr, xr - 8, 0); cairo_line_to(cr, xr + 8, 0); cairo_line_to(cr, xr, 14); cairo_close_path(cr); cairo_fill(cr);
  cairo_set_source_rgba(cr, 0.3, 1.0, 0.4, 1);
  cairo_move_to(cr, xr - 6, 1); cairo_line_to(cr, xr + 6, 1); cairo_line_to(cr, xr, 11); cairo_close_path(cr); cairo_fill(cr);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.7);
  cairo_move_to(cr, xt + xw - 8, 0); cairo_line_to(cr, xt + xw + 8, 0); cairo_line_to(cr, xt + xw, 14); cairo_close_path(cr); cairo_fill(cr);
  cairo_set_source_rgba(cr, 1.0, 0.3, 0.25, 1);
  cairo_move_to(cr, xt + xw - 6, 1); cairo_line_to(cr, xt + xw + 6, 1); cairo_line_to(cr, xt + xw, 11); cairo_close_path(cr); cairo_fill(cr);
  cairo_destroy(cr);
}

static void ftx_waterfall_measure(GtkWidget *widget, GtkOrientation o, int for_size, int *min, int *nat, int *mb, int *nb)
{
  (void)widget; (void)for_size; (void)mb; (void)nb;
  if (o == GTK_ORIENTATION_VERTICAL) { *min = 120; *nat = 300; }
  else { *min = 400; *nat = 1200; }
}

static void on_pressed(GtkGestureClick *g, int n_press, double x, double y, gpointer user)
{
  (void)n_press; (void)y;
  FtxWaterfall *w = user;
  int W = gtk_widget_get_width(GTK_WIDGET(w));
  if (W <= 0) return;
  double hz = w->fmin + x / W * (w->fmax - w->fmin);
  hz = CLAMP(hz, w->fmin, w->fmax);
  guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(g));
  GdkModifierType mods = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(g));
  gboolean shift = (mods & GDK_SHIFT_MASK) != 0, ctrl = (mods & GDK_CONTROL_MASK) != 0;
  if (button == 3 || shift) g_signal_emit(w, signals[SIG_TX_CLICKED], 0, hz);
  else g_signal_emit(w, signals[SIG_RX_CLICKED], 0, hz);
  if (ctrl && button == 1) g_signal_emit(w, signals[SIG_TX_CLICKED], 0, hz);
}

static void ftx_waterfall_finalize(GObject *obj)
{
  FtxWaterfall *w = FTX_WATERFALL(obj);
  g_free(w->rows); g_free(w->row_t); g_free(w->row_base); g_free(w->pix);
  g_array_free(w->labels, TRUE);
  G_OBJECT_CLASS(ftx_waterfall_parent_class)->finalize(obj);
}

static void ftx_waterfall_class_init(FtxWaterfallClass *klass)
{
  GtkWidgetClass *wc = GTK_WIDGET_CLASS(klass);
  wc->snapshot = ftx_waterfall_snapshot;
  wc->measure = ftx_waterfall_measure;
  G_OBJECT_CLASS(klass)->finalize = ftx_waterfall_finalize;
  signals[SIG_RX_CLICKED] = g_signal_new("rx-clicked", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_DOUBLE);
  signals[SIG_TX_CLICKED] = g_signal_new("tx-clicked", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_DOUBLE);
}

static void ftx_waterfall_init(FtxWaterfall *w)
{
  w->row_t = g_new0(gint64, MAX_ROWS);
  w->row_base = g_new0(float, MAX_ROWS);
  w->labels = g_array_new(FALSE, FALSE, sizeof(struct label));
  w->rx_hz = 1500; w->tx_hz = 1500; w->fmin = 0; w->fmax = 4000; w->period_s = 15; w->bw_hz = 50; w->range_db = 40;
  w->show_labels = TRUE;
  GtkGesture *click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
  g_signal_connect(click, "pressed", G_CALLBACK(on_pressed), w);
  gtk_widget_add_controller(GTK_WIDGET(w), GTK_EVENT_CONTROLLER(click));
  gtk_widget_set_cursor_from_name(GTK_WIDGET(w), "crosshair");
  gtk_widget_set_tooltip_text(GTK_WIDGET(w), "Click: Rx frequency · right click or Shift: Tx frequency · Ctrl: both");
}

GtkWidget *ftx_waterfall_new(void) { return g_object_new(FTX_TYPE_WATERFALL, NULL); }
