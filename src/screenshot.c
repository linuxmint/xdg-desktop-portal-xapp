#define _GNU_SOURCE 1

#include "config.h"

#include <errno.h>
#include <locale.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <gtk/gtk.h>

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gunixfdlist.h>

#include "cinnamon-dbus.h"

#include "screenshot.h"
#include "request.h"
#include "utils.h"

static OrgCinnamonScreenshot *cinnamon;

typedef struct {
  XdpImplScreenshot *impl;
  GDBusMethodInvocation *invocation;
  Request *request;

  int response;
  char *uri;
  const char *retval;
} ScreenshotHandle;

static void
screenshot_handle_free (gpointer data)
{
  ScreenshotHandle *handle = data;

  g_clear_object (&handle->request);
  g_free (handle->uri);

  g_free (handle);
}

static void
send_response (ScreenshotHandle *handle)
{
  if (handle->request->exported)
    request_unexport (handle->request);

  if (strcmp (handle->retval, "url") == 0)
    {
      GVariantBuilder opt_builder;

      g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
      g_variant_builder_add (&opt_builder, "{sv}", "uri", g_variant_new_string (handle->uri ? handle->uri : ""));

      xdp_impl_screenshot_complete_screenshot (handle->impl,
                                               handle->invocation,
                                               handle->response,
                                               g_variant_builder_end (&opt_builder));
    }

  screenshot_handle_free (handle);
}

static void
screenshot_done (GObject *source,
                 GAsyncResult *result,
                 gpointer data)
{
  ScreenshotHandle *handle = data;
  gboolean success;
  g_autofree char *filename = NULL;
  g_autoptr(GError) error = NULL;

  if (!org_cinnamon_screenshot_call_screenshot_finish (cinnamon,
                                                       &success,
                                                       &filename,
                                                       result,
                                                       &error))
    {
      g_print ("Failed to get screenshot: %s\n", error->message);
      handle->response = 1;
      return;
    }

  handle->uri = g_filename_to_uri (filename, NULL, NULL);
  handle->response = 0;

  send_response (handle);
}

static gboolean
handle_close (XdpImplRequest *object,
              GDBusMethodInvocation *invocation,
              ScreenshotHandle *handle)
{
  GVariantBuilder opt_builder;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  xdp_impl_screenshot_complete_screenshot (handle->impl,
                                           handle->invocation,
                                           2,
                                           g_variant_builder_end (&opt_builder));
  return FALSE;
}

static gboolean
handle_screenshot (XdpImplScreenshot *object,
                   GDBusMethodInvocation *invocation,
                   const char *arg_handle,
                   const char *arg_app_id,
                   const char *arg_parent_window,
                   GVariant *arg_options)
{
  g_autoptr(Request) request = NULL;
  ScreenshotHandle *handle;
  const char *sender;

  sender = g_dbus_method_invocation_get_sender (invocation);

  g_debug ("Got new Screenshot request from '%s'", sender);

  request = request_new (sender, arg_app_id, arg_handle);

  handle = g_new0 (ScreenshotHandle, 1);
  handle->impl = object;
  handle->invocation = invocation;
  handle->request = g_object_ref (request);
  handle->retval = "url";

  g_signal_connect (request, "handle-close", G_CALLBACK (handle_close), handle);

  request_export (request, g_dbus_method_invocation_get_connection (invocation));

  org_cinnamon_screenshot_call_screenshot (cinnamon,
                                           FALSE,
                                           TRUE,
                                           "Screenshot",
                                           NULL,
                                           screenshot_done,
                                           handle);

  return TRUE;
}

gboolean
screenshot_init (GDBusConnection *bus,
                 GError **error)
{
  GDBusInterfaceSkeleton *helper;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_screenshot_skeleton_new ());

  // TODO: Need to implement dialog (or maybe interact with screenshot app).
  g_signal_connect (helper, "handle-screenshot", G_CALLBACK (handle_screenshot), NULL);

  // TODO: Need to implement in Cinnamon
  // g_signal_connect (helper, "handle-pick-color", G_CALLBACK (handle_pick_color), NULL);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         error))
    return FALSE;

  cinnamon = org_cinnamon_screenshot_proxy_new_sync (bus,
                                                     G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                     "org.cinnamon.Screenshot",
                                                     "/org/cinnamon/Screenshot",
                                                     NULL,
                                                     error);
  if (cinnamon == NULL)
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
