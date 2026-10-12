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
 *       Jonas Ådahl <jadahl@redhat.com>
 */

#include "config.h"

#include <string.h>

#include "externalwindow.h"
#ifdef HAVE_GTK_X11
#include "externalwindow-x11.h"
#endif
#ifdef HAVE_GTK_WAYLAND
#include "externalwindow-wayland.h"
#endif

#define X11_HANDLE_PREFIX "x11:"
#define WAYLAND_HANDLE_PREFIX "wayland:"

enum
{
    PROP_0,
    PROP_DISPLAY,
    N_PROPS
};

static GParamSpec *props[N_PROPS];

typedef struct
{
    GdkDisplay *display;
} ExternalWindowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (ExternalWindow, external_window, G_TYPE_OBJECT)

ExternalWindow *
create_external_window_from_handle (const char *handle_str)
{
    if (handle_str == NULL || handle_str[0] == '\0')
        return NULL;

#ifdef HAVE_GTK_X11
    if (g_str_has_prefix (handle_str, X11_HANDLE_PREFIX))
        return (ExternalWindow *) external_window_x11_new (handle_str + strlen (X11_HANDLE_PREFIX));
#endif
#ifdef HAVE_GTK_WAYLAND
    if (g_str_has_prefix (handle_str, WAYLAND_HANDLE_PREFIX))
        return (ExternalWindow *) external_window_wayland_new (handle_str + strlen (WAYLAND_HANDLE_PREFIX));
#endif

    g_warning ("Unhandled parent window type %s", handle_str);
    return NULL;
}

void
external_window_set_parent_of (ExternalWindow *external_window,
                               GdkWindow      *child_window)
{
    ExternalWindowPrivate *priv;

    g_return_if_fail (EXTERNAL_IS_WINDOW (external_window));
    g_return_if_fail (GDK_IS_WINDOW (child_window));

    priv = external_window_get_instance_private (external_window);

    if (gdk_window_get_display (child_window) != priv->display)
    {
        g_warning ("Portal window was not created on its parent's display, not setting a parent");
        return;
    }

    EXTERNAL_WINDOW_GET_CLASS (external_window)->set_parent_of (external_window, child_window);
}

GdkDisplay *
external_window_get_display (ExternalWindow *external_window)
{
    ExternalWindowPrivate *priv;

    g_return_val_if_fail (EXTERNAL_IS_WINDOW (external_window), NULL);

    priv = external_window_get_instance_private (external_window);

    return priv->display;
}

static void
external_window_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    ExternalWindowPrivate *priv = external_window_get_instance_private (EXTERNAL_WINDOW (object));

    switch (prop_id)
    {
        case PROP_DISPLAY:
            g_set_object (&priv->display, g_value_get_object (value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
external_window_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    ExternalWindowPrivate *priv = external_window_get_instance_private (EXTERNAL_WINDOW (object));

    switch (prop_id)
    {
        case PROP_DISPLAY:
            g_value_set_object (value, priv->display);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
external_window_dispose (GObject *object)
{
    ExternalWindowPrivate *priv = external_window_get_instance_private (EXTERNAL_WINDOW (object));

    g_clear_object (&priv->display);

    G_OBJECT_CLASS (external_window_parent_class)->dispose (object);
}

static void
external_window_init (ExternalWindow *external_window)
{
}

static void
external_window_class_init (ExternalWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = external_window_get_property;
    object_class->set_property = external_window_set_property;
    object_class->dispose = external_window_dispose;

    props[PROP_DISPLAY] =
        g_param_spec_object ("display",
                             "GdkDisplay",
                             "The display the external window lives on",
                             GDK_TYPE_DISPLAY,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, props);
}
