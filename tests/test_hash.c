/* test_hash.c — ftx_ihashcall against WSJT-X v3.0.2
 * tests/unit/packjt77/test_packjt77_hash_vectors.f90 (13 vectors). */
#include "ftx_hash.h"

#include <stdio.h>
#include <string.h>

struct vec { const char *call; int h10, h12, h22; };

static const struct vec vectors[] = {
  { "K1ABC",       712, 2851, 2920267 },
  { "W9XYZ",       972, 3889, 3982604 },
  { "PJ4/K1ABC",   346, 1387, 1420834 },
  { "KH1/KH7Z",    201,  806,  825805 },
  { "K1JT/P",      959, 3839, 3931158 },
  { "W3CCX",       665, 2662, 2726483 },
  { "3DA0ABC",      48,  192,  196695 },
  { "3XABC",       563, 2254, 2308721 },
  { "///////////", 671, 2685, 2749801 },
  { "ZZZZZZZZZZZ", 902, 3609, 3695718 },
  { "99999999999", 762, 3050, 3123740 },
  { "A23456789Z/", 122,  488,  499902 },
  { "           ",   0,    0,       0 },
};

int main(void)
{
  int fails = 0, n = 0;
  for (size_t i = 0; i < sizeof vectors / sizeof vectors[0]; i++) {
    const struct vec *v = &vectors[i];
    int g10 = ftx_ihashcall(v->call, 10);
    int g12 = ftx_ihashcall(v->call, 12);
    int g22 = ftx_ihashcall(v->call, 22);
    n++;
    if (g10 != v->h10 || g12 != v->h12 || g22 != v->h22) {
      fails++;
      printf("FAIL \"%s\": got %d %d %d, want %d %d %d\n", v->call,
             g10, g12, g22, v->h10, v->h12, v->h22);
    }
  }
  /* A 13-char Fortran field padded with blanks equals the NUL-terminated
   * string: "K1ABC" and "K1ABC        " must hash alike. */
  n++;
  if (ftx_ihashcall("K1ABC        ", 22) != 2920267) {
    fails++;
    printf("FAIL blank padding equivalence\n");
  }
  /* The blank call hashes to zero at every width. */
  n++;
  if (ftx_ihashcall("", 10) != 0 || ftx_ihashcall("", 22) != 0) {
    fails++;
    printf("FAIL empty string\n");
  }
  printf("hash: %d checks, %d failures\n", n, fails);
  return fails == 0 ? 0 : 1;
}
