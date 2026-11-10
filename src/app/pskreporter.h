/* pskreporter.h — spotting to PSK Reporter (https://pskreporter.info): the
 * stations we decode go out as IPFIX reception reports, so the operator
 * shows up on the map as a receiving station.
 *
 * Ported from WSJT-X v3.0.2: Network/PSKReporterIPFIX.cpp (the packet
 * builder: templates 0x50e2 / 0x50e3 under enterprise number 30351, a 5-byte
 * frequency, 4-byte alignment, split at the payload limit) and
 * Network/PSKReporter.cpp (the client: report.pskreporter.info port 4739,
 * UDP by default or TCP, reports batched and sent every 121 s, templates in
 * the first three UDP datagrams and again hourly, a call not repeated within
 * 5 minutes, at most 2048 pending spots).  PSKReporter.cpp was "implemented
 * by Edson Pereira PY2SDR, updated by Bill Somerville, G4WJS".
 *
 * Deviations: the cache of recently spotted calls belongs to the reporter
 * (WSJT-X: one per process); a transport change carries the pending spots
 * over to the new socket instead of flushing them on the old one; a failed
 * connection is retried after 30 s, not on every call; the eclipse dates
 * (WSJT-X's eclipse.txt, +/- 6 h around a solar eclipse every spot goes
 * through for HamSCI) are compiled in.
 *
 * Copyright (C) 2001-2026 by Joe Taylor, K1JT, Bill Somerville, G4WJS,
 * Steve Franke, K9AN, Nico Palermo, IV3NWV, Uwe Risse, DG2YCB, Brian Moran,
 * N9ADG, Roger Rehr, W3SZ, John Nelson, G4KLA, Charlie Suckling, DL3WDG,
 * Terrell Deppe, KJ5HST (WSJT-X).  C port Copyright (C) 2026 Richard
 * Fakenberg, OK1BR.  Part of FTX for Linux.  GPL-3.0-or-later.  No warranty.
 */
#ifndef FTX_PSKREPORTER_H
#define FTX_PSKREPORTER_H

#include <glib.h>

G_BEGIN_DECLS

#define FTX_PSK_HOST              "report.pskreporter.info"
#define FTX_PSK_PORT              4739
#define FTX_PSK_MIN_SEND_INTERVAL 120                               /* seconds; the timer runs one longer */
#define FTX_PSK_FLUSH_INTERVAL    (FTX_PSK_MIN_SEND_INTERVAL + 5)   /* in send intervals */
#define FTX_PSK_MAX_PENDING_SPOTS 2048
#define FTX_PSK_CACHE_TIMEOUT     300                               /* seconds before a call is spotted again */
#define FTX_PSK_MAX_UDP_PAYLOAD   (1000 - 40 - 8)                   /* IP packet 1000 - IPv6 header - UDP header */
#define FTX_PSK_MAX_TCP_PAYLOAD   0xffff
#define FTX_PSK_CONNECT_TIMEOUT   20                                /* seconds for a TCP connection to open */

/* ---- the packet builder (pure; PSKReporterIPFIX.cpp) ------------------------ */

/* The receiving station.  NULL strings count as empty; UTF-8, cut to 32, 16,
 * 80, 128 and 128 bytes at a character boundary. */
typedef struct {
  const char *callsign, *locator, *program_info, *antenna, *rig_information;
} FtxPskReceiver;

/* One heard station.  The strings are cut to 32 (call), 16 (locator) and 16
 * (mode) bytes in the packet. */
typedef struct {
  char    callsign[48];
  char    locator[32];
  int     snr;             /* dB, sent as one signed byte          */
  guint64 frequency;       /* Hz, sent as 5 bytes                  */
  char    mode[32];        /* "FT8", "FT4"                         */
  gint64  time_s;          /* unix UTC seconds of the period start */
} FtxPskSpot;

typedef struct {
  GByteArray *payload;
  int         spot_count;
} FtxPskPacket;

/* PSKReporterIPFIX::buildPackets: one IPFIX message per packet, each with the
 * receiver record; the templates only in the first one when asked for; the
 * spots split so that no payload exceeds max_payload_bytes.  The sequence
 * number of a follow-on packet is advanced by the spots before it.  Returns
 * FtxPskPacket pointers (free with g_ptr_array_unref). */
GPtrArray *ftx_psk_build_packets(const FtxPskReceiver *receiver, const FtxPskSpot *spots, guint nspots,
                                 gboolean include_descriptors, guint32 sequence_number,
                                 guint32 observation_id, guint32 export_time, int max_payload_bytes);

/* ---- the client (PSKReporter.cpp; GLib main context) ------------------------- */

typedef struct _FtxPskReporter FtxPskReporter;

typedef struct {
  gboolean    active;        /* the report timer runs                        */
  gboolean    connected, tcp;
  guint       spots_sent, packets_sent, pending, errors;
  const char *error;         /* the last failure, NULL after a good send     */
  gint64      last_send_s;   /* unix seconds of the last packet, 0 = none    */
} FtxPskState;

typedef void   (*FtxPskStateCb)(gpointer user);
typedef gint64 (*FtxPskClock)(gpointer user);      /* unix seconds */

/* program_info is the decoderSoftware string ("FTX for Linux v0.1.0").
 * Nothing is opened and nothing leaves the machine until the first
 * _set_local / _add / _send. */
FtxPskReporter *ftx_pskreporter_new(const char *program_info);
void            ftx_pskreporter_free(FtxPskReporter *p);
/* The collector; NULL / 0 = report.pskreporter.info:4739.  For the gates and
 * a local test listener.  Takes effect at the next connection. */
void ftx_pskreporter_set_server(FtxPskReporter *p, const char *host, guint16 port);
/* Options.use_tcpip; a change reconnects at the next call. */
void ftx_pskreporter_set_tcp(FtxPskReporter *p, gboolean use_tcp);
void ftx_pskreporter_set_state_cb(FtxPskReporter *p, FtxPskStateCb cb, gpointer user);
void ftx_pskreporter_set_clock(FtxPskReporter *p, FtxPskClock clock, gpointer user);   /* tests */

/* setLocalStation */
void     ftx_pskreporter_set_local(FtxPskReporter *p, const char *call, const char *grid,
                                   const char *antenna, const char *rig_information);
/* addRemoteStation: queues the spot unless the call was spotted within the
 * last 5 minutes (always above 49 MHz and around an eclipse).  FALSE when an
 * older pending spot had to be dropped to make room. */
gboolean ftx_pskreporter_add(FtxPskReporter *p, const char *call, const char *grid, guint64 freq_hz,
                             const char *mode, int snr, gint64 time_s);
/* sendReport: send what is pending now; last = then close the connection and
 * stop the timers (spotting switched off). */
void     ftx_pskreporter_send(FtxPskReporter *p, gboolean last);
void     ftx_pskreporter_get_state(const FtxPskReporter *p, FtxPskState *st);

G_END_DECLS

#endif /* FTX_PSKREPORTER_H */
