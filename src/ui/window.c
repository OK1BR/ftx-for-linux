/* window.c — see window.h.  The QSO logic follows WSJT-X v3.0.2
 * widgets/mainwindow.cpp (processMessage, the Rx-frequency pane rule,
 * write_all) for FT8/FT4 without special operating activities.
 * Copyright (C) 2026 Richard Fakenberg, OK1BR.  GPL-3.0-or-later. */
#include "window.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/messages.h"
#include "app/rx_engine.h"
#include "decode_list.h"
#include "preferences.h"
#include "qso_panel.h"
#include "status_bar.h"
#include "waterfall.h"

/* mainwindow.h */
enum { CALLING, REPLYING, REPORT, ROGER_REPORT, ROGERS, SIGNOFF };

#define TX_UNAVAILABLE "Transmitting over TCI is not implemented yet (RX only)"

struct _FtxWindow {
  AdwApplicationWindow parent_instance;
  FtxSettings    *s;
  FtxRxEngine    *eng;
  ftx_pack77_ctx *pc;

  GtkWidget *toasts, *dial, *modelab, *title, *ft8_btn, *ft4_btn, *monitor_btn, *paned;
  FtxWaterfall  *wf;
  FtxDecodeList *band, *rxl;
  FtxQsoPanel   *qso;
  FtxStatusBar  *sb;
  guint tick_id;

  gboolean connecting, connected;
  double   dial_hz;

  /* QSO state (WSJT-X names) */
  char     his_call[16], his_grid[8], rpt_rcvd[8];
  int      progress;          /* m_QSOProgress */
  int      ntx;               /* m_ntx 1..6    */
  gboolean calling_cq;        /* m_bCallingCQ  */
  gboolean tx_first;          /* m_txFirst     */
  FtxStdMsgs msgs;

  /* last period statistics */
  int      last_nutc;
  FILE    *all_txt;
};

G_DEFINE_FINAL_TYPE(FtxWindow, ftx_window, ADW_TYPE_APPLICATION_WINDOW)

static void gen_messages(FtxWindow *win);
static void update_card(FtxWindow *win);

/* ---- helpers --------------------------------------------------------------- */

static void toast(FtxWindow *win, const char *text)
{
  adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(win->toasts), adw_toast_new(text));
}

static double period_s(FtxWindow *win) { return win->s->ft4 ? 7.5 : 15.0; }
static double bandwidth_hz(FtxWindow *win) { return win->s->ft4 ? 4 * 12000.0 / 576 : 50.0; }   /* 4 tones x 20.83 Hz / 8 x 6.25 Hz */

static const char *band_name(double hz)
{
  static const struct { double lo, hi; const char *name; } bands[] = {
    { 1.8e6, 2.0e6, "160 m" }, { 3.5e6, 4.0e6, "80 m" }, { 5.25e6, 5.45e6, "60 m" }, { 7.0e6, 7.3e6, "40 m" },
    { 10.1e6, 10.15e6, "30 m" }, { 14.0e6, 14.35e6, "20 m" }, { 18.068e6, 18.168e6, "17 m" }, { 21.0e6, 21.45e6, "15 m" },
    { 24.89e6, 24.99e6, "12 m" }, { 28.0e6, 29.7e6, "10 m" }, { 50e6, 54e6, "6 m" }, { 70e6, 70.5e6, "4 m" },
    { 144e6, 148e6, "2 m" }, { 430e6, 440e6, "70 cm" },
  };
  for (gsize i = 0; i < G_N_ELEMENTS(bands); i++)
    if (hz >= bands[i].lo && hz <= bands[i].hi) return bands[i].name;
  return NULL;
}

static void update_title(FtxWindow *win)
{
  char sub[96];
  const char *band = band_name(win->dial_hz);
  if (win->s->call[0]) snprintf(sub, sizeof sub, "%s · %s%s%s", win->s->call, win->s->grid[0] ? win->s->grid : "no grid",
                                band ? " · " : "", band ? band : "");
  else snprintf(sub, sizeof sub, "no station set — Preferences › Station");
  adw_window_title_set_subtitle(ADW_WINDOW_TITLE(win->title), sub);
}

static void update_dial(FtxWindow *win)
{
  char t[32];
  if (win->dial_hz > 0) {
    long hz = (long)(win->dial_hz + 0.5);
    snprintf(t, sizeof t, "%ld.%03ld.%03ld", hz / 1000000, (hz / 1000) % 1000, hz % 1000);
  } else {
    snprintf(t, sizeof t, "—.———.———");
  }
  gtk_label_set_text(GTK_LABEL(win->dial), t);
  const char *mode = win->connected ? ftx_rx_engine_mode(win->eng) : "";
  char *up = g_ascii_strup(mode, -1);
  gtk_label_set_text(GTK_LABEL(win->modelab), up);
  g_free(up);
  update_title(win);
}

static void apply_decoder_params(FtxWindow *win)
{
  ftx_ft8_params p8;
  memset(&p8, 0, sizeof p8);
  p8.nQSOProgress = win->progress;
  p8.nfa = win->s->nfa; p8.nfb = win->s->nfb;
  p8.nfqso = win->s->rx_hz; p8.nftx = win->s->tx_hz;
  p8.ndepth = win->s->depth; p8.napwid = 75; p8.ncontest = 0;
  p8.lapon = win->s->ap; p8.lapcqonly = false; p8.nagain = false;
  g_strlcpy(p8.mycall, win->s->call, sizeof p8.mycall);
  g_strlcpy(p8.hiscall, win->his_call, sizeof p8.hiscall);
  g_strlcpy(p8.hisgrid, win->his_grid, sizeof p8.hisgrid);
  ftx_ft4_params p4;
  memset(&p4, 0, sizeof p4);
  p4.nQSOProgress = win->progress; p4.nfqso = win->s->rx_hz; p4.nfa = win->s->nfa; p4.nfb = win->s->nfb;
  p4.ndepth = win->s->depth; p4.ncontest = 0; p4.lapcqonly = false;
  g_strlcpy(p4.mycall, win->s->call, sizeof p4.mycall);
  g_strlcpy(p4.hiscall, win->his_call, sizeof p4.hiscall);
  ftx_rx_engine_set_ft8_params(win->eng, &p8);
  ftx_rx_engine_set_ft4_params(win->eng, &p4);
}

