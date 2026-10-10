/* test_crc14.c — ftx_crc14 against vectors dumped from WSJT-X v3.0.2
 * lib/crc14.cpp (tests/vectors/crc14_vectors.txt, see tools/vectors/). */
#include "ftx_crc14.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int hexval(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static size_t parse_hex(const char *s, uint8_t *out, size_t max)
{
  size_t n = 0;
  while (s[0] && s[1] && n < max) {
    int hi = hexval(s[0]), lo = hexval(s[1]);
    if (hi < 0 || lo < 0) break;
    out[n++] = (uint8_t)(hi * 16 + lo);
    s += 2;
  }
  return n;
}

static void unpack_bits(const uint8_t *bytes, uint8_t *bits, size_t n)
{
  for (size_t i = 0; i < n; i++)
    bits[i] = (bytes[i / 8] >> (7 - i % 8)) & 1u;
}

int main(int argc, char **argv)
{
  if (argc != 2) {
    fprintf(stderr, "usage: %s crc14_vectors.txt\n", argv[0]);
    return 2;
  }
  FILE *f = fopen(argv[1], "r");
  if (f == NULL) {
    perror(argv[1]);
    return 2;
  }
  int fails = 0, na = 0, nb = 0, nc = 0;
  char line[256];
  while (fgets(line, sizeof line, f) != NULL) {
    char kind[4], hex[64];
    int len, want;
    uint8_t bytes[32];
    if (line[0] == '#' || line[0] == '\n') continue;
    if (sscanf(line, "%3s", kind) != 1) continue;
    if (strcmp(kind, "A") == 0) {
      if (sscanf(line, "A %63s %d", hex, &want) != 2) { fails++; continue; }
      size_t n = parse_hex(hex, bytes, sizeof bytes);
      if (n != 12) { fails++; continue; }
      na++;
      int got = ftx_crc14(bytes, 12);
      if (got != want) { fails++; printf("FAIL A %s: got %d want %d\n", hex, got, want); }
      /* Same through the 77-bit message helper... */
      uint8_t bits[96];
      unpack_bits(bytes, bits, 96);
      int got77 = ftx_crc14_message77(bits);
      if (got77 != want) { fails++; printf("FAIL A77 %s: got %d want %d\n", hex, got77, want); }
      /* ...and the 91-bit check must accept message+CRC and reject one flipped bit. */
      uint8_t bits91[91];
      memcpy(bits91, bits, 77);
      for (int i = 0; i < 14; i++) bits91[77 + i] = (want >> (13 - i)) & 1;
      if (!ftx_crc14_check91(bits91)) { fails++; printf("FAIL check91 accept %s\n", hex); }
      bits91[na % 91] ^= 1;
      if (ftx_crc14_check91(bits91)) { fails++; printf("FAIL check91 reject %s\n", hex); }
    } else if (strcmp(kind, "B") == 0) {
      if (sscanf(line, "B %d %63s %d", &len, hex, &want) != 3) { fails++; continue; }
      size_t n = parse_hex(hex, bytes, sizeof bytes);
      if ((int)n != len) { fails++; continue; }
      nb++;
      int got = ftx_crc14(bytes, n);
      if (got != want) { fails++; printf("FAIL B %d %s: got %d want %d\n", len, hex, got, want); }
    } else if (strcmp(kind, "C") == 0) {
      if (sscanf(line, "C %63s %d", hex, &want) != 2) { fails++; continue; }
      size_t n = parse_hex(hex, bytes, sizeof bytes);
      if (n != 12) { fails++; continue; }
      nc++;
      int got = ftx_crc14_check(bytes, 12) ? 1 : 0;
      if (got != want) { fails++; printf("FAIL C %s: got %d want %d\n", hex, got, want); }
      bytes[11] ^= 0x01;   /* a corrupted stream must fail the check */
      if (ftx_crc14_check(bytes, 12)) { fails++; printf("FAIL C reject %s\n", hex); }
    }
  }
  fclose(f);
  printf("crc14: A=%d B=%d C=%d vectors, %d failures\n", na, nb, nc, fails);
  if (na == 0 || nb == 0 || nc == 0) return 1;
  return fails == 0 ? 0 : 1;
}
