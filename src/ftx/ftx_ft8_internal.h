/* ftx_ft8_internal.h — the FT8 decoder context and private helpers.
 * See ftx_ft8.h for provenance and license (GPL-3.0-or-later). */
#ifndef FTX_FT8_INTERNAL_H
#define FTX_FT8_INTERNAL_H

#include <complex.h>

#include "ftx_ft8.h"

#define FT8_JZ 62                         /* sync8: max lag +/- in 1/4 symbols */
#define FT8_NFFT_DS 192000                /* ft8_downsample long FFT            */
#define FT8_NFILT 4000                    /* subtractft8 filter length          */
#define FT8_NTAB 65536                    /* gen_ft8wave phase table            */
#define FT8_A7_MAXDEC 200                 /* ft8_a7 table size                  */

/* cmplx(a,b) without C11's CMPLXF: exact, no arithmetic on the parts */
#define FTX_CMPLXF(a, b) __builtin_complex((float)(a), (float)(b))

struct ftx_ft8_ctx {
  ftx_pack77_ctx pack77;
  ftx_ldpc_ctx   ldpc;

  /* FFT plans with their in-place buffers (four2a plans keyed by array) */
  ftx_fft fft_sym;      /* r2c 3840     sync8 symbol spectra                 */
  ftx_fft fft_base;     /* r2c 3840     get_spectrum_baseline                */
  ftx_fft fft_long;     /* r2c 192000   ft8_downsample, cached spectrum of dd */
  ftx_fft fft_ds;       /* c2c 3200 bwd ft8_downsample output                */
  ftx_fft fft_sym32;    /* c2c 32 fwd   ft8b symbol spectra                  */

  /* get_spectrum_baseline: Nuttall window, normalised */
  float gsb_window[FT8_NFFT1];

  /* sync8 work arrays (heap: 2.9 MB + 1 MB) */
  float *s;             /* s(i,j), i = 1..NH1 bin, j = 1..NHSYM step: s[(j-1)*NH1 + (i-1)] */
  float *sync2d;        /* sync2d(i,j), j = -JZ..JZ, stored lag-contiguous: [(i-1)*(2JZ+1) + j+JZ] */

  /* ft8_downsample taper */
  float taper[101];

  /* sync8d: the 7 Costas tone waveforms */
  float complex csync[7][32];

  /* ft8_a8d: forward c2c 3200 */
  ftx_fft fft_a8;

  /* gen_ft8wave: frequency-smoothing pulse (cached per nsym/nsps/bt/fs) and phase table */
  float gw_pulse[23040];
  float gw_fchk0;
  float complex gw_ctab[FT8_NTAB];
  float *gw_dphi;                   /* (nsym+2)*nsps, sized for nsps up to 7680 */

  /* subtractft8: filter spectrum cw, end corrections, work buffers */
  ftx_fft fft_sub_fwd;              /* c2c 180000 forward (cfilt)  */
  ftx_fft fft_sub_bwd;              /* c2c 180000 backward (cfilt) */
  ftx_fft fft_sub_r2c;              /* r2c 180000 (sqf spectrum)   */
  float complex *sub_cw;            /* 180000 */
  float complex *sub_cref;          /* 151680 */
  float complex *sub_camp;          /* 180000 */
  float *sub_dd;                    /* 180000 working copy        */
  float sub_endcorrection[FT8_NFILT / 2 + 1];

  /* ft8_decode `save` state: the working waveform, early decodes */
  float *dd;                        /* 180000 */
  float *dd1;                       /* 180000 */
  int   nutc0;
  int   ndec_early;
  int   itone_save[FT8_MAX_EARLY][FT8_NN];
  float f1_save[FT8_MAX_EARLY];
  float xdt_save[FT8_MAX_EARLY];
  bool  lsubtracted[FT8_MAX_EARLY];
  char  allmessages[FT8_MAX_EARLY][38];

  /* module ft8_a7 tables: [i][j = even/odd sequence][k = previous/current] */
  float dt0[FT8_A7_MAXDEC][2][2];
  float f0[FT8_A7_MAXDEC][2][2];
  char  msg0[FT8_A7_MAXDEC][2][2][38];
  int   ndec[2][2];
  int   jseq;
};

/* ftx_ft8_demod.c: allocate/free the demodulation part of the context */
bool ftx_ft8_demod_init(ftx_ft8_ctx *ctx);
void ftx_ft8_demod_free(ftx_ft8_ctx *ctx);
/* ftx_ft8_demod.c: pieces shared with the a7/a8 decoders */
void ftx_ft8_refine(ftx_ft8_ctx *ctx, const float *dd0, bool *newdat, float *f1p, float xdt, int *ibestp,
                    float complex cs[FT8_NN][8], float s8[FT8_NN][8], int *nsyncp);
void ftx_ft8_bitmetrics(const float complex cs[FT8_NN][8], int imetric, float *bmeta, float *bmetb,
                        float *bmetc, float *bmetd);
void ftx_ft8_normalizebmet(float *bmet, int n);
bool ftx_ft8_stdcall(const char *callsign);
/* ftx_ft8_a7a8.c */
void ftx_ft8_genft8(ftx_ft8_ctx *ctx, const char *msg, int *i3, int *n3, char msgsent[38],
                    uint8_t msgbits[77], int itone[79]);
void ftx_ft8_a7d(ftx_ft8_ctx *ctx, const float *dd0, bool *newdat, const char *call_1, const char *call_2,
                 const char *grid4, float *xdt, float *f1, float xbase, int *nharderrors, float *dmin,
                 char msg37[38], float *xsnr);
void ftx_ft8_a8d(ftx_ft8_ctx *ctx, const float *dd, const char *mycall, const char *dxcall,
                 const char *dxgrid, float f1a, float *xdt, float *fbest, float *xsnr, float *plog,
                 char msgbest[38]);
/* ftx_ft8_decode.c */
bool ftx_ft8_decode_init(ftx_ft8_ctx *ctx);
void ftx_ft8_decode_free(ftx_ft8_ctx *ctx);

#endif /* FTX_FT8_INTERNAL_H */
