/* status_bar.h — the bottom line: UTC clock, clock health (mean DT of the
 * last decodes), the audio / TX state, decode count and time of the last
 * period, and the two links this program lives on (TCI, log-for-linux).
 * The period progress is not here: it is the period line under the waterfall.
 *
 * Part of FTX for Linux.  Copyright (C) 2026 Richard Fakenberg, OK1BR.
 * GPL-3.0-or-later.  No warranty.
 */
#ifndef FTX_STATUS_BAR_H
#define FTX_STATUS_BAR_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define FTX_TYPE_STATUS_BAR (ftx_status_bar_get_type())
G_DECLARE_FINAL_TYPE(FtxStatusBar, ftx_status_bar, FTX, STATUS_BAR, GtkWidget)

typedef enum { FTX_STATUS_PLAIN, FTX_STATUS_OK, FTX_STATUS_TX, FTX_STATUS_ERROR, FTX_STATUS_DIM } FtxStatusState;

GtkWidget *ftx_status_bar_new(void);
void ftx_status_bar_set_clock(FtxStatusBar *sb, int hh, int mm, int ss);
void ftx_status_bar_set_clock_health(FtxStatusBar *sb, gboolean show, gboolean have, double mean_dt, double limit);
void ftx_status_bar_set_audio(FtxStatusBar *sb, const char *text, FtxStatusState st);
void ftx_status_bar_set_decodes(FtxStatusBar *sb, const char *text);
void ftx_status_bar_set_tci(FtxStatusBar *sb, const char *text, FtxStatusState st);
void ftx_status_bar_set_logger(FtxStatusBar *sb, const char *text, FtxStatusState st);

G_END_DECLS

#endif /* FTX_STATUS_BAR_H */
