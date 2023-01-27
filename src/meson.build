gnome = import('gnome')
pkg = import('pkgconfig')

xdg_desktop_portal_dep = dependency('xdg-desktop-portal', version: '>= 1.5')

# Desktop Portal D-Bus interfaces
desktop_portal_interfaces_dir = xdg_desktop_portal_dep.get_pkgconfig_variable('interfaces_dir')
desktop_portal_dbus_interfaces = [
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Inhibit.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Lockdown.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Request.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Screenshot.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Session.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Settings.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Wallpaper.xml',
]

built_sources = gnome.gdbus_codegen(
  'xdg-desktop-portal-dbus',
  sources: desktop_portal_dbus_interfaces,
  interface_prefix: 'org.freedesktop.impl.portal.',
  namespace: 'XdpImpl',
)

# Cinnamon Screensaver/Session/Shot D-Bus interfaces
top_srcdir = meson.source_root()
misc_dbus_interfaces = files(
  top_srcdir / 'data' / 'org.freedesktop.ScreenSaver.xml',
  top_srcdir / 'data' / 'org.gnome.SessionManager.xml',
  top_srcdir / 'data' / 'org.Cinnamon.xml',
  top_srcdir / 'data' / 'org.cinnamon.ScreenSaver.xml',
  top_srcdir / 'data' / 'org.cinnamon.Screenshot.xml',
)

built_sources += gnome.gdbus_codegen(
  'cinnamon-dbus',
  sources: misc_dbus_interfaces,
  interface_prefix: 'org.freedesktop.impl.portal.',
)

# GResources
built_sources += gnome.compile_resources(
  'xdg-desktop-portal-cinnamon-resources',
  'xdg-desktop-portal-cinnamon.gresource.xml',
  c_name: '_xdg_desktop',
)

# config.h
config_h = configuration_data()
config_h.set_quoted('GETTEXT_PACKAGE', meson.project_name())
config_h.set_quoted('LOCALEDIR', prefix / get_option('localedir'))
config_h.set_quoted('PACKAGE_NAME', meson.project_name())
config_h.set_quoted('PACKAGE_STRING', '@0@ @1@'.format(meson.project_name(), meson.project_version()))
built_sources += configure_file(output: 'config.h', configuration: config_h)

deps = [
  declare_dependency(sources: built_sources),
  meson.get_compiler('c').find_library('m'),
  dependency('glib-2.0', version: '>= 2.44'),
  dependency('gio-unix-2.0'),
  dependency('gtk+-3.0', version: '>= 3.24.0'),
  dependency('cinnamon-desktop', version: '>= 5.3'),
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
  'xdg-desktop-portal-cinnamon.c',
)

executable(
  'xdg-desktop-portal-cinnamon',
  sources,
  dependencies: deps,
  install: true,
  install_dir: libexecdir,
)
