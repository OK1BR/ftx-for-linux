/* ftx_ldpc_tables.h — tables of the (174,91) LDPC code (see ftx_ldpc_tables.c). */
#ifndef FTX_LDPC_TABLES_H
#define FTX_LDPC_TABLES_H

#include <stdint.h>

#define FTX_LDPC_N 174   /* codeword bits               */
#define FTX_LDPC_K 91    /* message bits (77 + CRC14)   */
#define FTX_LDPC_M 83    /* parity checks (N - K)       */
#define FTX_LDPC_NCW 3   /* checks per bit              */

/* Generator matrix rows (M x K) as 23 hex digits each, MSB first; the last
 * digit carries only 3 bits (columns 89..91). */
extern const char *const ftx_ldpc_generator_hex[FTX_LDPC_M];
/* Mn(3,N): the three checks each bit takes part in (1-based check numbers). */
extern const uint8_t ftx_ldpc_mn[FTX_LDPC_N][3];
/* Nm(7,M): the bits of each check (1-based bit numbers, 0 = unused slot). */
extern const uint8_t ftx_ldpc_nm[FTX_LDPC_M][7];
/* nrw(M): number of bits in each check (6 or 7). */
extern const uint8_t ftx_ldpc_nrw[FTX_LDPC_M];

#endif /* FTX_LDPC_TABLES_H */
