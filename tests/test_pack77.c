/* test_pack77.c — replay WSJT-X v3.0.2 pack77/unpack77 oracle vectors
 * (tests/vectors/pack77/ .vec files, produced by tools/vectors/pack77_dump.f90)
 * through ftx_pack77 / ftx_unpack77 with one context per file. */
#include "ftx_pack77.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, shown;
static const int MAXSHOW = 25;

static void fail(const char *file, int lineno, const char *what, const char *line)
{
  fails++;
  if (shown < MAXSHOW) {
    shown++;
    printf("FAIL %s:%d %s\n   %s\n", file, lineno, what, line);
  }
}

static int split(char *s, char *f[], int max)
{
  int n = 0;
  f[n++] = s;
  for (char *p = s; *p && n < max; p++) {
    if (*p == '|') {
      *p = '\0';
      f[n++] = p + 1;
    }
  }
  return n;
}

static int run_file(const char *path, int *npack, int *nunpack)
{
  FILE *fp = fopen(path, "r");
  if (fp == NULL) {
    perror(path);
    return 1;
  }
  ftx_pack77_ctx *ctx = malloc(sizeof *ctx);
  ftx_pack77_init(ctx);
  int nrx = 1, lineno = 0;
  char c77[78];
  memset(c77, ' ', 77);                                /* the driver's c77 starts blank */
  c77[77] = '\0';
  char line[512], orig[512];
  while (fgets(line, sizeof line, fp) != NULL) {
    lineno++;
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
    strcpy(orig, line);
    if (line[0] == '#') {
      if (strncmp(line, "#MYCALL ", 8) == 0) ftx_pack77_set_mycall(ctx, line + 8);
      else if (strncmp(line, "#DXCALL ", 8) == 0) ftx_pack77_set_dxcall(ctx, line + 8);
      else if (strncmp(line, "#NRX ", 5) == 0) nrx = atoi(line + 5);
      continue;
    }
    char *f[8];
    if (strncmp(line, "P|", 2) == 0) {
      if (split(line + 2, f, 8) != 6) { fail(path, lineno, "bad P line", orig); continue; }
      /* driver: character*80 line, fmtmsg, msg = line(1:37), pack77, unpack77 */
      char m[81];
      snprintf(m, sizeof m, "%-80.80s", f[0]);
      ftx_fmtmsg(m);
      char msg37[38];
      snprintf(msg37, sizeof msg37, "%-37.37s", m);
      int i3 = -1, n3 = -1;
      ftx_pack77(ctx, msg37, &i3, &n3, c77);
      (*npack)++;
      char got[256];
      snprintf(got, sizeof got, "%d|%d", i3, n3);
      char want[64];
      snprintf(want, sizeof want, "%s|%s", f[1], f[2]);
      if (strcmp(got, want) != 0) {
        snprintf(got + strlen(got), sizeof got - strlen(got), " (want %s)", want);
        fail(path, lineno, got, orig);
      }
      if (strcmp(c77, f[3]) != 0) {
        snprintf(got, sizeof got, "c77 mismatch, got %s", c77);
        fail(path, lineno, got, orig);
      }
      char out[38];
      bool ok = ftx_unpack77(ctx, f[3], nrx, out);
      (*nunpack)++;
      if (strcmp(out, f[4]) != 0 || ok != (f[5][0] == 'T')) {
        snprintf(got, sizeof got, "unpack got \"%s\" %c", out, ok ? 'T' : 'F');
        fail(path, lineno, got, orig);
      }
    } else if (strncmp(line, "U|", 2) == 0) {
      if (split(line + 2, f, 8) != 3) { fail(path, lineno, "bad U line", orig); continue; }
      char out[38];
      char word[78];
      snprintf(word, sizeof word, "%-77.77s", f[0]);
      bool ok = ftx_unpack77(ctx, word, nrx, out);
      (*nunpack)++;
      if (strcmp(out, f[1]) != 0 || ok != (f[2][0] == 'T')) {
        char got[128];
        snprintf(got, sizeof got, "unpack got \"%s\" %c", out, ok ? 'T' : 'F');
        fail(path, lineno, got, orig);
      }
    }
  }
  fclose(fp);
  free(ctx);
  return 0;
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    fprintf(stderr, "usage: %s file.vec...\n", argv[0]);
    return 2;
  }
  int npack = 0, nunpack = 0;
  for (int i = 1; i < argc; i++)
    if (run_file(argv[i], &npack, &nunpack) != 0) return 2;
  printf("pack77: %d pack + %d unpack vectors in %d files, %d failures\n", npack, nunpack, argc - 1, fails);
  if (npack == 0 || nunpack == 0) return 1;
  return fails == 0 ? 0 : 1;
}
