project('ftx-for-linux', 'c',
  version : '0.0.1',
  license : 'GPL-3.0-or-later',
  default_options : [
    'c_std=c99',
    'warning_level=2',
    'buildtype=debugoptimized',
  ],
)

add_project_arguments('-DFTX_VERSION="@0@"'.format(meson.project_version()),
                      language : 'c')

cc         = meson.get_compiler('c')
m_dep      = cc.find_library('m', required : false)
# The decoder core may depend on FFTW3 (single + double precision) and libm
# only — no GLib.  Anything GLib/GTK lives above src/ftx/.
fftw3f_dep = dependency('fftw3f')
fftw3_dep  = dependency('fftw3')

subdir('src/ftx')   # libftx: GLib-free FT8/FT4 decoder core (C99 + FFTW3)

executable('ftx-decode', 'src/ftx-decode.c',
  dependencies : [ftx_dep, fftw3f_dep, m_dep],
  install : true)

# The app layer (GLib allowed, no GTK): TCI client, period assembly, the live
# RX engine, standard messages, settings — shared by the CLI tools and the GUI.
glib_dep = dependency('glib-2.0')
lws_dep  = dependency('libwebsockets')
src_inc  = include_directories('src')
app_src  = files('src/app/tci_client.c', 'src/app/period.c')
rx_src   = files('src/app/rx_engine.c', 'src/app/messages.c', 'src/app/settings.c')
executable('ftx-record', ['src/ftx-record.c', app_src],
  include_directories : src_inc,
  dependencies : [glib_dep, lws_dep, m_dep],
  install : true)
executable('ftx-rx', ['src/ftx-rx.c', app_src, rx_src],
  include_directories : src_inc,
  dependencies : [ftx_dep, fftw3f_dep, glib_dep, lws_dep, m_dep],
  install : true)

# The GUI (GTK4 >= 4.12 for gtk_css_provider_load_from_string and
# gtk_list_box_remove_all, libadwaita >= 1.5 for AdwDialog-based preferences
# and About dialogs).
gnome   = import('gnome')
gtk_dep = dependency('gtk4', version : '>= 4.12')
adw_dep = dependency('libadwaita-1', version : '>= 1.5')
ftx_res = gnome.compile_resources('ftx-resources', 'data/ftx-for-linux.gresource.xml',
  source_dir : 'data', c_name : 'ftx')
executable('ftx-for-linux',
  [ftx_res, 'src/ui/main.c', 'src/ui/window.c', 'src/ui/waterfall.c', 'src/ui/decode_list.c',
   'src/ui/qso_panel.c', 'src/ui/status_bar.c', 'src/ui/preferences.c', app_src, rx_src],
  include_directories : [src_inc, include_directories('src/ui')],
  dependencies : [ftx_dep, fftw3f_dep, glib_dep, gtk_dep, adw_dep, lws_dep, m_dep],
  install : true)

subdir('data')   # .desktop + icons + AppStream metainfo (install only)

# The bit-exact gates against WSJT-X-generated vectors are development
# material kept outside the repository (dev/, ignored by git); the build
# picks them up when present so `meson test -C build` works on the dev box.
fs = import('fs')
if fs.is_dir('dev/tests')
  subdir('dev/tests')
endif
