/* preferences.h — the preferences dialog bound to FtxSettings: Station,
 * Radio, Decoder, Logging, Display.  Every row maps to a parameter the
 * ported decoder or the app layer actually uses; a change updates the
 * struct and calls back so the window applies it live.
 *
 * Part of FTX for Linux.  Copyright (C) 2026 Richard Fakenberg, OK1BR.
 * GPL-3.0-or-later.  No warranty.
 */
#ifndef FTX_PREFERENCES_H
#define FTX_PREFERENCES_H

#include <adwaita.h>

#include "app/settings.h"

G_BEGIN_DECLS

typedef void (*FtxPrefsChanged)(gpointer user_data);

AdwDialog *ftx_preferences_new(FtxSettings *s, FtxPrefsChanged changed, gpointer user_data);

G_END_DECLS

#endif /* FTX_PREFERENCES_H */
