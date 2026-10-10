/* ftx_internal.h — helpers shared inside libftx (not part of the public API).
 * Copyright (C) 2026 Richard Fakenberg, OK1BR.  GPL-3.0-or-later. */
#ifndef FTX_INTERNAL_H
#define FTX_INTERNAL_H

/* indexx (WSJT-X lib/indexx.f90, Numerical Recipes): indx[1..n] orders
 * arr[1..n] ascending; both arrays are 1-based (element 0 unused). */
void ftx_indexx(const float *arr, int n, int *indx);

#endif /* FTX_INTERNAL_H */