static void set_rx_hz(FtxWindow *win, int hz)
{
  hz = CLAMP(hz, 100, 5000);
  win->s->rx_hz = hz;
  ftx_waterfall_set_rx_hz(win->wf, hz);
  if (ftx_qso_panel_get_tx_eq_rx(win->qso)) {
    win->s->tx_hz = hz;
    ftx_waterfall_set_tx_hz(win->wf, hz);
    ftx_qso_panel_set_tx_hz(win->qso, hz);
  }
  apply_decoder_params(win);
  update_card(win);
}

static void set_tx_hz(FtxWindow *win, int hz)
{
  hz = CLAMP(hz, 100, 5000);
  win->s->tx_hz = hz;
  ftx_waterfall_set_tx_hz(win->wf, hz);
  ftx_qso_panel_set_tx_hz(win->qso, hz);
  apply_decoder_params(win);
  update_card(win);
}

static void set_dx(FtxWindow *win, const char *call, const char *grid)
{
  g_strlcpy(win->his_call, call ? call : "", sizeof win->his_call);
  g_strlcpy(win->his_grid, grid ? grid : "", sizeof win->his_grid);
  ftx_qso_panel_set_dx(win->qso, win->his_call, win->his_grid);
  /* what we know about him: distance and bearing from the grids */
  char info[160] = "";
  if (win->his_call[0]) {
    double az, km;
    if (win->his_grid[0] && ftx_grid_az_dist(win->s->grid, win->his_grid, &az, &km))
      snprintf(info, sizeof info, "%s · %.0f km · %.0f°", win->his_grid, km, az);
    else if (win->his_grid[0]) snprintf(info, sizeof info, "%s", win->his_grid);
    else snprintf(info, sizeof info, "grid unknown");
  }
  ftx_qso_panel_set_info(win->qso, info);
  apply_decoder_params(win);
}

/* ---- the card and the clock ---------------------------------------------- */

static void update_card(FtxWindow *win)
{
  gint64 now = g_get_real_time();
  double p = period_s(win);
  double secs = (now / 1000000) % 86400 + (now % 1000000) / 1e6;
  double into = fmod(secs, p);
  gboolean odd = ((gint64)floor(secs / p)) % 2 == 1;
  /* which period is ours: tx_first = we transmit in the even period */
  gboolean ours_now = (odd != win->tx_first);
  int start = (int)(floor(secs / p) * p);
  char state[96], detail[48];
  const char *msg = win->msgs.tx[win->ntx - 1];
  if (!win->s->call[0]) {
    snprintf(state, sizeof state, "Set your callsign and grid in Preferences › Station");
    msg = "";
    detail[0] = '\0';
  } else {
    snprintf(state, sizeof state, "%s · %s period %02d%02d%02d · %.0f s left%s", win->connected ? "Receiving" : "Not connected", odd ? "odd" : "even",
             start / 3600, (start / 60) % 60, start % 60, p - into,
             ours_now ? " · our slot" : "");
    snprintf(detail, sizeof detail, "Tx%d · %d Hz", win->ntx, win->s->tx_hz);
  }
  ftx_qso_panel_set_card(win->qso, state, msg[0] ? msg : "(no message)", detail, FALSE);
}

static gboolean tick(gpointer data)
{
  FtxWindow *win = data;
  gint64 now = g_get_real_time();
  gint64 s = (now / 1000000) % 86400;
  double p = period_s(win);
  double secs = s + (now % 1000000) / 1e6;
  double into = fmod(secs, p);
  gboolean odd = ((gint64)floor(secs / p)) % 2 == 1;
  ftx_status_bar_set_clock(win->sb, (int)(s / 3600), (int)((s / 60) % 60), (int)(s % 60), p, into, odd);
  static int last_sec = -1;
  if ((int)s != last_sec) { last_sec = (int)s; update_card(win); }
  return G_SOURCE_CONTINUE;
}

/* ---- messages ------------------------------------------------------------- */

static void gen_messages(FtxWindow *win)
{
  ftx_msg_gen_std(&win->msgs, win->pc, win->s->call, win->s->grid, win->his_call,
                  ftx_qso_panel_get_report(win->qso), win->s->rr73);
  for (int i = 0; i < 6; i++) ftx_qso_panel_set_message(win->qso, i, win->msgs.tx[i], win->msgs.valid[i]);
  update_card(win);
}

static void set_ntx(FtxWindow *win, int n)
{
  win->ntx = CLAMP(n, 1, 6);
  ftx_qso_panel_set_next(win->qso, win->ntx - 1);
  update_card(win);
}

/* MainWindow::processMessage for FT8/FT4, SpecOp::NONE, no VHF features.
 * double_clicked: the operator picked the message (m_bDoubleClicked);
 * otherwise the auto-sequencer reacts to a message for us. */
