#define _GNU_SOURCE 1

#include <config.h>

#include <errno.h>
#include <glib/gstdio.h>
#include <locale.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <glib/gi18n.h>

#include "xdg-desktop-portal-dbus.h"

#include "externalwindow.h"
#include "wallpaper.h"
#include "wallpaperdialog.h"
#include "request.h"
#include "utils.h"

#define MATE_ZOOMED_WALLPAPER_ENUM 1
#define CINNAMON_ZOOMED_WALLPAPER_ENUM 5

typedef struct {
    XdpImplWallpaper *impl;
    GDBusMethodInvocation *invocation;
    Request *request;
    GCancellable *cancellable;
    ExternalWindow *parent;
    GtkWidget *dialog;
    GBytes *image;
    gboolean show_preview;
    guint response;
} WallpaperDialogHandle;

static void
wallpaper_dialog_handle_free (gpointer data)
{
    WallpaperDialogHandle *handle = data;

    g_clear_pointer (&handle->dialog, gtk_widget_destroy);
    g_clear_object (&handle->parent);
    g_clear_object (&handle->cancellable);
    g_clear_pointer (&handle->image, g_bytes_unref);
    g_clear_object (&handle->request);

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
send_response_idle (gpointer data)
{
    send_response (data);

    return G_SOURCE_REMOVE;
}

static gboolean
set_gsettings (const gchar *uri)
{
    g_autoptr(GSettings) settings = NULL;
    g_autoptr(GFile) file = NULL;

    file = g_file_new_for_uri (uri);
    if (!g_file_is_native (file))
    {
        g_warning ("Only native files can be used for wallpaper.");
        return FALSE;
    }

    if (CINNAMON_MODE)
    {
        settings = g_settings_new ("org.cinnamon.desktop.background");

        return (g_settings_set_string (settings, "picture-uri", uri) &&
                g_settings_set_enum (settings, "picture-options", CINNAMON_ZOOMED_WALLPAPER_ENUM));
    }
    else
    if (MATE_MODE)
    {
        settings = g_settings_new ("org.mate.background");

        return (g_settings_set_string (settings, "picture-filename", g_file_peek_path (file)) &&
                g_settings_set_enum (settings, "picture-options", MATE_ZOOMED_WALLPAPER_ENUM));
    }
    else
    if (XFCE_MODE)
    {
        const gchar * const args[] = {
            "xfce4-set-wallpaper",
            g_file_peek_path (file),
            NULL
        };

        GError *error = NULL;

        if (!g_spawn_async (NULL, (gchar **) args, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error))
        {
            g_warning ("Can't set XFCE4 wallpaper: %s", error ? error->message : "reason unknown");
            g_clear_error (&error);

            return FALSE;
        }

        return TRUE;
    }

    return FALSE;
}

// The stored copy comes from the same bytes the preview showed, so the app
// can't swap the file after the user has accepted it.
static guint
store_wallpaper (WallpaperDialogHandle *handle)
{
    g_autofree gchar *path = g_build_filename (g_get_user_config_dir (), "background", NULL);
    g_autofree gchar *uri = NULL;
    g_autoptr(GError) error = NULL;

    if (!g_file_set_contents (path,
                              g_bytes_get_data (handle->image, NULL),
                              g_bytes_get_size (handle->image),
                              &error))
    {
        g_warning ("Failed to store wallpaper as '%s': %s", path, error->message);
        return 2;
    }

    uri = g_filename_to_uri (path, NULL, NULL);

    return set_gsettings (uri) ? 0 : 2;
}

static void
on_dialog_response (GtkDialog *dialog,
                    gint response,
                    WallpaperDialogHandle *handle)
{
    switch (response)
    {
        case GTK_RESPONSE_APPLY:
            handle->response = store_wallpaper (handle);
            break;
        case GTK_RESPONSE_CANCEL:
        case GTK_RESPONSE_DELETE_EVENT:
            handle->response = 1;
            break;
        default:
            g_warning ("Unexpected response: %d", response);
            handle->response = 2;
            break;
    }

    send_response (handle);
}

static void
on_dialog_ready (GObject *source_object,
                 GAsyncResult *result,
                 gpointer data)
{
    WallpaperDialogHandle *handle = data;
    g_autoptr(GError) error = NULL;

    handle->dialog = wallpaper_dialog_new_finish (result, &error);

    if (handle->dialog == NULL)
    {
        if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            g_warning ("Failed to load wallpaper preview: %s", error->message);

        handle->response = 2;
        send_response (handle);
        return;
    }

    g_signal_connect (handle->dialog, "response", G_CALLBACK (on_dialog_response), handle);
    gtk_widget_realize (handle->dialog);

    if (handle->parent != NULL)
        external_window_set_parent_of (handle->parent, gtk_widget_get_window (handle->dialog));

    gtk_window_present (GTK_WINDOW (handle->dialog));
}

static void
on_image_loaded (GObject *source_object,
                 GAsyncResult *result,
                 gpointer data)
{
    WallpaperDialogHandle *handle = data;
    g_autoptr(GError) error = NULL;
    GdkDisplay *display;
    gchar *contents = NULL;
    gsize length = 0;

    if (!g_file_load_contents_finish (G_FILE (source_object), result, &contents, &length, NULL, &error))
    {
        if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
            g_autofree gchar *uri = g_file_get_uri (G_FILE (source_object));
            g_warning ("Failed to load '%s': %s", uri, error->message);
        }

        handle->response = 2;
        send_response (handle);
        return;
    }

    handle->image = g_bytes_new_take (contents, length);

    if (!handle->show_preview)
    {
        handle->response = store_wallpaper (handle);
        send_response (handle);
        return;
    }

    display = handle->parent != NULL ? external_window_get_display (handle->parent)
                                     : gdk_display_get_default ();

    if (display == NULL)
    {
        g_warning ("No display available for the wallpaper preview");

        handle->response = 2;
        send_response (handle);
        return;
    }

    wallpaper_dialog_new_async (handle->image, display, handle->cancellable, on_dialog_ready, handle);
}

static gboolean
handle_close (XdpImplRequest *object,
              GDBusMethodInvocation *invocation,
              WallpaperDialogHandle *handle)
{
    g_cancellable_cancel (handle->cancellable);

    // Loading and decoding respond from their own callbacks once cancelled. The
    // request is still mid-emission here, so the dialog case can't free it yet.
    if (handle->dialog != NULL)
    {
        g_clear_pointer (&handle->dialog, gtk_widget_destroy);
        handle->response = 2;
        g_idle_add (send_response_idle, handle);
    }

    return FALSE;
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
    g_autoptr(GFile) file = NULL;
    WallpaperDialogHandle *handle;
    const char *sender;

    g_debug ("Got new SetWallpaperUri request");

    sender = g_dbus_method_invocation_get_sender (invocation);
    request = request_new (sender, arg_app_id, arg_handle);

    handle = g_new0 (WallpaperDialogHandle, 1);
    handle->impl = object;
    handle->invocation = invocation;
    handle->request = g_object_ref (request);
    handle->cancellable = g_cancellable_new ();

    g_variant_lookup (arg_options, "show-preview", "b", &handle->show_preview);

    if (handle->show_preview)
        handle->parent = create_external_window_from_handle (arg_parent_window);

    g_signal_connect (request, "handle-close", G_CALLBACK (handle_close), handle);
    request_export (request, g_dbus_method_invocation_get_connection (invocation));

    file = g_file_new_for_uri (arg_uri);
    g_file_load_contents_async (file, handle->cancellable, on_image_loaded, handle);

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
