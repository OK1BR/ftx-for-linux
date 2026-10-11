/* decode_list.h — one decode pane (Band activity / Rx frequency): title,
 * column header and a list of monospace decode lines in the jt9 layout
 * (UTC dB DT Freq Message), coloured by kind, with a separator between
 * periods.  Double-click or Enter on a row = "decode-activated".
 *
 * Part of FTX for Linux.  Copyright (C) 2026 Richard Fakenberg, OK1BR.
 * GPL-3.0-or-later.  No warranty.
 */
#ifndef FTX_DECODE_LIST_H
#define FTX_DECODE_LIST_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define FTX_TYPE_DECODE_LIST (ftx_decode_list_get_type())
G_DECLARE_FINAL_TYPE(FtxDecodeList, ftx_decode_list, FTX, DECODE_LIST, GtkWidget)

typedef enum { FTX_DECODE_PLAIN, FTX_DECODE_CQ, FTX_DECODE_TO_ME, FTX_DECODE_B4, FTX_DECODE_TX } FtxDecodeKind;

typedef struct {
  int   nutc;
  int   snr;
  float dt;
  int   freq;
  char  msg[38];
  FtxDecodeKind kind;
  gboolean ft4;
  int   nap;        /* a-priori type used (0 = none), shown as "aN" like jt9 */
  float qual;       /* AP quality; < 0.17 = low confidence, jt9's "?"       */
} FtxDecodeRow;

GtkWidget *ftx_decode_list_new(const char *title);
/* Add a line to its period; a separator parts the periods.  Periods run
 * oldest to newest (WSJT-X) or newest first; within a period the lines keep
 * the decoder's order either way. */
void ftx_decode_list_add(FtxDecodeList *l, const FtxDecodeRow *row);
/* Drop the oldest periods beyond keep. */
void ftx_decode_list_prune(FtxDecodeList *l, int keep_periods);
void ftx_decode_list_clear(FtxDecodeList *l);
/* Order of the periods; switching rebuilds the pane from its history. */
void ftx_decode_list_set_order(FtxDecodeList *l, gboolean newest_first);
/* TRUE: every new line scrolls the pane to the newest period (the top when
 * newest first, else the bottom).  FALSE: only when it was already there. */
void ftx_decode_list_set_follow(FtxDecodeList *l, gboolean follow);
/* The dim note at the right of the pane header (decode count, Rx frequency). */
void ftx_decode_list_set_note(FtxDecodeList *l, const char *text);

/* Signal: "decode-activated" (gpointer FtxDecodeRow *) */

G_END_DECLS

#endif /* FTX_DECODE_LIST_H */