static void process_message(FtxWindow *win, const FtxDecodeRow *d, gboolean double_clicked, gboolean shift, gboolean ctrl)
{
  char mybase[16], w1[16], w2[16], w3[16], w4[16];
  ftx_msg_base_call(win->s->call, mybase, sizeof mybase);
  if (!mybase[0]) { toast(win, "Set your callsign in Preferences › Station first"); return; }
  const int freq = d->freq;

  if (d->kind == FTX_DECODE_TX) {                 /* our own transmission in the Rx pane */
    if (!shift) set_rx_hz(win, freq);
    if ((ctrl || shift) && !ftx_qso_panel_get_hold(win->qso)) set_tx_hz(win, freq);
    return;
  }

  gboolean tokens_ok = ftx_msg_tokens(d->msg, w1, w2, w3, w4);
  char clean[64];
  {
    gsize j = 0;
    for (const char *p = d->msg; *p && j + 1 < sizeof clean; p++) if (*p != '<' && *p != '>') clean[j++] = *p;
    clean[j] = '\0';
  }
  gboolean is_73 = FALSE;
  {
    char **ws = g_strsplit(clean, " ", -1);
    for (char **p = ws; *p; p++) if (strcmp(*p, "73") == 0 || strcmp(*p, "RR73") == 0) is_73 = TRUE;
    g_strfreev(ws);
  }
  gboolean is_standard = ftx_msg_is_standard(win->pc, d->msg);
  if (!is_73 && !is_standard && !strchr(d->msg, '<')) return;

  /* which period he transmits in → we take the other one */
  int nmod = (int)fmod((double)((d->nutc / 10000) * 3600 + ((d->nutc / 100) % 100) * 60 + d->nutc % 100), 2.0 * period_s(win));
  win->tx_first = nmod != 0;

  if (!tokens_ok || !w1[0] || !w2[0]) return;

  char hiscall[16], hisgrid[8];
  ftx_msg_de_call_and_grid(d->msg, hiscall, hisgrid);
  if (double_clicked && strcmp(hiscall, mybase) == 0) return;             /* no QSO with yourself */
  {
    char pat_me[24], pat_his[24];
    snprintf(pat_me, sizeof pat_me, " %s ", mybase);
    snprintf(pat_his, sizeof pat_his, " %s ", hiscall);
    char padded[72];
    snprintf(padded, sizeof padded, " %s ", clean);
    if (double_clicked && strstr(padded, pat_me) && strstr(padded, pat_his) && strstr(padded, " 73")) return;   /* the final 73 of our QSO */
  }
  {
    char t[24];
    snprintf(t, sizeof t, "%s/R", hiscall);
    if (strstr(clean, t)) g_strlcpy(hiscall, t, sizeof hiscall);
    snprintf(t, sizeof t, "%s/P", hiscall);
    if (strstr(clean, t)) g_strlcpy(hiscall, t, sizeof hiscall);
  }

  char firstcall[16];
  g_strlcpy(firstcall, w1, sizeof firstcall);
  if (strncmp(firstcall, "CQ ", 3) == 0) g_strlcpy(firstcall, "CQ", sizeof firstcall);

  /* Tx frequency follows a CQ or a station in QSO with someone else, unless held */
  if ((ftx_msg_is_callsign(firstcall) && strcmp(firstcall, win->s->call) != 0 && strcmp(firstcall, mybase) != 0 &&
       strcmp(firstcall, "DE") != 0) || strcmp(firstcall, "CQ") == 0 || strcmp(firstcall, "QRZ") == 0 || ctrl || shift) {
    if (!ftx_qso_panel_get_hold(win->qso) || shift || ctrl) set_tx_hz(win, freq);
  }

  char partner_base[16], base_call[16];
  ftx_msg_base_call(ftx_qso_panel_get_dx_call(win->qso), partner_base, sizeof partner_base);
  ftx_msg_base_call(hiscall, base_call, sizeof base_call);

  char dtext[80], pat1[24], pat2[24], pat3[24];
  snprintf(dtext, sizeof dtext, " %s ", clean);
  snprintf(pat1, sizeof pat1, " %s ", mybase);
  snprintf(pat2, sizeof pat2, "/%s ", mybase);
  snprintf(pat3, sizeof pat3, " %s/", mybase);
  const gboolean for_us = strstr(dtext, pat1) || strstr(dtext, pat2) || strstr(dtext, pat3) || strcmp(firstcall, "DE") == 0;
  const int first = win->s->skip_tx1 ? 2 : 1, first_progress = win->s->skip_tx1 ? REPORT : REPLYING;

  if (for_us) {
    char *end;
    long n3 = strtol(w3, &end, 10);
    gboolean w3_num = w3[0] && *end == '\0';
    if ((strstr(w1, mybase) || strcmp(w1, "DE") == 0) &&
        (strstr(w2, partner_base) != NULL || double_clicked || win->progress == CALLING)) {
      if (ftx_msg_is_grid(w3)) {
        set_ntx(win, 2);
        win->progress = REPORT;
      } else if (strcmp(w3, "RRR") == 0 || (w3_num && n3 == 73 && win->progress == ROGERS) || strcmp(w3, "RR73") == 0 ||
                 (strcmp(w3, "R") == 0 && win->progress != REPORT)) {
        if (w3[0] == 'R' && strncmp(w3, "RR73", 4) != 0 && strncmp(w3, "RR", 2) != 0 && win->progress != ROGER_REPORT) {
          set_ntx(win, 4);
        } else if ((win->progress > CALLING && win->progress < ROGERS) || strcmp(w3, "RRR") == 0 || strcmp(w3, "RR73") == 0) {
          set_ntx(win, 5);
        } else if (win->progress == ROGERS) {
          set_ntx(win, 6);
          toast(win, "QSO complete — Log QSO comes with the TX phase");
        } else {
          set_ntx(win, first);
          win->progress = first_progress;
        }
        if (win->progress >= ROGER_REPORT) win->progress = SIGNOFF;
      } else if (win->progress >= REPLYING && w3[0] == 'R') {
        set_ntx(win, 4);
        win->progress = ROGERS;
      } else if (win->progress >= CALLING) {
        if (w3_num && n3 >= -50 && n3 <= 49) {
          if (strncmp(w3, "R-", 2) == 0 || strncmp(w3, "R+", 2) == 0) { set_ntx(win, 4); win->progress = ROGERS; }
          else { set_ntx(win, 3); win->progress = ROGER_REPORT; }
        } else if (w3[0] == 'R' && (w3[1] == '-' || w3[1] == '+')) {
          set_ntx(win, 4);
          win->progress = ROGERS;
        }
      } else {
        return;
      }
    } else if (win->progress >= ROGERS && strstr(w1, mybase) && strcmp(w2, "73") == 0) {
      set_ntx(win, 5);
      win->progress = SIGNOFF;
    } else if (!is_73) {
      set_ntx(win, 2);
      win->progress = REPORT;
    } else {
      return;
    }
  } else if (strcmp(firstcall, "DE") == 0 && strcmp(w3, "73") == 0) {
    set_ntx(win, first);
    win->progress = first_progress;
  } else if (is_73 && !is_standard) {
    set_ntx(win, 5);
    win->progress = SIGNOFF;
  } else {
    set_ntx(win, first);                        /* just work them */
    win->progress = first_progress;
  }

  /* we are reacting to the message */
  win->calling_cq = FALSE;
  if (!shift) set_rx_hz(win, freq);
  if (ftx_msg_is_callsign(hiscall) && (strcmp(base_call, partner_base) != 0 || strcmp(base_call, hiscall) != 0)) {
    const char *grid = strcmp(partner_base, base_call) != 0 ? "" : win->his_grid;
    set_dx(win, hiscall, grid);
  }
  if (ftx_msg_is_grid(hisgrid)) {
    char g4[8];
    g_strlcpy(g4, hisgrid, sizeof g4);
    g4[4] = '\0';
    if (strncmp(win->his_grid, g4, 4) != 0) set_dx(win, win->his_call, g4);
  }
  if (double_clicked) {
    char dxbase[16];
    ftx_msg_base_call(win->his_call, dxbase, sizeof dxbase);
    if (ftx_msg_report_in(win->pc, d->msg, mybase, dxbase, win->rpt_rcvd)) ftx_qso_panel_set_received(win->qso, win->rpt_rcvd);
  }
  /* the report to send is his SNR; FT8/FT4 update it within the QSO too */
  int n = CLAMP(d->snr, -50, 49);
  ftx_qso_panel_set_report(win->qso, n);
  gen_messages(win);
  apply_decoder_params(win);
}

