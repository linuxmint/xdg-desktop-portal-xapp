i18n = import('i18n')

libexecdir_conf = configuration_data()
libexecdir_conf.set('libexecdir', prefix / libexecdir)

# Portals
portal_dir = datadir / 'xdg-desktop-portal' / 'portals'
install_data(
  [
    'xapp-cinnamon.portal',
    'xapp-mate.portal'
  ],
  install_dir: portal_dir)

foreach service_name : [
                         'org.freedesktop.impl.portal.desktop.xapp-cinnamon',
                         'org.freedesktop.impl.portal.desktop.xapp-mate'
                       ]
  # dbus service
  configure_file(
    input: service_name + '.service.in',
    output: service_name + '.service',
    configuration: libexecdir_conf,
    install_dir: dbus_service_dir,
  )
endforeach

foreach filename : [
                     'xdg-desktop-portal-xapp-cinnamon',
                     'xdg-desktop-portal-xapp-mate'
                   ]
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
endforeach