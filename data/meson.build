libexecdir_conf = configuration_data()
libexecdir_conf.set('libexecdir', prefix / libexecdir)

# Portal
install_data(
  [ 'xapp.portal' ],
  install_dir: datadir / 'xdg-desktop-portal' / 'portals'
)

# dbus service
service_name = 'org.freedesktop.impl.portal.desktop.xapp'
configure_file(
  input: service_name + '.service.in',
  output: service_name + '.service',
  configuration: libexecdir_conf,
  install_dir: dbus_service_dir,
)

filename = 'xdg-desktop-portal-xapp'
# systemd unit
configure_file(
  input: filename + '.service.in',
  output: filename + '.service',
  configuration: libexecdir_conf,
  install_dir: systemduserunitdir,
)

# Desktop file
configure_file(
  input: filename + '.desktop.in',
  output: filename + '.desktop',
  configuration: libexecdir_conf,
  install: true,
  install_dir: datadir / 'applications',
)
