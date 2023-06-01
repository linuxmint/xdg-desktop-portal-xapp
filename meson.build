project(
  'xdg-desktop-portal-xapp',
  'c',
  version: '1.0.1',
  meson_version: '>= 0.56.0',
  license: 'LGPLv2.1+'
)

prefix = get_option('prefix')
datadir = get_option('datadir')
libdir = get_option('libdir')
libexecdir = get_option('libexecdir')

systemduserunitdir = get_option('systemduserunitdir')
if systemduserunitdir == ''
  systemd = dependency('systemd', version: '>= 242')
  systemduserunitdir = systemd.get_variable(
    pkgconfig: 'systemduserunitdir',
    pkgconfig_define: ['prefix', get_option('prefix')]
  )
endif

dbus_service_dir = get_option('dbus_service_dir')
if dbus_service_dir == ''
  dbus_service_dir = datadir / 'dbus-1' / 'services'
endif

subdir('data')
# subdir('po')
subdir('src')

summary({
  'prefix': prefix,
  'datadir': datadir,
  'libdir': libdir,
  'libexecdir': libexecdir,
  'dbus_service_dir': dbus_service_dir,
  'systemduserunitdir': systemduserunitdir,
})