/* ---- engine callbacks (main context) --------------------------------------- */

static void write_all_txt(FtxWindow *win, const FtxRxDecode *d)
{
  if (!win->s->all_txt) return;
  if (!win->all_txt) {
    char *path = g_build_filename(ftx_settings_data_dir(), "ALL.TXT", NULL);
    win->all_txt = fopen(path, "a");
    g_free(path);
    if (!win->all_txt) return;
  }
  char dts[G_ASCII_DTOSTR_BUF_SIZE], mhz[G_ASCII_DTOSTR_BUF_SIZE];
  g_ascii_formatd(dts, sizeof dts, "%4.1f", d->dt);
  g_ascii_formatd(mhz, sizeof mhz, "%10.3f", win->dial_hz / 1e6);
  char d37[38], annot[8] = "";
  snprintf(d37, sizeof d37, "%-37.37s", d->msg);
  if (d->nap != 0) {
    snprintf(annot, sizeof annot, " a%d", d->nap % 10);
    if (d->qual < 0.17f) d37[36] = '?';
  }
  g_strchomp(d37);
  fprintf(win->all_txt, "%06d_%06d%s Rx %-6s %3d %s %4d %s%s\n", d->day, d->nutc, mhz, d->ft4 ? "FT4" : "FT8", d->snr, dts, (int)(d->freq + 0.5f), d37, annot);
}

static void on_decode(const FtxRxDecode *d, gpointer user)
{
  FtxWindow *win = user;
  char mybase[16], w1[16], w2[16], w3[16], w4[16];
  ftx_msg_base_call(win->s->call, mybase, sizeof mybase);
  ftx_msg_tokens(d->msg, w1, w2, w3, w4);

  /* for_us: the first word is my call (the Rx-pane rule of readFromStdout) */
  char first[16] = "";
  {
    char clean[64];
    gsize j = 0;
    for (const char *p = d->msg; *p && j + 1 < sizeof clean; p++) if (*p != '<' && *p != '>') clean[j++] = *p;
    clean[j] = '\0';
    const char *sp = strchr(clean, ' ');
    g_strlcpy(first, clean, sizeof first);
    if (sp && (gsize)(sp - clean) < sizeof first) first[sp - clean] = '\0';
  }
  gboolean for_us = mybase[0] && (strstr(first, mybase) != NULL ||
                                  (strcmp(first, "DE") == 0 && abs((int)d->freq - win->s->rx_hz) <= 10));
  if (mybase[0]) {
    if (strcmp(mybase, win->s->call) == 0) { if (strcmp(mybase, first) != 0) for_us = FALSE; }
    else if (strcmp(win->s->call, first) != 0) for_us = FALSE;
  }
  gboolean is_cq = strncmp(d->msg, "CQ ", 3) == 0 || strncmp(d->msg, "QRZ ", 4) == 0;
  gboolean right = for_us || abs((int)d->freq - win->s->rx_hz) <= 10;
  if (!right && mybase[0] && win->his_call[0]) {
    char pat[40];
    snprintf(pat, sizeof pat, "%s %s", mybase, win->his_call);
    char clean[64];
    gsize j = 0;
    for (const char *p = d->msg; *p && j + 1 < sizeof clean; p++) if (*p != '<' && *p != '>') clean[j++] = *p;
    clean[j] = '\0';
    if (strstr(clean, pat)) right = TRUE;
  }

  FtxDecodeRow row;
  row.nutc = d->nutc; row.snr = d->snr; row.dt = d->dt; row.freq = (int)(d->freq + 0.5f);
  g_strlcpy(row.msg, d->msg, sizeof row.msg);
  row.ft4 = d->ft4;
  row.nap = d->nap;
  row.qual = d->qual;
  row.kind = for_us ? FTX_DECODE_TO_ME : (is_cq && win->s->hl_cq) ? FTX_DECODE_CQ : FTX_DECODE_PLAIN;
  ftx_decode_list_add(win->band, &row);
  if (right) ftx_decode_list_add(win->rxl, &row);

  if (w2[0]) {
    char label[24];
    if (is_cq) snprintf(label, sizeof label, "CQ %s", w2); else g_strlcpy(label, w2, sizeof label);
    if (d->nap != 0 && d->qual < 0.17f) g_strlcat(label, "?", sizeof label);
    ftx_waterfall_add_label(win->wf, d->nutc, d->freq, label,
                            for_us ? FTX_WF_LABEL_TO_ME : is_cq ? FTX_WF_LABEL_CQ : FTX_WF_LABEL_PLAIN);
  }
  write_all_txt(win, d);

  /* the auto-sequencer reacts when our partner calls us (auto_sequence in
   * mainwindow.cpp; WSJT-X also requires TX to be enabled — that gate comes
   * with the TX phase, until then the sequencer shows what it would send) */
  if (ftx_qso_panel_get_auto_seq(win->qso) && for_us && mybase[0]) {
    char partner_base[16];
    ftx_msg_base_call(win->his_call, partner_base, sizeof partner_base);
    gboolean from_partner = partner_base[0] && strstr(w2, partner_base) != NULL;
    if (from_partner || win->calling_cq) process_message(win, &row, FALSE, FALSE, FALSE);
  }
}

