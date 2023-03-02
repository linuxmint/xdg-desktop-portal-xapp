gnome = import('gnome')
pkg = import('pkgconfig')

top_srcdir = meson.source_root()

# **************************
# ****** Common stuff ******
# **************************

# Desktop Portal D-Bus interfaces

xdg_desktop_portal_dep = dependency('xdg-desktop-portal', version: '>= 1.5')
desktop_portal_interfaces_dir = xdg_desktop_portal_dep.get_pkgconfig_variable('interfaces_dir')

common_desktop_portal_dbus_interfaces = [
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Inhibit.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Lockdown.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Request.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Screenshot.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Session.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Settings.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Wallpaper.xml',
]

common_built_sources = gnome.gdbus_codegen(
  'xdg-desktop-portal-dbus',
  sources: common_desktop_portal_dbus_interfaces,
  interface_prefix: 'org.freedesktop.impl.portal.',
  namespace: 'XdpImpl',
)

common_dbus_interfaces = files(
  top_srcdir / 'data' / 'org.freedesktop.ScreenSaver.xml',
  top_srcdir / 'data' / 'org.gnome.SessionManager.xml',
  top_srcdir / 'data' / 'org.cinnamon.Screenshot.xml',
)

common_built_sources += gnome.gdbus_codegen(
  'common-dbus',
  sources: common_dbus_interfaces
)

common_deps = [
  meson.get_compiler('c').find_library('m'),
  dependency('glib-2.0', version: '>= 2.44'),
  dependency('gio-unix-2.0'),
  dependency('gtk+-3.0', version: '>= 3.24.0'),
  dependency('cinnamon-desktop', version: '>= 5.3'),
  xdg_desktop_portal_dep,
]

common_config = {
    'GETTEXT_PACKAGE' : '"@0@"'.format(meson.project_name()),
    'LOCALEDIR': '"@0@"'.format(prefix / get_option('localedir'))
}

# **************************
# ****** Cinnamon **********
# **************************

# Cinnamon Screensaver/Session/Shot D-Bus interfaces

cinnamon_built_sources = common_built_sources + gnome.gdbus_codegen(
  'cinnamon-screensaver-dbus',
  sources: files(top_srcdir / 'data' / 'org.cinnamon.ScreenSaver.xml'),
  interface_prefix: 'org.cinnamon.',
  namespace: 'Desktop'
)

cinnamon_config = configuration_data(common_config)

cinnamon_config.set10('CINNAMON_PORTAL', 1)
cinnamon_config.set_quoted('PACKAGE_STRING', 'xdg-desktop-portal-xapp-cinnamon @0@'.format(meson.project_version()))
cinnamon_config.set_quoted('SCREENSAVER_H', 'cinnamon-screensaver-dbus.h')
cinnamon_config.set_quoted('DESKTOP_INTERFACE_SCHEMA', 'org.cinnamon.desktop.interface')
cinnamon_config.set_quoted('LOCKDOWN_SCHEMA', 'org.cinnamon.desktop.lockdown')
cinnamon_config.set_quoted('PRIVACY_SCHEMA', 'org.cinnamon.desktop.privacy')
cinnamon_config.set_quoted('BACKGROUND_SCHEMA', 'org.cinnamon.desktop.background')

cinnamon_built_sources += configure_file(output: 'config-cinnamon.h', configuration: cinnamon_config)

cinnamon_deps = [
  declare_dependency(sources: cinnamon_built_sources),
  dependency('cinnamon-desktop', version: '>= 5.3')
] + common_deps

cinnamon_sources = cinnamon_built_sources + files(
  'inhibit.c',
  'lockdown.c',
  'request.c',
  'screenshot.c',
  'session.c',
  'settings.c',
  'utils.c',
  'wallpaper.c',
  'xdg-desktop-portal-xapp-cinnamon.c',
)

executable(
  'xdg-desktop-portal-xapp-cinnamon',
  cinnamon_sources,
  dependencies: cinnamon_deps,
  install: true,
  install_dir: libexecdir,
  c_args: [
    '-DCONFIG_H="config-cinnamon.h"'
  ],
)

# **************************
# ****** MATE **********
# **************************

# MATE Screensaver/Session D-Bus interfaces

mate_built_sources = common_built_sources + gnome.gdbus_codegen(
  'mate-screensaver-dbus',
  sources: files(top_srcdir / 'data' / 'org.mate.ScreenSaver.xml'),
  interface_prefix: 'org.mate.',
  namespace: 'Desktop'
)

mate_config = configuration_data(common_config)
mate_config.set10('MATE_PORTAL', 1)
mate_config.set_quoted('PACKAGE_STRING', 'xdg-desktop-portal-xapp-mate @0@'.format(meson.project_version()))
mate_config.set_quoted('SCREENSAVER_H', 'mate-screensaver-dbus.h')
mate_config.set_quoted('DESKTOP_INTERFACE_SCHEMA', 'org.mate.interface')
mate_config.set_quoted('LOCKDOWN_SCHEMA', 'org.mate.lockdown')
mate_config.set_quoted('PRIVACY_SCHEMA', 'org.mate.privacy')
mate_config.set_quoted('BACKGROUND_SCHEMA', 'org.mate.background')
mate_built_sources += configure_file(output: 'config-mate.h', configuration: mate_config)

mate_deps = [
  declare_dependency(sources: mate_built_sources),
  dependency('mate-desktop-2.0', version: '>= 1.26')
] + common_deps

mate_sources = mate_built_sources + files(
  'inhibit.c',
  'lockdown.c',
  'request.c',
  'session.c',
  'settings.c',
  'utils.c',
  'wallpaper.c',
  'xdg-desktop-portal-xapp-mate.c',
)

executable(
  'xdg-desktop-portal-xapp-mate',
  mate_sources,
  dependencies: mate_deps,
  install: true,
  install_dir: libexecdir,
  c_args: [
    '-DCONFIG_H="config-mate.h"'
  ],
)


