# libftx — the GLib-free decoder core.  Allowed dependencies: libm, FFTW3.
ftx_inc = include_directories('.')
ftx_src = files(
  'ftx_hash.c',
  'ftx_crc14.c',
  'ftx_pack77.c',
  'ftx_ldpc_tables.c',
  'ftx_ldpc.c',
  'ftx_fft.c',
  'ftx_ft8_sync.c',
  'ftx_ft8_demod.c',
  'ftx_ft8_a7a8.c',
  'ftx_ft8_decode.c',
  'ftx_ft4.c',
)
libftx = static_library('ftx', ftx_src,
  include_directories : ftx_inc,
  dependencies : [fftw3f_dep, fftw3_dep, m_dep],
)
ftx_dep = declare_dependency(link_with : libftx, include_directories : ftx_inc)