static void on_period(const FtxRxPeriod *p, gpointer user)
{
  FtxWindow *win = user;
  char buf[96];
  switch (p->status) {
  case FTX_RX_PERIOD_GAP:
    snprintf(buf, sizeof buf, "%06d skipped · %d ms without audio", p->nutc, p->gap_ms);
    break;
  case FTX_RX_PERIOD_KEYED:
    snprintf(buf, sizeof buf, "%06d skipped · radio keyed", p->nutc);
    break;
  default:
    snprintf(buf, sizeof buf, "%d decode%s · %.1f s · peak %.0f %% FS", p->ndecodes, p->ndecodes == 1 ? "" : "s",
             p->decode_ms / 1000.0, 100.0 * p->peak / 32767.0);
    ftx_status_bar_set_clock_health(win->sb, win->s->show_dt_health, p->have_dt, p->mean_dt, win->s->clock_warn_dt);
    break;
  }
  ftx_status_bar_set_decodes(win->sb, buf);
  if (win->all_txt) fflush(win->all_txt);
  ftx_decode_list_prune(win->band, win->s->keep_periods);
  ftx_decode_list_prune(win->rxl, win->s->keep_periods);
  win->last_nutc = p->nutc;
}

static void on_row(const float *db, guint nbins, double bin_hz, gint64 t_us, gpointer user)
{
  ftx_waterfall_push_row(FTX_WINDOW(user)->wf, db, nbins, bin_hz, t_us);
}

static void on_vfo(double hz, gpointer user)
{
  FtxWindow *win = user;
  win->dial_hz = hz;
  update_dial(win);
}

static void set_audio_status(FtxWindow *win, gboolean tx)
{
  if (!win->connected) { ftx_status_bar_set_audio(win->sb, "● no audio", FTX_STATUS_DIM); return; }
  if (tx) ftx_status_bar_set_audio(win->sb, "● radio keyed · not decoding", FTX_STATUS_TX);
  else ftx_status_bar_set_audio(win->sb, win->s->ft4 ? "● RX  FT4 · 12 kHz TCI audio" : "● RX  FT8 · 12 kHz TCI audio", FTX_STATUS_OK);
}

static void on_tx(gboolean tx, gpointer user) { set_audio_status(FTX_WINDOW(user), tx); }

static void set_tci_status(FtxWindow *win)
{
  char buf[128];
  if (win->connecting) {
    snprintf(buf, sizeof buf, "● TCI connecting to %s:%d…", win->s->tci_host, win->s->tci_port);
    ftx_status_bar_set_tci(win->sb, buf, FTX_STATUS_DIM);
  } else if (win->connected) {
    const char *dev = ftx_rx_engine_device(win->eng);
    char *mode = g_ascii_strup(ftx_rx_engine_mode(win->eng), -1);
    snprintf(buf, sizeof buf, "● TCI %s %s  %s", dev[0] ? dev : "server", win->s->tci_host, mode);
    g_free(mode);
    ftx_status_bar_set_tci(win->sb, buf, FTX_STATUS_OK);
  } else {
    ftx_status_bar_set_tci(win->sb, "● TCI disconnected", FTX_STATUS_ERROR);
  }
  ftx_status_bar_set_logger(win->sb, "● log-for-linux: not linked yet", FTX_STATUS_DIM);
}

static void on_closed(gpointer user)
{
  FtxWindow *win = user;
  ftx_rx_engine_disconnect(win->eng);
  win->connected = FALSE;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(win->monitor_btn), FALSE);
  set_tci_status(win);
  set_audio_status(win, FALSE);
  update_dial(win);
  toast(win, "The TCI server closed the connection");
}

/* ---- connecting ------------------------------------------------------------ */

struct connect_args { char host[64]; guint16 port; };

static void connect_thread(GTask *task, gpointer src, gpointer data, GCancellable *c)
{
  (void)c;
  FtxWindow *win = src;
  struct connect_args *a = data;
  GError *err = NULL;
  if (ftx_rx_engine_connect(win->eng, a->host, a->port, &err)) g_task_return_boolean(task, TRUE);
  else g_task_return_error(task, err);
}

static void connect_done(GObject *src, GAsyncResult *res, gpointer data)
{
  (void)data;
  FtxWindow *win = FTX_WINDOW(src);
  GError *err = NULL;
  win->connecting = FALSE;
  if (g_task_propagate_boolean(G_TASK(res), &err)) {
    win->connected = TRUE;
    win->dial_hz = ftx_rx_engine_vfo_hz(win->eng);
    ftx_waterfall_clear(win->wf);
  } else {
    win->connected = FALSE;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(win->monitor_btn), FALSE);
    toast(win, err->message);
    g_error_free(err);
  }
  set_tci_status(win);
  set_audio_status(win, win->connected && ftx_rx_engine_tx(win->eng));
  update_dial(win);
}

static void do_connect(FtxWindow *win)
{
  if (win->connected || win->connecting) return;
  win->connecting = TRUE;
  set_tci_status(win);
  struct connect_args *a = g_new0(struct connect_args, 1);
  g_strlcpy(a->host, win->s->tci_host, sizeof a->host);
  a->port = (guint16)win->s->tci_port;
  GTask *task = g_task_new(win, NULL, connect_done, NULL);
  g_task_set_task_data(task, a, g_free);
  g_task_run_in_thread(task, connect_thread);
  g_object_unref(task);
}

