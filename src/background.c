#define _GNU_SOURCE 1

#include <config.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <glib/gstdio.h>
#include <gio/gdesktopappinfo.h>

#include "xdg-desktop-portal-dbus.h"
#include "common-dbus.h"

#include "utils.h"

static OrgCinnamonPortalHandlers *portal_handlers;
static GVariant *app_state;

static gboolean
needs_quoting (const char *arg)
{
    while (*arg != 0)
      {
        char c = *arg;
        if (!g_ascii_isalnum (c) &&
            !(c == '-' || c == '/' || c == '~' ||
              c == ':' || c == '.' || c == '_' ||
              c == '=' || c == '@'))
          return TRUE;
        arg++;
      }
    return FALSE;
}

char *
flatpak_quote_argv (const char *argv[],
                    gssize      len)
{
    GString *res = g_string_new ("");
    int i;

    if (len == -1)
      len = g_strv_length ((char **) argv);

    for (i = 0; i < len; i++)
      {
        if (i != 0)
          g_string_append_c (res, ' ');
  
        if (needs_quoting (argv[i]))
          {
            g_autofree char *quoted = g_shell_quote (argv[i]);
            g_string_append (res, quoted);
          }
        else
          g_string_append (res, argv[i]);
      }

    return g_string_free (res, FALSE);
}

typedef enum {
    AUTOSTART_FLAGS_NONE        = 0,
    AUTOSTART_FLAGS_ACTIVATABLE = 1 << 0,
} AutostartFlags;

static gboolean
handle_enable_autostart (XdpImplBackground      *object,
                         GDBusMethodInvocation  *invocation,
                         const char             *app_id,
                         gboolean                enable,
                         const char * const     *commandline,
                         guint                   flags)
{
    g_autofree gchar *dir = NULL;
    g_autofree gchar *path = NULL;
    gchar *file = NULL;
    gboolean result = FALSE;

    g_debug ("background: handle EnableAutostart");

    file = g_strconcat (app_id, ".desktop", NULL);
    dir = g_build_filename (g_get_user_config_dir (), "autostart", NULL);
    path = g_build_filename (dir, file, NULL);
    g_free (file);


    if (enable)
    {
        GKeyFile *keyfile = NULL;
        GError *error = NULL;
        g_autofree gchar *exec = NULL;

        if (g_mkdir_with_parents (dir, 0755) != 0)
        {
            g_warning ("Failed to create dirs %s", dir);
            xdp_impl_background_complete_enable_autostart (object, invocation, FALSE);
            return TRUE;
        }

        exec = flatpak_quote_argv ((const char **) commandline, -1);

        keyfile = g_key_file_new ();

        g_key_file_set_string (keyfile,
                               G_KEY_FILE_DESKTOP_GROUP,
                               G_KEY_FILE_DESKTOP_KEY_TYPE,
                               "Application");
        g_key_file_set_string (keyfile,
                               G_KEY_FILE_DESKTOP_GROUP,
                               G_KEY_FILE_DESKTOP_KEY_NAME,
                               app_id);
        g_key_file_set_string (keyfile,
                               G_KEY_FILE_DESKTOP_GROUP,
                               G_KEY_FILE_DESKTOP_KEY_EXEC,
                               exec);
        if (flags & AUTOSTART_FLAGS_ACTIVATABLE)
        {
            g_key_file_set_boolean (keyfile,
                                    G_KEY_FILE_DESKTOP_GROUP,
                                    G_KEY_FILE_DESKTOP_KEY_DBUS_ACTIVATABLE,
                                    TRUE);
        }

        g_key_file_set_string (keyfile,
                               G_KEY_FILE_DESKTOP_GROUP,
                               "X-Flatpak",
                               app_id);

        if (!g_key_file_save_to_file (keyfile, path, &error))
        {
          g_warning ("Failed to save %s: %s", path, error->message);
          g_clear_error (&error);
        }
        else
        {
            g_debug ("Wrote autostart file %s", path);
            result = TRUE;
        }

        g_key_file_unref (keyfile);
    }
    else
    {
        g_unlink (path);
        g_debug ("Removed %s", path);
    }

    xdp_impl_background_complete_enable_autostart (object, invocation, result);
    return TRUE;
}

