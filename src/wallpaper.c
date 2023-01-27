#define _GNU_SOURCE 1

#include "config.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <locale.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <gtk/gtk.h>

#include <glib/gi18n.h>
#include <gio/gdesktopappinfo.h>
#include <libcinnamon-desktop/cdesktop-enums.h>

#include "xdg-desktop-portal-dbus.h"

#include "wallpaper.h"
#include "request.h"
#include "utils.h"

#define BACKGROUND_SCHEMA "org.cinnamon.desktop.background"

typedef struct {
  XdpImplWallpaper *impl;
  GDBusMethodInvocation *invocation;
  Request *request;
  guint response;
  gchar *picture_uri;
} WallpaperDialogHandle;

static void
wallpaper_dialog_handle_free (gpointer data)
{
  WallpaperDialogHandle *handle = data;

  g_clear_object (&handle->request);
  g_clear_pointer (&handle->picture_uri, g_free);

  g_free (handle);
}

static void
send_response (WallpaperDialogHandle *handle)
{
  if (handle->request->exported)
    request_unexport (handle->request);

  xdp_impl_wallpaper_complete_set_wallpaper_uri (handle->impl,
                                                 handle->invocation,
                                                 handle->response);

  wallpaper_dialog_handle_free (handle);
}

static gboolean
set_gsettings (gchar *schema,
               gchar *uri)
{
  g_autoptr(GSettings) settings = NULL;

  settings = g_settings_new (schema);

  return (g_settings_set_string (settings, "picture-uri", uri) &&
          g_settings_set_enum (settings, "picture-options", C_DESKTOP_BACKGROUND_STYLE_ZOOM));
}

static void
on_file_copy_cb (GObject *source_object,
                 GAsyncResult *result,
                 gpointer data)
{
  WallpaperDialogHandle *handle = data;
  g_autoptr(GFile) destination = NULL;
  GFile *picture_file = G_FILE (source_object);
  g_autoptr(GError) error = NULL;
  g_autofree gchar *source_uri = NULL;
  g_autofree gchar *dest_uri = NULL;
  gchar *contents = NULL;
  gsize length = 0;

  handle->response = 2;

  source_uri = g_file_get_uri (picture_file);
  if (!g_file_load_contents_finish (picture_file, result, &contents, &length, NULL, &error))
    {
      g_warning ("Failed to copy '%s': %s", source_uri, error->message);

      goto out;
    }

  gchar *path = g_build_filename (g_get_user_config_dir (), "background", NULL);

  destination = g_file_new_for_path (path);
  g_file_replace_contents (destination,
                           contents,
                           length,
                           NULL, FALSE,
                           G_FILE_CREATE_REPLACE_DESTINATION,
                           NULL, NULL,
                           &error);
  dest_uri = g_file_get_uri (destination);

  if (set_gsettings (BACKGROUND_SCHEMA, dest_uri))
    handle->response = 0;
  else
    handle->response = 1;

out:
  send_response (handle);
}

static void
set_wallpaper (WallpaperDialogHandle *handle,
               const gchar *uri)
{
  g_autoptr(GFile) source = NULL;

  source = g_file_new_for_uri (uri);
  g_file_load_contents_async (source,
                              NULL,
                              on_file_copy_cb,
                              handle);
}

static gboolean
handle_set_wallpaper_uri (XdpImplWallpaper *object,
                          GDBusMethodInvocation *invocation,
                          const char *arg_handle,
                          const char *arg_app_id,
                          const char *arg_parent_window,
                          const char *arg_uri,
                          GVariant *arg_options)
{
  g_autoptr(Request) request = NULL;
  WallpaperDialogHandle *handle;
  const char *sender;

  g_debug ("Got new SetWallpaperUri request");

  sender = g_dbus_method_invocation_get_sender (invocation);
  request = request_new (sender, arg_app_id, arg_handle);

  handle = g_new0 (WallpaperDialogHandle, 1);
  handle->impl = object;
  handle->invocation = invocation;
  handle->request = g_object_ref (request);

  set_wallpaper (handle, g_strdup (arg_uri));
  request_export (request, g_dbus_method_invocation_get_connection (invocation));

  return TRUE;
}

gboolean
wallpaper_init (GDBusConnection *bus,
                GError **error)
{
  GDBusInterfaceSkeleton *helper;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_wallpaper_skeleton_new ());

  g_signal_connect (helper, "handle-set-wallpaper-uri", G_CALLBACK (handle_set_wallpaper_uri), NULL);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
