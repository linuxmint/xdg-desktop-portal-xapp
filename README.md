# xdg-desktop-portal-cinnamon

A backend implementation for [xdg-desktop-portal](http://github.com/flatpak/xdg-desktop-portal)
that is using GTK and various pieces of Cinnamon infrastructure, such as the
org.cinnamon.Screenshot or org.gnome.SessionManager D-Bus interfaces.

## Building xdg-desktop-portal-cinnamon

xdg-desktop-portal-cinnamon depends on xdg-desktop-portal and xdg-desktop-portal-gtk.

Implements:
- org.freedesktop.impl.portal.Inhibit
- org.freedesktop.impl.portal.Lockdown
- org.freedesktop.impl.portal.Screenshot
- org.freedesktop.impl.portal.Settings
- org.freedesktop.impl.portal.Wallpaper

Todo:
- org.freedesktop.impl.portal.Background
- ?