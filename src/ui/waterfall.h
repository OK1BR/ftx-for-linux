/* waterfall.h — the labelled waterfall: scrolling rows with
 * per-row UTC time (period boundaries carry their HHMMSS tag), the callsign
 * of every decode drawn on its trace, the Rx (green) and Tx (red) brackets.
 * Left click = Rx frequency, right click (or shift) = Tx frequency, as in
 * WSJT-X's wide graph.
 *
 * Part of FTX for Linux.  Copyright (C) 2026 Richard Fakenberg, OK1BR.
 * GPL-3.0-or-later.  No warranty.
 */
#ifndef FTX_WATERFALL_H
#define FTX_WATERFALL_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define FTX_TYPE_WATERFALL (ftx_waterfall_get_type())
G_DECLARE_FINAL_TYPE(FtxWaterfall, ftx_waterfall, FTX, WATERFALL, GtkWidget)

typedef enum { FTX_WF_LABEL_PLAIN, FTX_WF_LABEL_CQ, FTX_WF_LABEL_TO_ME } FtxWfLabelKind;

GtkWidget *ftx_waterfall_new(void);

/* One spectrum row: nbins dB values, bin_hz apart from 0 Hz, taken at t_utc_us. */
void ftx_waterfall_push_row(FtxWaterfall *w, const float *db, guint nbins, double bin_hz, gint64 t_utc_us);
/* A decode of period nutc at hz: text is drawn on the signal's trace. */
void ftx_waterfall_add_label(FtxWaterfall *w, int nutc, double hz, const char *text, FtxWfLabelKind kind);
void ftx_waterfall_clear(FtxWaterfall *w);

void   ftx_waterfall_set_rx_hz(FtxWaterfall *w, double hz);
double ftx_waterfall_get_rx_hz(FtxWaterfall *w);
void   ftx_waterfall_set_tx_hz(FtxWaterfall *w, double hz);
void   ftx_waterfall_set_period(FtxWaterfall *w, double seconds);      /* 15 or 7.5     */
void   ftx_waterfall_set_bandwidth(FtxWaterfall *w, double hz);        /* 50 FT8, 83.3 FT4 */
void   ftx_waterfall_set_span(FtxWaterfall *w, double fmin, double fmax);
void   ftx_waterfall_set_palette(FtxWaterfall *w, int palette);        /* index into the family palette table */
/* The palettes of sdr-for-linux / skimmer-for-linux: Classic (0), Mono white, Mono green, Mono amber, Inferno, Turbo. */
int         ftx_waterfall_palette_count(void);
const char *ftx_waterfall_palette_name(int idx);
void   ftx_waterfall_set_show_labels(FtxWaterfall *w, gboolean show);
/* Opacity of the Rx/Tx passband overlays, 0-100 % (the same slider as sdr-for-linux's "Filter opacity"). */
void   ftx_waterfall_set_filter_opacity(FtxWaterfall *w, int pct);

/* Signals: "rx-clicked" (gdouble hz), "tx-clicked" (gdouble hz). */

G_END_DECLS

#endif /* FTX_WATERFALL_H */
