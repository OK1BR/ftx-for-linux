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

# The app layer (GLib allowed): TCI client, period assembly, recorder.
glib_dep = dependency('glib-2.0')
lws_dep  = dependency('libwebsockets')
app_src  = files('src/app/tci_client.c', 'src/app/period.c')
executable('ftx-record', ['src/ftx-record.c', app_src],
  include_directories : include_directories('src'),
  dependencies : [glib_dep, lws_dep, m_dep],
  install : true)

# The bit-exact gates against WSJT-X-generated vectors are development
# material kept outside the repository (dev/, ignored by git); the build
# picks them up when present so `meson test -C build` works on the dev box.
fs = import('fs')
if fs.is_dir('dev/tests')
  subdir('dev/tests')
endif
