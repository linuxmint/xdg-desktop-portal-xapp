gnome = import('gnome')
pkg = import('pkgconfig')

top_srcdir = meson.project_source_root()

# Desktop Portal D-Bus interfaces

xdg_desktop_portal_dep = dependency('xdg-desktop-portal', version: '>= 1.5')
desktop_portal_interfaces_dir = xdg_desktop_portal_dep.get_variable(
  pkgconfig: 'interfaces_dir'
)

desktop_portal_dbus_interfaces = [
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Inhibit.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Lockdown.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Request.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Session.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Settings.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Wallpaper.xml',
]
# This is a newer screenshot interface that has a version property
# ref https://github.com/flatpak/xdg-desktop-portal-gtk/commit/30ef5f8ed500c2a7f850ed2164d7ee201860a700
desktop_portal_dbus_interfaces += [
  top_srcdir / 'data' / 'org.freedesktop.impl.portal.Screenshot.xml',
]

built_sources = gnome.gdbus_codegen(
  'xdg-desktop-portal-dbus',
  sources: desktop_portal_dbus_interfaces,
  interface_prefix: 'org.freedesktop.impl.portal.',
  namespace: 'XdpImpl',
)

desktop_dbus_interfaces = files(
  top_srcdir / 'data' / 'org.freedesktop.ScreenSaver.xml',
  top_srcdir / 'data' / 'org.gnome.SessionManager.xml',
  top_srcdir / 'data' / 'org.cinnamon.Screenshot.xml',
  top_srcdir / 'data' / 'org.cinnamon.ScreenSaver.xml',
  top_srcdir / 'data' / 'org.gnome.ScreenSaver.xml',
  top_srcdir / 'data' / 'org.mate.ScreenSaver.xml'
)

built_sources += gnome.gdbus_codegen(
  'common-dbus',
  sources: desktop_dbus_interfaces
)

config_entries = {
    'GETTEXT_PACKAGE' : '"@0@"'.format(meson.project_name()),
    'LOCALEDIR': '"@0@"'.format(prefix / get_option('localedir')),
    'PACKAGE_STRING': '"xdg-desktop-portal-xapp @0@"'.format(meson.project_version())
}
config = configuration_data(config_entries)
built_sources += configure_file(output: 'config.h', configuration: config)

deps = [
  meson.get_compiler('c').find_library('m'),
  dependency('glib-2.0', version: '>= 2.44'),
  dependency('gio-unix-2.0'),
  dependency('gtk+-3.0', version: '>= 3.24.0'),
  xdg_desktop_portal_dep,
]

sources = built_sources + files(
  'inhibit.c',
  'lockdown.c',
  'request.c',
  'screenshot.c',
  'session.c',
  'settings.c',
  'utils.c',
  'wallpaper.c',
  'xdg-desktop-portal-xapp.c',
)

executable(
  'xdg-desktop-portal-xapp',
  sources,
  dependencies: deps,
  install: true,
  install_dir: libexecdir,
)
