/* bands.h — the HF and 6 m amateur bands FTX works on, with the WSJT-X
 * default working frequencies for FT8 and FT4, so the band switcher can put
 * the radio on the right dial frequency for the mode over TCI.
 *
 * The frequencies are the preferred entries of default_frequency_list in
 * WSJT-X models/FrequencyList.cpp @ v3.0.2 (160 m has no FT4 entry there).
 * The band edges are the generous IARU-wide ones — the BANDS table of
 * sdr-for-linux gui.c — used only to tell which band the dial is on.
 *
 * Part of FTX for Linux.  Copyright (C) 2026 Richard Fakenberg, OK1BR.
 * GPL-3.0-or-later.  No warranty.
 */
#ifndef FTX_BANDS_H
#define FTX_BANDS_H

#define FTX_NBANDS 11

typedef struct {
  const char *name;     /* "20 m" — shown in the header                 */
  const char *key;      /* "20m" — config key suffix                    */
  long        lo, hi;   /* Hz, inclusive                                */
  long        ft8_hz;   /* WSJT-X default dial frequency, 0 = none      */
  long        ft4_hz;
} FtxBand;

extern const FtxBand ftx_bands[FTX_NBANDS];

/* Index into ftx_bands of the band containing hz, or -1 when out of every band. */
int ftx_band_for_hz(double hz);

#endif /* FTX_BANDS_H */