static GVariant *
get_app_state (void)
{
    g_autoptr(GVariant) apps = NULL;

    if (!org_cinnamon_portal_handlers_call_get_app_states_sync (portal_handlers, &apps, NULL, NULL))
      {
        return NULL;
      }

    if (apps)
      {
          return g_variant_ref (apps);
      }

    return NULL;
}

static gboolean
handle_get_app_state (XdpImplBackground *object,
                      GDBusMethodInvocation *invocation)
{
    g_debug ("background: handle GetAppState");

    if (!CINNAMON_MODE)
    {
        g_dbus_method_invocation_return_error (invocation,
                                               XDG_DESKTOP_PORTAL_ERROR,
                                               XDG_DESKTOP_PORTAL_ERROR_FAILED,
                                               "GetAppState currently only supported in Cinnamon");
        return TRUE;
    }

    if (app_state == NULL)
        app_state = get_app_state ();

    if (app_state == NULL)
    {
        g_dbus_method_invocation_return_error (invocation,
                                               XDG_DESKTOP_PORTAL_ERROR,
                                               XDG_DESKTOP_PORTAL_ERROR_FAILED,
                                               "Could not get window list");
    }
    else
    {
        xdp_impl_background_complete_get_app_state (object, invocation, app_state);
    }

    return TRUE;
}

#define NOTIFY_BACKGROUND_ALLOW 1

static gboolean
handle_notify_background (XdpImplBackground     *object,
                          GDBusMethodInvocation *invocation,
                          const char            *handle,
                          const char            *app_id,
                          const char            *name) 
{
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&builder, "{sv}", "result", g_variant_new_uint32 (NOTIFY_BACKGROUND_ALLOW));
    xdp_impl_background_complete_notify_background (object, invocation, 0, g_variant_builder_end (&builder));
    return TRUE;
}

static void
on_cinnamon_signal (OrgCinnamonPortalHandlers *proxy,
                    const gchar               *sender_name,
                    const gchar               *signal_name,
                    GVariant                  *parameters,
                    gpointer                   user_data)
{
    if (g_strcmp0 (signal_name, "RunningAppsChanged") == 0)
    {
        g_debug ("RunningAppsChanged signal received from Cinnamon");
        g_clear_pointer (&app_state, g_variant_unref);
        xdp_impl_background_emit_running_applications_changed (XDP_IMPL_BACKGROUND (user_data));
    }
}

gboolean
background_init (GDBusConnection *bus,
                 GError **error)
{
    GDBusInterfaceSkeleton *helper;

    helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_background_skeleton_new ());

    /* See https://github.com/flatpak/xdg-desktop-portal/commit/458560c768d5acf7e163ba57f219725294f5ff51 */
    /* Starting with xdg-desktop-portal 1.19.0 this signal is no longer emitted. */
    g_signal_connect (helper, "handle-enable-autostart", G_CALLBACK (handle_enable_autostart), NULL);

    g_signal_connect (helper, "handle-get-app-state", G_CALLBACK (handle_get_app_state), NULL);
    g_signal_connect (helper, "handle-notify-background", G_CALLBACK (handle_notify_background), NULL);

    if (!g_dbus_interface_skeleton_export (helper,
                                           bus,
                                           DESKTOP_PORTAL_OBJECT_PATH,
                                           error))
        return FALSE;

    if (CINNAMON_MODE)
    {
        portal_handlers = org_cinnamon_portal_handlers_proxy_new_sync (bus,
                                                                       G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                                       "org.Cinnamon",
                                                                       "/org/Cinnamon",
                                                                       NULL,
                                                                       error);
        g_signal_connect (portal_handlers, "g-signal", G_CALLBACK (on_cinnamon_signal), helper);
    }

    g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

    return TRUE;
}
