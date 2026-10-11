/* messages.h — standard FT8/FT4 messages and decoded-text helpers, ported
 * from WSJT-X v3.0.2: widgets/mainwindow.cpp (genStdMsgs, genCQMsg,
 * stdCall), Radio.cpp (base_callsign, is_callsign), Decoder/decodedtext.cpp
 * (the message tokenizer, deCallAndGrid, report) and lib/stdmsg.f90.
 * WSJT-X is Copyright (C) 2001-2026 by Joe Taylor, K1JT, Bill Somerville,
 * G4WJS, Steve Franke, K9AN, Nico Palermo, IV3NWV, Uwe Risse, DG2YCB,
 * Brian Moran, N9ADG, Roger Rehr, W3SZ, John Nelson, G4KLA, Charlie
 * Suckling, DL3WDG, and Terrell Deppe, KJ5HST.  https://github.com/WSJTX/wsjtx
 * This port: Copyright (C) 2026 Richard Fakenberg, OK1BR.  GPL-3.0-or-later.
 *
 * Only the FT8/FT4 (77-bit) path without contest exchanges is ported.
 */
#ifndef FTX_MESSAGES_H
#define FTX_MESSAGES_H

#include <glib.h>

#include "ftx/ftx_pack77.h"

G_BEGIN_DECLS

/* The six standard messages; tx[i] is the text as it will be transmitted
 * (pack77 → unpack77), valid[i] false when it does not pack as a standard
 * message.  Empty strings when there is no DX call (except Tx6). */
typedef struct {
  char     tx[6][38];
  gboolean valid[6];
} FtxStdMsgs;

gboolean ftx_msg_std_call(const char *call);                       /* MainWindow::stdCall    */
void     ftx_msg_base_call(const char *call, char *base, gsize n);  /* Radio::base_callsign   */
gboolean ftx_msg_is_callsign(const char *s);                       /* Radio::is_callsign     */
gboolean ftx_msg_is_grid(const char *s);                           /* grid_regexp (4/6 chars, not RR73) */

/* genStdMsgs(report) + genCQMsg.  pc is the caller's pack77 state (its
 * mycall/dxcall are set here). */
void ftx_msg_gen_std(FtxStdMsgs *m, ftx_pack77_ctx *pc, const char *mycall, const char *mygrid,
                     const char *hiscall, int report, gboolean rr73);

/* stdmsg: TRUE when the text packs as something other than free text. */
gboolean ftx_msg_is_standard(ftx_pack77_ctx *pc, const char *msg);

/* DecodedText's tokenizer (angle brackets removed first): w1 = addressee or
 * "CQ"/"CQ DX"/"CQ xxx"/"QRZ"/"DE", w2 = the DE call, w3 = report, grid,
 * RRR/RR73/73 or "R", w4 = the word after "R" (grid).  Absent words are "".
 * Returns FALSE when the text does not look like a standard message. */
gboolean ftx_msg_tokens(const char *msg, char w1[16], char w2[16], char w3[16], char w4[16]);
/* deCallAndGrid: the DE call and whatever stands in the grid position. */
void     ftx_msg_de_call_and_grid(const char *msg, char call[16], char grid[8]);

/* DecodedText::report: SNR → "+dd"/"-dd" (clamped to -50..49). */
void     ftx_msg_report_string(int snr, char rpt[8]);
/* DecodedText::report(myBase, dxBase): the report sent to me in msg (with
 * or without the leading R), TRUE when found. */
gboolean ftx_msg_report_in(ftx_pack77_ctx *pc, const char *msg, const char *mybase, const char *dxbase, char rpt[8]);

/* Maidenhead locator → centre latitude/longitude (degrees), great-circle
 * azimuth (degrees) and distance (km); FALSE for an invalid locator. */
gboolean ftx_grid_latlon(const char *grid, double *lat, double *lon);
gboolean ftx_grid_az_dist(const char *mygrid, const char *hisgrid, double *az, double *km);

G_END_DECLS

#endif /* FTX_MESSAGES_H */
