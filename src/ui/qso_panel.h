/* qso_panel.h — the QSO panel: the partner (call, grid, report to send,
 * report received, what we know about him), the transmission card (what is
 * on the air or goes out next, in which period, how long), the TX controls,
 * the six standard messages with the auto-sequencer switch, Tx frequency and
 * Log QSO.  The card and the "on air" tag are the TX indication that no
 * theme can recolour.
 *
 * Part of FTX for Linux.  Copyright (C) 2026 Richard Fakenberg, OK1BR.
 * GPL-3.0-or-later.  No warranty.
 */
#ifndef FTX_QSO_PANEL_H
#define FTX_QSO_PANEL_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define FTX_TYPE_QSO_PANEL (ftx_qso_panel_get_type())
G_DECLARE_FINAL_TYPE(FtxQsoPanel, ftx_qso_panel, FTX, QSO_PANEL, GtkWidget)

typedef enum {
  FTX_QSO_ACTION_CALL_CQ,
  FTX_QSO_ACTION_ENABLE_TX,     /* read the state with ftx_qso_panel_get_tx_enabled */
  FTX_QSO_ACTION_HALT_TX,
  FTX_QSO_ACTION_TUNE,          /* read the state with ftx_qso_panel_get_tune */
  FTX_QSO_ACTION_LOG_QSO,
} FtxQsoAction;

GtkWidget *ftx_qso_panel_new(void);

/* Setters do not emit "changed". */
void        ftx_qso_panel_set_dx(FtxQsoPanel *p, const char *call, const char *grid);
const char *ftx_qso_panel_get_dx_call(FtxQsoPanel *p);
const char *ftx_qso_panel_get_dx_grid(FtxQsoPanel *p);
void        ftx_qso_panel_set_report(FtxQsoPanel *p, int report);
int         ftx_qso_panel_get_report(FtxQsoPanel *p);
void        ftx_qso_panel_set_received(FtxQsoPanel *p, const char *text);
void        ftx_qso_panel_set_info(FtxQsoPanel *p, const char *text);
/* The card: a state line, the message in large type, a detail line, red when on the air. */
void        ftx_qso_panel_set_card(FtxQsoPanel *p, const char *state, const char *message, const char *detail, gboolean on_air);
void        ftx_qso_panel_set_message(FtxQsoPanel *p, int i, const char *text, gboolean valid);
void        ftx_qso_panel_set_next(FtxQsoPanel *p, int i);           /* 0..5 */
int         ftx_qso_panel_get_next(FtxQsoPanel *p);
void        ftx_qso_panel_set_on_air(FtxQsoPanel *p, int i);         /* -1 = none */
void        ftx_qso_panel_set_tx_hz(FtxQsoPanel *p, int hz);
int         ftx_qso_panel_get_tx_hz(FtxQsoPanel *p);
void        ftx_qso_panel_set_hold(FtxQsoPanel *p, gboolean v);
gboolean    ftx_qso_panel_get_hold(FtxQsoPanel *p);
void        ftx_qso_panel_set_tx_eq_rx(FtxQsoPanel *p, gboolean v);
gboolean    ftx_qso_panel_get_tx_eq_rx(FtxQsoPanel *p);
void        ftx_qso_panel_set_auto_seq(FtxQsoPanel *p, gboolean v);
gboolean    ftx_qso_panel_get_auto_seq(FtxQsoPanel *p);
void        ftx_qso_panel_set_tx_enabled(FtxQsoPanel *p, gboolean v);
gboolean    ftx_qso_panel_get_tx_enabled(FtxQsoPanel *p);
void        ftx_qso_panel_set_tune(FtxQsoPanel *p, gboolean v);
gboolean    ftx_qso_panel_get_tune(FtxQsoPanel *p);
/* Grey out the transmit controls / the Log QSO button (with the reason as tooltip). */
void        ftx_qso_panel_set_tx_available(FtxQsoPanel *p, gboolean available, const char *why);
void        ftx_qso_panel_set_log_available(FtxQsoPanel *p, gboolean available, const char *why);

/* Signals: "changed" (any editable field: DX call/grid, report, next message,
 * Tx frequency, Hold, Tx=Rx, Auto sequence) and "action" (gint FtxQsoAction). */

G_END_DECLS

#endif /* FTX_QSO_PANEL_H */
