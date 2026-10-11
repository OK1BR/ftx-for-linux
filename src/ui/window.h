/* window.h — the main window: header bar (dial, mode, Monitor, FT8/FT4),
 * waterfall over the two decode panes and the QSO panel, status bar.  Owns
 * the RX engine and the QSO state (the auto-sequencer).
 *
 * Part of FTX for Linux.  Copyright (C) 2026 Richard Fakenberg, OK1BR.
 * GPL-3.0-or-later.  No warranty.
 */
#ifndef FTX_WINDOW_H
#define FTX_WINDOW_H

#include <adwaita.h>

#include "app/settings.h"

G_BEGIN_DECLS

#define FTX_TYPE_WINDOW (ftx_window_get_type())
G_DECLARE_FINAL_TYPE(FtxWindow, ftx_window, FTX, WINDOW, AdwApplicationWindow)

GtkWidget *ftx_window_new(AdwApplication *app, FtxSettings *settings);
/* Runtime facts for the About dialog's debug information. */
char *ftx_window_debug_info(FtxWindow *win);

G_END_DECLS

#endif /* FTX_WINDOW_H */