static void do_disconnect(FtxWindow *win)
{
  if (!win->connected) return;
  ftx_rx_engine_disconnect(win->eng);
  win->connected = FALSE;
  set_tci_status(win);
  set_audio_status(win, FALSE);
  update_dial(win);
}

/* ---- UI handlers ------------------------------------------------------------ */

static void on_monitor(GtkToggleButton *b, gpointer user)
{
  FtxWindow *win = user;
  if (gtk_toggle_button_get_active(b)) do_connect(win); else do_disconnect(win);
}

static void apply_mode(FtxWindow *win)
{
  ftx_rx_engine_set_ft4(win->eng, win->s->ft4);
  ftx_waterfall_set_period(win->wf, period_s(win));
  ftx_waterfall_set_bandwidth(win->wf, bandwidth_hz(win));
  set_audio_status(win, win->connected && ftx_rx_engine_tx(win->eng));
  update_card(win);
}

static void on_mode(GtkToggleButton *b, gpointer user)
{
  FtxWindow *win = user;
  if (!gtk_toggle_button_get_active(b)) return;
  win->s->ft4 = (GTK_WIDGET(b) == win->ft4_btn);
  apply_mode(win);
}

static void on_rx_clicked(FtxWaterfall *wf, double hz, gpointer user) { (void)wf; set_rx_hz(user, (int)(hz + 0.5)); }
static void on_tx_clicked(FtxWaterfall *wf, double hz, gpointer user) { (void)wf; set_tx_hz(user, (int)(hz + 0.5)); }

static void on_decode_activated(FtxDecodeList *l, gpointer row, gpointer user)
{
  (void)l;
  FtxWindow *win = user;
  GdkModifierType mods = 0;
  GdkDisplay *disp = gtk_widget_get_display(GTK_WIDGET(win));
  GdkSeat *seat = gdk_display_get_default_seat(disp);
  GdkDevice *kb = seat ? gdk_seat_get_keyboard(seat) : NULL;
  if (kb) mods = gdk_device_get_modifier_state(kb);
  process_message(win, row, TRUE, (mods & GDK_SHIFT_MASK) != 0, (mods & GDK_CONTROL_MASK) != 0);
}

static void on_qso_changed(FtxQsoPanel *q, gpointer user)
{
  FtxWindow *win = user;
  const char *call = ftx_qso_panel_get_dx_call(q), *grid = ftx_qso_panel_get_dx_grid(q);
  gboolean dx_changed = strcmp(call, win->his_call) != 0 || strcmp(grid, win->his_grid) != 0;
  if (dx_changed) {
    set_dx(win, call, grid);
    if (!call[0]) { win->progress = CALLING; set_ntx(win, 6); }
  }
  win->s->hold_tx = ftx_qso_panel_get_hold(q);
  win->s->tx_eq_rx = ftx_qso_panel_get_tx_eq_rx(q);
  win->s->auto_seq = ftx_qso_panel_get_auto_seq(q);
  win->s->report = ftx_qso_panel_get_report(q);
  win->ntx = ftx_qso_panel_get_next(q) + 1;
  int tx = ftx_qso_panel_get_tx_hz(q);
  if (tx != win->s->tx_hz) set_tx_hz(win, tx);
  if (win->s->tx_eq_rx && win->s->tx_hz != win->s->rx_hz) set_tx_hz(win, win->s->rx_hz);
  gen_messages(win);
}

static void on_qso_action(FtxQsoPanel *q, int action, gpointer user)
{
  (void)q;
  FtxWindow *win = user;
  switch (action) {
  case FTX_QSO_ACTION_CALL_CQ:
    win->progress = CALLING;
    win->calling_cq = TRUE;
    set_ntx(win, 6);
    toast(win, "Next message is CQ — transmitting comes with the TX phase");
    break;
  default:
    toast(win, TX_UNAVAILABLE);
    break;
  }
}

static void apply_settings(FtxWindow *win)
{
  ftx_waterfall_set_palette(win->wf, win->s->palette);
  ftx_waterfall_set_show_labels(win->wf, win->s->wf_labels);
  ftx_waterfall_set_span(win->wf, 0, MAX(win->s->nfb, 1000));
  ftx_rx_engine_set_rows_per_second(win->eng, win->s->rows_per_s);
  ftx_status_bar_set_clock_health(win->sb, win->s->show_dt_health, FALSE, 0, win->s->clock_warn_dt);
  if (!win->s->all_txt && win->all_txt) { fclose(win->all_txt); win->all_txt = NULL; }
  update_title(win);
  gen_messages(win);
  apply_decoder_params(win);
}

static void on_prefs_changed(gpointer user)
{
  FtxWindow *win = user;
  apply_settings(win);
  ftx_settings_save(win->s);
}

static void act_preferences(GSimpleAction *a, GVariant *p, gpointer user)
{
  (void)a; (void)p;
  FtxWindow *win = user;
  adw_dialog_present(ftx_preferences_new(win->s, on_prefs_changed, win), GTK_WIDGET(win));
}

static void act_clear(GSimpleAction *a, GVariant *p, gpointer user)
{
  (void)a; (void)p;
  FtxWindow *win = user;
  ftx_decode_list_clear(win->band);
  ftx_decode_list_clear(win->rxl);
  ftx_waterfall_clear(win->wf);
}

