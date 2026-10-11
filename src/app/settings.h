/* settings.h — persistent application state: a GKeyFile INI at
 * $XDG_CONFIG_HOME/ftx-for-linux/config.ini (the sdr-for-linux pattern: no
 * GSettings schema to compile, human-editable).  The caller fills the struct
 * with defaults, overlays the file, and saves the whole struct back.
 *
 * Part of FTX for Linux.  Copyright (C) 2026 Richard Fakenberg, OK1BR.
 * GPL-3.0-or-later.  No warranty.
 */
#ifndef FTX_SETTINGS_H
#define FTX_SETTINGS_H

#include <glib.h>

typedef struct {
  /* [station] */
  char     call[16];        /* my callsign                                  */
  char     grid[8];         /* my grid locator (4 or 6 characters)          */
  gboolean skip_tx1;        /* start a QSO with Tx2 (report) instead of Tx1 */
  gboolean rr73;            /* Tx4 = RR73 (else RRR)                        */
  int      watchdog_min;    /* TX watchdog, minutes                         */
  /* [radio] */
  char     tci_host[64];
  int      tci_port;
  gboolean connect_at_start;
  /* [decoder] */
  int      depth;           /* 1..3                                         */
  gboolean ap;              /* a-priori decoding (incl. a7/a8)              */
  int      nfa, nfb;        /* decoding range, Hz                           */
  double   clock_warn_dt;   /* mean |DT| above which the clock is flagged   */
  gboolean show_dt_health;
  /* [display] */
  int      palette;         /* family palette index (0 = Classic)           */
  int      rows_per_s;      /* waterfall rows per second                    */
  gboolean wf_labels;       /* callsigns on the waterfall                   */
  gboolean hl_cq;
  gboolean dim_b4;
  int      keep_periods;    /* Band activity history                        */
  /* [logging] */
  gboolean all_txt;         /* write ALL.TXT                                */
  /* [state] — operating state remembered across runs */
  gboolean ft4;
  int      rx_hz, tx_hz;
  gboolean hold_tx, tx_eq_rx, auto_seq;
  int      report;          /* default report to send                       */
  /* [window] */
  int      win_w, win_h;
  gboolean win_max;
  int      paned_pos;
} FtxSettings;

void        ftx_settings_defaults(FtxSettings *s);
/* Overlay the file's values; returns TRUE if the file existed and parsed. */
gboolean    ftx_settings_load(FtxSettings *s);
gboolean    ftx_settings_save(const FtxSettings *s);
const char *ftx_settings_path(void);
/* The directory for ALL.TXT and other data files (created on demand). */
const char *ftx_settings_data_dir(void);

#endif /* FTX_SETTINGS_H */
