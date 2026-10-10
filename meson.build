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
subdir('tests')     # bit-exact unit gates: `meson test -C build`
