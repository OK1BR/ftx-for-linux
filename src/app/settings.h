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

#include "bands.h"

typedef struct {
  /* [station] */
  char     call[16];        /* my callsign                                  */
  char     grid[8];         /* my grid locator (4 or 6 characters)          */
  gboolean skip_tx1;        /* start a QSO with Tx2 (report) instead of Tx1 */
  gboolean rr73;            /* Tx4 = RR73 (else RRR)                        */
  gboolean dclick_tx;       /* a double-click on a decode also enables TX   */
  int      watchdog_min;    /* TX watchdog, minutes                         */
  /* [radio] */
  char     tci_server[160]; /* "unix:/path", "ws://host:port" or "" = the local radio: sdr-for-linux's
                               socket, then ws://127.0.0.1:40001 (ftx_tci_client_new)                 */
  gboolean connect_at_start;
  int      tx_level_pct;    /* TX audio level, % of full scale               */
  gboolean check_mode;      /* refuse to key unless the radio is in DIGU/USB */
  /* [frequencies] — dial frequency per band and mode, Hz (0 = none); the
   * band switcher tunes the radio here over TCI.  WSJT-X defaults. */
  int      freq_ft8[FTX_NBANDS], freq_ft4[FTX_NBANDS];
  /* [decoder] */
  int      depth;           /* 1..3                                         */
  gboolean ap;              /* a-priori decoding (incl. a7/a8)              */
  int      nfa, nfb;        /* decoding range, Hz                           */
  double   clock_warn_dt;   /* mean |DT| above which the clock is flagged   */
  gboolean show_dt_health;
  /* [display] */
  int      palette;         /* family palette index (0 = Classic)           */
  int      filter_op;       /* Rx/Tx passband overlay opacity, % (sdr-for-linux's filter_op) */
  int      rows_per_s;      /* waterfall rows per second                    */
  gboolean wf_labels;       /* callsigns on the waterfall                   */
  gboolean hl_cq;
  gboolean dim_b4;
  gboolean tag_dxcc;        /* "DXCC" tag on stations whose entity the logger reports as new */
  int      country;         /* country after the message: 0 off, 1 CQ calls (WSJT-X "Show DXCC…"), 2 all messages */
  int      keep_periods;    /* Band activity history                        */
  gboolean newest_first;    /* lists: the latest period at the top          */
  gboolean follow_new;      /* lists: jump to the newest period on every decode */
  /* [logging] */
  gboolean all_txt;         /* write ALL.TXT                                */
  gboolean udp_enable;      /* speak WSJT-X UDP to the logger (log-for-linux) */
  char     udp_host[64];
  int      udp_port;        /* 2237                                          */
  gboolean auto_log;        /* log the QSO when our 73 / RR73 has gone out   */
  gboolean psk_enable;      /* spot the stations we decode to PSK Reporter (off: nothing leaves the machine) */
  char     psk_antenna[256]; /* antennaInformation, shown on the PSK Reporter map */
  gboolean psk_tcp;         /* TCP instead of UDP (WSJT-X "Use TCP/IP connection") */
  /* [state] — operating state remembered across runs */
  gboolean ft4;
  int      rx_hz, tx_hz;
  gboolean hold_tx, tx_eq_rx, auto_seq;
  int      report;          /* default report to send                       */
  /* [window] */
  int      win_w, win_h;
  gboolean win_max;
  int      paned_pos;       /* waterfall / lower part                       */
  int      lists_pos;       /* Band activity / Rx frequency divider         */
} FtxSettings;

void        ftx_settings_defaults(FtxSettings *s);
/* Overlay the file's values; returns TRUE if the file existed and parsed. */
gboolean    ftx_settings_load(FtxSettings *s);
gboolean    ftx_settings_save(const FtxSettings *s);
const char *ftx_settings_path(void);
/* The directory for ALL.TXT and other data files (created on demand). */
const char *ftx_settings_data_dir(void);

#endif /* FTX_SETTINGS_H */