static void act_about(GSimpleAction *a, GVariant *p, gpointer user)
{
  (void)a; (void)p;
  FtxWindow *win = user;
  AdwDialog *dlg = adw_about_dialog_new();
  AdwAboutDialog *ad = ADW_ABOUT_DIALOG(dlg);
  adw_about_dialog_set_application_name(ad, "FTX for Linux");
  adw_about_dialog_set_application_icon(ad, "cz.ok1br.ftx_for_linux");
  adw_about_dialog_set_version(ad, FTX_VERSION);
  adw_about_dialog_set_developer_name(ad, "Richard Fakenberg, OK1BR");
  adw_about_dialog_set_comments(ad, "FT8 and FT4 for TCI radios.  The decoders are ported by hand from WSJT-X "
                                    "and are bit-exact against the original.");
  adw_about_dialog_set_website(ad, "https://github.com/OK1BR/ftx-for-linux");
  adw_about_dialog_set_issue_url(ad, "https://github.com/OK1BR/ftx-for-linux/issues");
  adw_about_dialog_set_copyright(ad, "© 2026 Richard Fakenberg, OK1BR");
  adw_about_dialog_set_license_type(ad, GTK_LICENSE_GPL_3_0);
  char *dbg = ftx_window_debug_info(win);
  adw_about_dialog_set_debug_info(ad, dbg);
  g_free(dbg);
  const char *wsjtx[] = {
    "WSJT-X v3.0.2 — the FT8/FT4 decoders, encoders, the 77-bit message codec and the QSO logic are ported from it. "
    "Copyright (C) 2001-2026 by Joe Taylor K1JT, Bill Somerville G4WJS, Steve Franke K9AN, Nico Palermo IV3NWV, "
    "Uwe Risse DG2YCB, Brian Moran N9ADG, Roger Rehr W3SZ, John Nelson G4KLA, Charlie Suckling DL3WDG and "
    "Terrell Deppe KJ5HST.  https://github.com/WSJTX/wsjtx",
    NULL };
  adw_about_dialog_add_acknowledgement_section(ad, "Ported from", wsjtx);
  const char *family[] = { "sdr-for-linux, icom-for-linux — the TCI radios", "log-for-linux — the logbook (WSJT-X UDP)", NULL };
  adw_about_dialog_add_acknowledgement_section(ad, "Family", family);
  adw_dialog_present(dlg, GTK_WIDGET(win));
}

char *ftx_window_debug_info(FtxWindow *win)
{
  char fftw[64];
  snprintf(fftw, sizeof fftw, "%s", fftwf_version);
  return g_strdup_printf(
    "FTX for Linux %s\n"
    "GTK %u.%u.%u, libadwaita %u.%u.%u, GSK_RENDERER=%s\n"
    "Decoders: ported from WSJT-X v3.0.2 (git ccdfaf3); %s\n"
    "TCI: %s:%d — %s%s%s%s\n"
    "Mode %s, Rx %d Hz, Tx %d Hz, depth %d, AP %s, range %d–%d Hz\n"
    "Config: %s\n"
    "Data: %s",
    FTX_VERSION,
    gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version(),
    adw_get_major_version(), adw_get_minor_version(), adw_get_micro_version(),
    g_getenv("GSK_RENDERER") ? g_getenv("GSK_RENDERER") : "default",
    fftw,
    win->s->tci_host, win->s->tci_port,
    win->connected ? "connected, device \"" : "not connected",
    win->connected ? ftx_rx_engine_device(win->eng) : "",
    win->connected ? "\", protocol " : "",
    win->connected ? ftx_rx_engine_protocol(win->eng) : "",
    win->s->ft4 ? "FT4" : "FT8", win->s->rx_hz, win->s->tx_hz, win->s->depth, win->s->ap ? "on" : "off",
    win->s->nfa, win->s->nfb,
    ftx_settings_path(), ftx_settings_data_dir());
}

static gboolean on_close_request(GtkWindow *w, gpointer user)
{
  (void)user;
  FtxWindow *win = FTX_WINDOW(w);
  win->s->win_max = gtk_window_is_maximized(w);
  if (!win->s->win_max) gtk_window_get_default_size(w, &win->s->win_w, &win->s->win_h);
  win->s->paned_pos = gtk_paned_get_position(GTK_PANED(win->paned));
  ftx_settings_save(win->s);
  return FALSE;
}

/* ---- construction ------------------------------------------------------------ */

static void ftx_window_dispose(GObject *obj)
{
  FtxWindow *win = FTX_WINDOW(obj);
  if (win->tick_id) { g_source_remove(win->tick_id); win->tick_id = 0; }
  if (win->eng) { ftx_rx_engine_free(win->eng); win->eng = NULL; }
  if (win->all_txt) { fclose(win->all_txt); win->all_txt = NULL; }
  g_clear_pointer(&win->pc, g_free);
  G_OBJECT_CLASS(ftx_window_parent_class)->dispose(obj);
}

static void ftx_window_class_init(FtxWindowClass *klass)
{
  G_OBJECT_CLASS(klass)->dispose = ftx_window_dispose;
}

static void ftx_window_init(FtxWindow *win) { (void)win; }

static GtkWidget *mode_button(const char *label)
{
  GtkWidget *b = gtk_toggle_button_new_with_label(label);
  gtk_widget_add_css_class(b, "mode");
  return b;
}

