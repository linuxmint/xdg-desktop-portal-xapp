/*
 * Copyright © 2016 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Matthias Clasen <mclasen@redhat.com>
 */

#pragma once

#include <gdk/gdk.h>

#define DESKTOP_PORTAL_OBJECT_PATH "/org/freedesktop/portal/desktop"

extern const gchar *mode;
#define CINNAMON_MODE ( g_strcmp0 (mode, "cinnamon") == 0 )
#define MATE_MODE ( g_strcmp0 (mode, "mate") == 0 )
#define XFCE_MODE ( g_strcmp0 (mode, "xfce") == 0 )
#define UNKNOWN_MODE ( g_strcmp0 (mode, "unknown") == 0 )

typedef enum {
  XDG_DESKTOP_PORTAL_ERROR_FAILED     = 0,
  XDG_DESKTOP_PORTAL_ERROR_INVALID_ARGUMENT,
  XDG_DESKTOP_PORTAL_ERROR_NOT_FOUND,
  XDG_DESKTOP_PORTAL_ERROR_EXISTS,
  XDG_DESKTOP_PORTAL_ERROR_NOT_ALLOWED,
  XDG_DESKTOP_PORTAL_ERROR_CANCELLED,
  XDG_DESKTOP_PORTAL_ERROR_WINDOW_DESTROYED
} XdgDesktopPortalErrorEnum;

#define XDG_DESKTOP_PORTAL_ERROR xdg_desktop_portal_error_quark ()

GQuark  xdg_desktop_portal_error_quark (void);

glong str_distance (const char *a,
                    const char *b);
