# Desktop integration: .desktop launcher, scalable icon, AppStream metainfo
# (the sdr-for-linux template).  File names match the GApplication id so the
# compositor associates the window with its icon.

# Exec needs an absolute path: a ~/.local prefix's bindir is usually not on the
# graphical session's PATH, and a bare command name would not launch.
desktop_conf = configuration_data()
desktop_conf.set('bindir', get_option('prefix') / get_option('bindir'))
configure_file(
  input         : 'cz.ok1br.ftx_for_linux.desktop.in',
  output        : 'cz.ok1br.ftx_for_linux.desktop',
  configuration : desktop_conf,
  install_dir   : get_option('datadir') / 'applications',
)

install_data('cz.ok1br.ftx_for_linux.svg',
  install_dir : get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps')
install_data('cz.ok1br.ftx_for_linux-symbolic.svg',
  install_dir : get_option('datadir') / 'icons' / 'hicolor' / 'symbolic' / 'apps')
install_data('cz.ok1br.ftx_for_linux.metainfo.xml',
  install_dir : get_option('datadir') / 'metainfo')

# Country from a callsign prefix: AD1C's cty.dat (MIT, see cty.dat.LICENSE),
# the snapshot log-for-linux ships; src/app/cty.c locates it (a user copy in
# ~/.local/share/{ftx,log}-for-linux/ wins — refresh by replacing the file).
install_data('cty.dat', 'cty.dat.LICENSE',
  install_dir : get_option('datadir') / 'ftx-for-linux')

# Icon cache and desktop database at install time (skipped under DESTDIR, where
# the packager does it).  update-desktop-database is optional.
gnome.post_install(
  gtk_update_icon_cache   : true,
  update_desktop_database : find_program('update-desktop-database',
                                         required : false).found(),
)