GtkWidget *ftx_window_new(AdwApplication *app, FtxSettings *s)
{
  FtxWindow *win = g_object_new(FTX_TYPE_WINDOW, "application", app, NULL);
  win->s = s;
  win->pc = g_new0(ftx_pack77_ctx, 1);
  ftx_pack77_init(win->pc);
  win->eng = ftx_rx_engine_new(g_main_context_default());
  ftx_rx_engine_set_decode_cb(win->eng, on_decode, win);
  ftx_rx_engine_set_period_cb(win->eng, on_period, win);
  ftx_rx_engine_set_row_cb(win->eng, on_row, win);
  ftx_rx_engine_set_vfo_cb(win->eng, on_vfo, win);
  ftx_rx_engine_set_tx_cb(win->eng, on_tx, win);
  ftx_rx_engine_set_closed_cb(win->eng, on_closed, win);
  win->progress = CALLING;
  win->ntx = 6;

  gtk_window_set_title(GTK_WINDOW(win), "FTX for Linux");
  gtk_window_set_default_size(GTK_WINDOW(win), s->win_w > 0 ? s->win_w : 1480, s->win_h > 0 ? s->win_h : 920);
  if (s->win_max) gtk_window_maximize(GTK_WINDOW(win));
  g_signal_connect(win, "close-request", G_CALLBACK(on_close_request), NULL);

  /* actions */
  const GActionEntry entries[] = {
    { "preferences", act_preferences, NULL, NULL, NULL, { 0 } },
    { "clear", act_clear, NULL, NULL, NULL, { 0 } },
    { "about", act_about, NULL, NULL, NULL, { 0 } },
  };
  g_action_map_add_action_entries(G_ACTION_MAP(win), entries, G_N_ELEMENTS(entries), win);

  /* header bar */
  GtkWidget *hdr = adw_header_bar_new();
  win->title = adw_window_title_new("FTX for Linux", "");
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(hdr), win->title);
  GtkWidget *dialbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  win->dial = gtk_label_new("");
  gtk_widget_add_css_class(win->dial, "dial");
  gtk_widget_set_tooltip_text(win->dial, "Dial frequency of the radio (TCI)");
  win->modelab = gtk_label_new("");
  gtk_widget_add_css_class(win->modelab, "band");
  gtk_box_append(GTK_BOX(dialbox), win->dial);
  gtk_box_append(GTK_BOX(dialbox), win->modelab);
  adw_header_bar_pack_start(ADW_HEADER_BAR(hdr), dialbox);
  GMenu *menu = g_menu_new();
  g_menu_append(menu, "Preferences", "win.preferences");
  g_menu_append(menu, "Clear decodes", "win.clear");
  g_menu_append(menu, "About FTX for Linux", "win.about");
  GtkWidget *mb = gtk_menu_button_new();
  gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(mb), "open-menu-symbolic");
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(mb), G_MENU_MODEL(menu));
  g_object_unref(menu);
  adw_header_bar_pack_end(ADW_HEADER_BAR(hdr), mb);
  GtkWidget *modes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class(modes, "linked");
  win->ft8_btn = mode_button("FT8");
  win->ft4_btn = mode_button("FT4");
  gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(win->ft4_btn), GTK_TOGGLE_BUTTON(win->ft8_btn));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->ft4 ? win->ft4_btn : win->ft8_btn), TRUE);
  g_signal_connect(win->ft8_btn, "toggled", G_CALLBACK(on_mode), win);
  g_signal_connect(win->ft4_btn, "toggled", G_CALLBACK(on_mode), win);
  gtk_box_append(GTK_BOX(modes), win->ft8_btn);
  gtk_box_append(GTK_BOX(modes), win->ft4_btn);
  adw_header_bar_pack_end(ADW_HEADER_BAR(hdr), modes);
  win->monitor_btn = mode_button("Monitor");
  gtk_widget_set_tooltip_text(win->monitor_btn, "Receive: connect to the TCI server and decode");
  g_signal_connect(win->monitor_btn, "toggled", G_CALLBACK(on_monitor), win);
  adw_header_bar_pack_end(ADW_HEADER_BAR(hdr), win->monitor_btn);

  /* waterfall over the lower panes */
  win->wf = FTX_WATERFALL(ftx_waterfall_new());
  g_signal_connect(win->wf, "rx-clicked", G_CALLBACK(on_rx_clicked), win);
  g_signal_connect(win->wf, "tx-clicked", G_CALLBACK(on_tx_clicked), win);
  GtkWidget *lower = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  win->band = FTX_DECODE_LIST(ftx_decode_list_new("Band activity"));
  win->rxl = FTX_DECODE_LIST(ftx_decode_list_new("Rx frequency"));
  gtk_widget_set_hexpand(GTK_WIDGET(win->band), TRUE);
  gtk_widget_set_hexpand(GTK_WIDGET(win->rxl), TRUE);
  g_signal_connect(win->band, "decode-activated", G_CALLBACK(on_decode_activated), win);
  g_signal_connect(win->rxl, "decode-activated", G_CALLBACK(on_decode_activated), win);
  win->qso = FTX_QSO_PANEL(ftx_qso_panel_new());
  g_signal_connect(win->qso, "changed", G_CALLBACK(on_qso_changed), win);
  g_signal_connect(win->qso, "action", G_CALLBACK(on_qso_action), win);
  gtk_box_append(GTK_BOX(lower), GTK_WIDGET(win->band));
  gtk_box_append(GTK_BOX(lower), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
  gtk_box_append(GTK_BOX(lower), GTK_WIDGET(win->rxl));
  gtk_box_append(GTK_BOX(lower), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
  gtk_box_append(GTK_BOX(lower), GTK_WIDGET(win->qso));
  win->paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
  gtk_paned_set_start_child(GTK_PANED(win->paned), GTK_WIDGET(win->wf));
  gtk_paned_set_end_child(GTK_PANED(win->paned), lower);
  gtk_paned_set_position(GTK_PANED(win->paned), s->paned_pos > 0 ? s->paned_pos : 300);
  gtk_paned_set_resize_start_child(GTK_PANED(win->paned), FALSE);
  gtk_widget_set_vexpand(win->paned, TRUE);

  win->sb = FTX_STATUS_BAR(ftx_status_bar_new());
  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append(GTK_BOX(v), hdr);
  gtk_box_append(GTK_BOX(v), win->paned);
  gtk_box_append(GTK_BOX(v), GTK_WIDGET(win->sb));
  win->toasts = adw_toast_overlay_new();
  adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(win->toasts), v);
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(win), win->toasts);

  /* state from the settings */
  ftx_qso_panel_set_tx_available(win->qso, FALSE, TX_UNAVAILABLE);
  ftx_qso_panel_set_hold(win->qso, s->hold_tx);
  ftx_qso_panel_set_tx_eq_rx(win->qso, s->tx_eq_rx);
  ftx_qso_panel_set_auto_seq(win->qso, s->auto_seq);
  ftx_qso_panel_set_report(win->qso, s->report);
  ftx_qso_panel_set_tx_hz(win->qso, s->tx_hz);
  ftx_qso_panel_set_next(win->qso, 5);
  ftx_waterfall_set_rx_hz(win->wf, s->rx_hz);
  ftx_waterfall_set_tx_hz(win->wf, s->tx_hz);
  set_dx(win, "", "");
  apply_mode(win);
  apply_settings(win);
  set_tci_status(win);
  set_audio_status(win, FALSE);
  update_dial(win);
  win->tick_id = g_timeout_add(100, tick, win);
  tick(win);
  if (s->connect_at_start) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(win->monitor_btn), TRUE);
  return GTK_WIDGET(win);
}
