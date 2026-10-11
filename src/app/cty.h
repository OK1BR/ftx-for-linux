/* cty.h — country/continent from a callsign prefix (AD1C cty.dat).
 *
 * Loads the standard Amateur Radio Country File (country-files.com,
 * MIT-licensed, snapshot bundled in data/) and answers "which DXCC entity
 * and continent is this call?". That is what contest validity rules need
 * (WAE: EU works non-EU only; EUHFC: EU only; CQ WW: own country scores
 * zero) and what the entry row shows for a new call ("New call from
 * Czech Republic").
 *
 * Matching follows the cty.dat conventions: exact "=CALL" entries win over
 * prefixes, then the longest matching prefix. Portable calls take the
 * designator part ("DL/OK1BR" → DL, "OK1BR/P" → OK1BR); /MM and /AM have
 * no country by definition and miss. Alias zone/continent overrides
 * "(cq)[itu]{cont}" are applied; "<lat/long>" and "~tz~" are skipped.
 *
 * Copied from log-for-linux's src/engine/cty.[ch] (same author, same
 * licence) with the ftx_ prefix; the lookup order of ftx_cty_locate() is
 * the only change, so a fix in either tree ports by diff.
 *
 * Part of FTX for Linux.  Copyright (C) 2026 Richard Fakenberg, OK1BR.
 * GPL-3.0-or-later.  No warranty.
 */
#ifndef FTX_CTY_H
#define FTX_CTY_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct _FtxCty FtxCty;

/* One lookup answer. Strings are owned by the FtxCty — valid until free. */
typedef struct {
  const char *country;         /* entity name, e.g. "Czech Republic" */
  const char *prefix;          /* primary prefix, e.g. "OK" */
  char        continent[3];    /* "EU", "NA", … */
  int         cq_zone;
  int         itu_zone;
  gboolean    waedc_only;      /* '*' entity: on the WAE list, not DXCC */
} FtxCtyInfo;

/* Parse a cty.dat file. NULL + error when unreadable or no entity parses. */
FtxCty *ftx_cty_load (const char *path, GError **error);
void      ftx_cty_free (FtxCty *c);

/* Find the country file.  A user-refreshed snapshot wins and one on the
 * desk serves the family: $XDG_DATA_HOME/ftx-for-linux/cty.dat, then
 * $XDG_DATA_HOME/log-for-linux/cty.dat, then each system data dir (both
 * names), then data/cty.dat next to the build tree (resolved from the
 * executable, so a build-dir run works from any cwd).  NULL when none. */
char *ftx_cty_locate (void);

/* Entity for a callsign; FALSE when nothing matches (also /MM, /AM).
 * call may be any case and carry portable designators. */
gboolean ftx_cty_lookup (FtxCty *c, const char *call, FtxCtyInfo *out);

/* Snapshot version from the "=VERyyyymmdd" marker entry, e.g. "20260803";
 * NULL when the file carries none. Owned by the FtxCty. */
const char *ftx_cty_version (FtxCty *c);

guint ftx_cty_n_entities (FtxCty *c);

G_END_DECLS

#endif /* FTX_CTY_H */
