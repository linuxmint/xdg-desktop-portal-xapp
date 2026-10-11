/*
 * Copyright © 2026
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
 */


#include <math.h>

#include <gio/gio.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <json-glib/json-glib.h>

#include "xdg-desktop-portal-dbus.h"
#include "common-dbus.h"

#include "screencast.h"
#include "screencastdialog.h"
#include "muffinscreencast.h"
#include "request.h"
#include "session.h"
#include "utils.h"

#define CINNAMON_SCREEN_CAST_SCRIPT \
  "global.get_window_actors().map(a => a.meta_window)." \
  "filter(w => w && !w.is_skip_taskbar() && " \
  "w.get_window_type() !== 1 && w.get_window_type() !== 2)." \
  "map(w => [String(w.get_id()), w.get_title() || '', w.get_wm_class() || ''])"

#define ITEMS_PRESENT_AFTER 4

typedef enum _ScreenCastDisplayRotation
{
  SCREEN_CAST_DISPLAY_ROTATION_NONE,
  SCREEN_CAST_DISPLAY_ROTATION_90,
  SCREEN_CAST_DISPLAY_ROTATION_180,
  SCREEN_CAST_DISPLAY_ROTATION_270,
  SCREEN_CAST_DISPLAY_ROTATION_FLIPPED,
  SCREEN_CAST_DISPLAY_ROTATION_90_FLIPPED,
  SCREEN_CAST_DISPLAY_ROTATION_180_FLIPPED,
  SCREEN_CAST_DISPLAY_ROTATION_270_FLIPPED,
} ScreenCastDisplayRotation;

typedef enum _ScreenCastLayoutMode
{
  SCREEN_CAST_LAYOUT_MODE_LOGICAL = 1,
  SCREEN_CAST_LAYOUT_MODE_PHYSICAL = 2,
  SCREEN_CAST_LAYOUT_MODE_LOGICAL_WITH_UI_SCALE = 3,
} ScreenCastLayoutMode;

static MuffinScreenCast *muffin_screen_cast;
static OrgCinnamon *cinnamon;

static GVariant *
new_empty_results (void)
{
  return g_variant_new_array (G_VARIANT_TYPE ("{sv}"), NULL, 0);
}

static void
picker_item_free (gpointer data)
{
  PickerItem *item = data;

  g_free (item->title);
  g_free (item->subtitle);
  g_free (item->connector);
  g_free (item);
}

static PickerItem *
picker_item_new (ScreenCastSourceType type,
                 guint32 id)
{
  PickerItem *item;

  item = g_new0 (PickerItem, 1);
  item->type = type;
  item->id = id;

  return item;
}

static void
monitor_free (gpointer data)
{
  MuffinMonitor *monitor = data;

  g_free (monitor->connector);
  g_free (monitor->label);
  g_free (monitor);
}

static gboolean
monitor_is_rotated (ScreenCastDisplayRotation rotation)
{
  return rotation == SCREEN_CAST_DISPLAY_ROTATION_90 ||
         rotation == SCREEN_CAST_DISPLAY_ROTATION_270 ||
         rotation == SCREEN_CAST_DISPLAY_ROTATION_90_FLIPPED ||
         rotation == SCREEN_CAST_DISPLAY_ROTATION_270_FLIPPED;
}

static MuffinMonitor *
muffin_monitor_new (const char *connector,
                    const char *label,
                    gboolean primary,
                    const ScreenCastRect *rect,
                    double scale,
                    ScreenCastDisplayRotation rotation,
                    ScreenCastLayoutMode layout_mode,
                    int ui_scaling_factor)
{
  MuffinMonitor *monitor;

  monitor = g_new0 (MuffinMonitor, 1);
  monitor->connector = g_strdup (connector);
  monitor->label = g_strdup (label);
  monitor->primary = primary;
  monitor->rect = *rect;

  if (layout_mode == SCREEN_CAST_LAYOUT_MODE_LOGICAL_WITH_UI_SCALE)
    {
      monitor->rect.width = round (rect->width / (scale * ui_scaling_factor));
      monitor->rect.height = round (rect->height / (scale * ui_scaling_factor));
    }
  else if (layout_mode == SCREEN_CAST_LAYOUT_MODE_LOGICAL)
    {
      monitor->rect.width = round (rect->width / scale);
      monitor->rect.height = round (rect->height / scale);
    }

  if (monitor_is_rotated (rotation))
    {
      int tmp = monitor->rect.width;

      monitor->rect.width = monitor->rect.height;
      monitor->rect.height = tmp;
    }

  return monitor;
}

static void
read_current_width_and_height (GVariantIter *modes,
                               int *width,
                               int *height)
{
  while (TRUE)
    {
      g_autoptr(GVariant) variant = NULL;
      g_autoptr(GVariant) properties_variant = NULL;
      gboolean is_current;

      if (!g_variant_iter_next (modes, "@(siiddada{sv})", &variant))
        break;

      g_variant_get (variant, "(siiddad@a{sv})",
                     NULL, /* mode_id */
                     width,
                     height,
                     NULL, /* refresh_rate */
                     NULL, /* preferred_scale */
                     NULL, /* supported_scales */
                     &properties_variant);
      if (!g_variant_lookup (properties_variant, "is-current", "b", &is_current))
        is_current = FALSE;

      if (is_current)
        return;
    }

  *width = 0;
  *height = 0;
}

static GPtrArray *
get_monitors (GDBusConnection *connection)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) monitors = NULL;
  g_autoptr(GVariant) logical_monitors = NULL;
  g_autoptr(GVariant) properties = NULL;
  g_autoptr(OrgCinnamonMuffinDisplayConfig) proxy = NULL;
  g_autoptr(GPtrArray) result = NULL;
  GVariantIter monitors_iter;
  GVariantIter logical_iter;
  GHashTable *monitor_map;
  ScreenCastLayoutMode layout_mode = SCREEN_CAST_LAYOUT_MODE_LOGICAL;
  int ui_scaling_factor = 1;
  guint serial;

  proxy = org_cinnamon_muffin_display_config_proxy_new_sync (connection,
                                                             G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                             "org.cinnamon.Muffin.DisplayConfig",
                                                             "/org/cinnamon/Muffin/DisplayConfig",
                                                             NULL,
                                                             &error);
  if (!proxy)
    {
      g_warning ("Failed to acquire org.cinnamon.Muffin.DisplayConfig proxy: %s",
                 error->message);
      return NULL;
    }

  if (!org_cinnamon_muffin_display_config_call_get_current_state_sync (proxy,
                                                                       &serial,
                                                                       &monitors,
                                                                       &logical_monitors,
                                                                       &properties,
                                                                       NULL,
                                                                       &error))
    {
      g_warning ("Failed to get display state: %s", error->message);
      return NULL;
    }

  g_variant_lookup (properties, "layout-mode", "u", &layout_mode);

  if (!g_variant_lookup (properties, "legacy-ui-scaling-factor",
                         "i", &ui_scaling_factor) ||
      ui_scaling_factor < 1)
    ui_scaling_factor = 1;

  monitor_map = g_hash_table_new_full (g_str_hash, g_str_equal,
                                       g_free, monitor_free);

  g_variant_iter_init (&monitors_iter, monitors);
  while (TRUE)
    {
      g_autoptr(GVariant) monitor_variant = NULL;
      g_autofree char *connector = NULL;
      g_autofree char *vendor = NULL;
      g_autofree char *product = NULL;
      g_autofree char *serial_s = NULL;
      g_autoptr(GVariantIter) modes = NULL;
      g_autoptr(GVariant) monitor_properties = NULL;
      g_autofree char *display_name = NULL;
      MuffinMonitor *monitor;

      if (!g_variant_iter_next (&monitors_iter, "@((ssss)a(siiddada{sv})a{sv})",
                                &monitor_variant))
        break;

      g_variant_get (monitor_variant, "((ssss)a(siiddada{sv})@a{sv})",
                     &connector,
                     &vendor,
                     &product,
                     &serial_s,
                     &modes,
                     &monitor_properties);
      if (!g_variant_lookup (monitor_properties, "display-name", "s", &display_name))
        display_name = g_strdup (connector);

      monitor = g_new0 (MuffinMonitor, 1);
      monitor->connector = g_strdup (connector);
      monitor->label = g_strdup (display_name);
      read_current_width_and_height (modes,
                                     &monitor->rect.width,
                                     &monitor->rect.height);

      g_hash_table_insert (monitor_map, g_strdup (connector), monitor);
    }

  result = g_ptr_array_new_with_free_func (monitor_free);

  g_variant_iter_init (&logical_iter, logical_monitors);
  while (TRUE)
    {
      g_autoptr(GVariant) logical_variant = NULL;
      g_autoptr(GVariantIter) monitors_iter_inner = NULL;
      g_autoptr(GString) label_builder = NULL;
      MuffinMonitor *first_monitor = NULL;
      ScreenCastRect rect;
      int x, y;
      double scale;
      guint rotation;
      gboolean is_primary;

      if (!g_variant_iter_next (&logical_iter, "@(iiduba(ssss)a{sv})",
                                &logical_variant))
        break;

      g_variant_get (logical_variant, "(iiduba(ssss)a{sv})",
                     &x, &y, &scale, &rotation, &is_primary,
                     &monitors_iter_inner, NULL);

      label_builder = g_string_new (NULL);
      while (TRUE)
        {
          g_autoptr(GVariant) monitor_variant = NULL;
          g_autofree char *connector = NULL;
          MuffinMonitor *monitor;

          if (!g_variant_iter_next (monitors_iter_inner, "@(ssss)",
                                    &monitor_variant))
            break;

          g_variant_get (monitor_variant, "(ssss)",
                         &connector, NULL, NULL, NULL);

          monitor = g_hash_table_lookup (monitor_map, connector);
          if (!monitor)
            continue;

          if (!first_monitor)
            first_monitor = monitor;

          if (label_builder->len > 0)
            g_string_append_c (label_builder, ' ');
          g_string_append (label_builder, monitor->label);
        }

      if (!first_monitor)
        continue;

      rect.x = x;
      rect.y = y;
      rect.width = first_monitor->rect.width;
      rect.height = first_monitor->rect.height;

      g_ptr_array_add (result,
                       muffin_monitor_new (first_monitor->connector,
                                           label_builder->str,
                                           is_primary,
                                           &rect,
                                           scale,
                                           (ScreenCastDisplayRotation) rotation,
                                           layout_mode,
                                           ui_scaling_factor));
    }

  g_hash_table_destroy (monitor_map);

  return g_steal_pointer (&result);
}

static GPtrArray *
get_windows (GDBusConnection *connection)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(JsonParser) parser = NULL;
  JsonNode *root;
  JsonArray *array;
  g_autoptr(GPtrArray) result = NULL;
  gboolean success;
  g_autofree char *response = NULL;

  if (!cinnamon)
    return NULL;

  if (!org_cinnamon_call_eval_sync (cinnamon,
                                    CINNAMON_SCREEN_CAST_SCRIPT,
                                    &success,
                                    &response,
                                    NULL,
                                    &error))
    {
      g_warning ("Failed to query windows: %s", error->message);
      return NULL;
    }

  if (!success)
    {
      g_warning ("Cinnamon Eval failed to list windows");
      return NULL;
    }

  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser, response, -1, &error))
    {
      g_warning ("Failed to parse window list: %s", error->message);
      return NULL;
    }

  root = json_parser_get_root (parser);
  if (!root || !JSON_NODE_HOLDS_ARRAY (root))
    return NULL;

  array = json_node_get_array (root);
  result = g_ptr_array_new_with_free_func (NULL);

  for (guint i = 0; i < json_array_get_length (array); i++)
    {
      JsonNode *element = json_array_get_element (array, i);
      JsonArray *entry;
      PickerItem *item;

      if (!element || !JSON_NODE_HOLDS_ARRAY (element))
        continue;

      entry = json_node_get_array (element);
      if (json_array_get_length (entry) < 3)
        continue;

      item = picker_item_new (SCREEN_CAST_SOURCE_TYPE_WINDOW, i);
      item->window_id = (guint64) g_ascii_strtoull (json_array_get_string_element (entry, 0),
                                                    NULL, 10);
      item->title = g_strdup (json_array_get_string_element (entry, 1));
      item->subtitle = g_strdup (json_array_get_string_element (entry, 2));

      g_ptr_array_add (result, item);
    }

  return g_steal_pointer (&result);
}

typedef struct _ScreenCastSession
{
  Session parent;

  ScreenCastSelection selection;
  MuffinScreenCastSession *muffin_session;
  gboolean started;
  gboolean start_pending;
} ScreenCastSession;

typedef struct _ScreenCastSessionClass
{
  SessionClass parent_class;
} ScreenCastSessionClass;

static GType screencast_session_get_type (void);
G_DEFINE_TYPE (ScreenCastSession, screencast_session, session_get_type ())

static void
screencast_session_close (Session *session)
{
  ScreenCastSession *screencast_session = (ScreenCastSession *)session;
  GError *error = NULL;

  g_debug ("Closing screencast session");

  if (screencast_session->muffin_session)
    {
      muffin_screen_cast_session_stop (screencast_session->muffin_session,
                                       &error);
      if (error)
        {
          g_warning ("Failed to stop muffin screencast session: %s",
                     error->message);
          g_clear_error (&error);
        }

      g_clear_object (&screencast_session->muffin_session);
    }
}

static void
screencast_session_finalize (GObject *object)
{
  ScreenCastSession *screencast_session = (ScreenCastSession *)object;

  g_clear_object (&screencast_session->muffin_session);

  G_OBJECT_CLASS (screencast_session_parent_class)->finalize (object);
}

static void
screencast_session_init (ScreenCastSession *screencast_session)
{
  screencast_session->selection.source_types =
    SCREEN_CAST_SOURCE_TYPE_MONITOR | SCREEN_CAST_SOURCE_TYPE_WINDOW;
  screencast_session->selection.cursor_mode = SCREEN_CAST_CURSOR_MODE_HIDDEN;
}

static void
screencast_session_class_init (ScreenCastSessionClass *klass)
{
  GObjectClass *gobject_class;
  SessionClass *session_class;

  gobject_class = (GObjectClass *)klass;
  gobject_class->finalize = screencast_session_finalize;

  session_class = (SessionClass *)klass;
  session_class->close = screencast_session_close;
}

static ScreenCastSession *
screencast_session_new (const char *session_handle)
{
  ScreenCastSession *screencast_session;

  g_debug ("Creating screencast session %s", session_handle);

  screencast_session = g_object_new (screencast_session_get_type (),
                                     "id", session_handle,
                                     NULL);

  return screencast_session;
}

typedef struct _StartOperation
{
  ScreenCastSession *session;
  XdpImplScreenCast *impl;
  GDBusMethodInvocation *invocation;
  Request *request;
  ScreenCastDialog *dialog;
  GPtrArray *items;
  MuffinScreenCastSession *muffin_session;
  char *preview_dir;
  guint preview_idle_id;
  guint preview_index;
  guint defer_id;
  GArray *selected_indices;
  int busy_count;
  gboolean presented;
  gboolean completed;
  gboolean finished_pending;
} StartOperation;

static void
start_operation_free (StartOperation *op)
{
  if (op->preview_idle_id)
    {
      g_source_remove (op->preview_idle_id);
      op->preview_idle_id = 0;
    }

  if (op->defer_id)
    {
      g_source_remove (op->defer_id);
      op->defer_id = 0;
    }

  if (op->preview_dir)
    {
      g_rmdir (op->preview_dir);
      g_free (op->preview_dir);
    }

  if (op->muffin_session)
    g_signal_handlers_disconnect_by_data (op->muffin_session, op);

  g_clear_object (&op->muffin_session);
  g_clear_object (&op->request);

  if (op->dialog)
    {
      screencast_dialog_destroy (op->dialog);
      op->dialog = NULL;
    }

  if (op->selected_indices)
    {
      g_array_free (op->selected_indices, TRUE);
      op->selected_indices = NULL;
    }

  g_clear_pointer (&op->items, g_ptr_array_unref);
  g_clear_object (&op->session);

  g_free (op);
}

static void
start_operation_complete (StartOperation *op,
                          guint response,
                          GVariant *results)
{
  if (op->completed)
    {
      if (results)
        g_variant_unref (results);
      return;
    }

  op->completed = TRUE;
  op->session->start_pending = FALSE;

  if (op->request->exported)
    request_unexport (op->request);

  if (!results)
    results = new_empty_results ();

  xdp_impl_screen_cast_complete_start (op->impl,
                                       op->invocation,
                                       response,
                                       results);

  if (op->busy_count > 0)
    {
      op->finished_pending = TRUE;
      return;
    }

  start_operation_free (op);
}

static void
start_operation_busy_enter (StartOperation *op)
{
  op->busy_count++;
}

static gboolean
start_operation_busy_leave (StartOperation *op)
{
  op->busy_count--;

  if (op->busy_count > 0 || !op->finished_pending)
    return TRUE;

  start_operation_free (op);

  return FALSE;
}

static void
start_operation_fail (StartOperation *op,
                      const char *message)
{
  g_warning ("Screencast start failed: %s", message);
  start_operation_complete (op, 2, NULL);
}

static void
start_operation_cancel (StartOperation *op)
{
  start_operation_complete (op, 1, NULL);
}

static gboolean
close_session_idle (gpointer user_data)
{
  Session *session = user_data;

  if (!session->closed)
    session_close (session);

  g_object_unref (session);

  return G_SOURCE_REMOVE;
}

static void
on_muffin_session_closed (MuffinScreenCastSession *muffin_session,
                          gpointer user_data)
{
  ScreenCastSession *session = user_data;

  g_debug ("Muffin screen cast session closed");

  xdp_impl_session_emit_closed (XDP_IMPL_SESSION (session));
  g_idle_add (close_session_idle, g_object_ref (session));
}

static void
on_muffin_session_closed_during_start (MuffinScreenCastSession *muffin_session,
                                       gpointer user_data)
{
  StartOperation *op = user_data;

  if (op->completed)
    return;

  start_operation_fail (op, "Muffin screen cast session closed");
}

static void
on_muffin_session_ready (MuffinScreenCastSession *muffin_session,
                         gpointer user_data)
{
  StartOperation *op = user_data;
  GVariantBuilder streams_builder;
  GVariantBuilder results_builder;
  GVariant *results;

  if (op->completed)
    return;

  g_variant_builder_init (&streams_builder, G_VARIANT_TYPE ("a(ua{sv})"));
  muffin_screen_cast_session_add_stream_properties (muffin_session,
                                                    &streams_builder);

  g_variant_builder_init (&results_builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&results_builder, "{sv}",
                         "streams",
                         g_variant_builder_end (&streams_builder));
  results = g_variant_builder_end (&results_builder);

  op->session->started = TRUE;

  g_signal_connect (muffin_session, "closed",
                    G_CALLBACK (on_muffin_session_closed), op->session);

  start_operation_complete (op, 0, results);
}

static gboolean
start_muffin_session (StartOperation *op,
                      GArray *selected_indices)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) selections = NULL;
  MuffinScreenCastSession *muffin_session;
  gboolean alive = TRUE;
  guint i;

  start_operation_busy_enter (op);

  selections = g_ptr_array_new_with_free_func (NULL);
  for (i = 0; i < selected_indices->len; i++)
    {
      guint index = g_array_index (selected_indices, guint, i);

      if (index < op->items->len)
        g_ptr_array_add (selections, g_ptr_array_index (op->items, index));
    }

  if (selections->len == 0)
    {
      start_operation_fail (op, "No source selected");
      goto out;
    }

  muffin_session = muffin_screen_cast_create_session (muffin_screen_cast,
                                                      &error);
  if (!muffin_session)
    {
      start_operation_fail (op, error->message);
      goto out;
    }

  op->muffin_session = muffin_session;
  op->session->muffin_session = g_object_ref (muffin_session);

  g_signal_connect (muffin_session, "ready",
                    G_CALLBACK (on_muffin_session_ready), op);
  g_signal_connect (muffin_session, "closed",
                    G_CALLBACK (on_muffin_session_closed_during_start), op);

  for (i = 0; i < selections->len; i++)
    {
      PickerItem *selection;
      gboolean recorded;

      selection = g_ptr_array_index (selections, i);

      if (selection->type == SCREEN_CAST_SOURCE_TYPE_MONITOR)
        recorded = muffin_screen_cast_session_record_monitor (muffin_session,
                                                              selection->id,
                                                              selection->connector,
                                                              op->session->selection.cursor_mode,
                                                              &error);
      else
        recorded = muffin_screen_cast_session_record_window (muffin_session,
                                                             selection->id,
                                                             selection->window_id,
                                                             op->session->selection.cursor_mode,
                                                             &error);

      if (!recorded)
        {
          g_signal_handlers_disconnect_by_data (muffin_session, op);
          muffin_screen_cast_session_abort (muffin_session);
          g_clear_pointer ((GObject **) &op->muffin_session, g_object_unref);
          g_clear_pointer ((GObject **) &op->session->muffin_session, g_object_unref);
          start_operation_fail (op, error->message);
          goto out;
        }
    }

  if (!muffin_screen_cast_session_start (muffin_session, &error))
    {
      g_signal_handlers_disconnect_by_data (muffin_session, op);
      muffin_screen_cast_session_abort (muffin_session);
      g_clear_pointer ((GObject **) &op->muffin_session, g_object_unref);
      g_clear_pointer ((GObject **) &op->session->muffin_session, g_object_unref);
      start_operation_fail (op, error->message);
      goto out;
    }
out:
  alive = start_operation_busy_leave (op);

  return alive;
}

static gboolean
start_muffin_session_timeout (gpointer user_data)
{
  StartOperation *op = user_data;
  GArray *indices = g_steal_pointer (&op->selected_indices);

  op->defer_id = 0;
  start_muffin_session (op, indices);
  g_array_free (indices, TRUE);

  return G_SOURCE_REMOVE;
}

static void
on_dialog_done (ScreenCastDialog *dialog,
                gboolean accepted,
                GArray *selected_indices,
                gpointer user_data)
{
  StartOperation *op = user_data;
  guint i;

  if (op->completed)
    return;

  op->dialog = NULL;

  if (!accepted || !selected_indices || selected_indices->len == 0)
    {
      start_operation_cancel (op);
      return;
    }

  op->selected_indices = g_array_new (FALSE, FALSE, sizeof (guint));
  for (i = 0; i < selected_indices->len; i++)
    g_array_append_val (op->selected_indices,
                        g_array_index (selected_indices, guint, i));

  op->session->start_pending = TRUE;
  op->defer_id = g_timeout_add (100, start_muffin_session_timeout, op);
}

static gboolean
on_request_close (XdpImplRequest *object,
                  GDBusMethodInvocation *invocation,
                  gpointer user_data)
{
  StartOperation *op = user_data;

  if (op->completed)
    return FALSE;

  start_operation_cancel (op);

  return FALSE;
}

static GdkPixbuf *
capture_preview (PickerItem *item,
                 const char *path)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *script = NULL;
  g_autofree char *response = NULL;
  g_autoptr(GdkPixbuf) pixbuf = NULL;
  gboolean success = FALSE;

  if (!cinnamon)
    return NULL;

  if (item->type == SCREEN_CAST_SOURCE_TYPE_MONITOR)
    {
      script = g_strdup_printf (
        "var f='%s';var l=new imports.gi.GLib.MainLoop(null,false);var o=false;"
        "var s=new imports.gi.Cinnamon.Screenshot();var u=global.ui_scale||1;"
        "s.screenshot_area(false,Math.round(%d*u),Math.round(%d*u),"
        "Math.round(%d*u),Math.round(%d*u),f,false,"
        "function(a,b){o=b;l.quit();});"
        "l.run();o;",
        path,
        item->rect.x,
        item->rect.y,
        item->rect.width,
        item->rect.height);
    }
  else if (item->type == SCREEN_CAST_SOURCE_TYPE_WINDOW)
    {
      script = g_strdup_printf (
        "var f='%s';var l=new imports.gi.GLib.MainLoop(null,false);var o=false;"
        "var s=new imports.gi.Cinnamon.Screenshot();"
        "s.screenshot_window_by_id(%" G_GUINT64_FORMAT ",false,false,f,false,"
        "function(a,b){o=b;l.quit();});"
        "l.run();o;",
        path,
        item->window_id);
    }
  else
    {
      return NULL;
    }

  if (!org_cinnamon_call_eval_sync (cinnamon,
                                    script,
                                    &success,
                                    &response,
                                    NULL,
                                    &error))
    {
      g_warning ("Failed to capture preview: %s", error->message);
      return NULL;
    }

  if (!success)
    {
      g_warning ("Cinnamon Eval failed to capture preview");
      return NULL;
    }

  if (!g_file_test (path, G_FILE_TEST_EXISTS))
    return NULL;

  pixbuf = gdk_pixbuf_new_from_file (path, &error);
  if (!pixbuf)
    g_warning ("Failed to load preview %s: %s", path, error->message);

  return g_steal_pointer (&pixbuf);
}

static void
maybe_present_dialog (StartOperation *op)
{
  if (op->presented || !op->dialog)
    return;

  if (op->preview_index >= ITEMS_PRESENT_AFTER || op->preview_index >= op->items->len)
    {
      op->presented = TRUE;
      screencast_dialog_present (op->dialog);
    }
}

static gboolean
on_preview_idle (gpointer user_data)
{
  StartOperation *op = user_data;
  PickerItem *item;
  g_autoptr(GdkPixbuf) preview = NULL;
  g_autofree char *path = NULL;
  gboolean keep_going = FALSE;

  start_operation_busy_enter (op);

  if (op->dialog && op->preview_index < op->items->len)
    {
      item = g_ptr_array_index (op->items, op->preview_index);
      path = g_strdup_printf ("%s/%u.png", op->preview_dir, op->preview_index);
      preview = capture_preview (item, path);
      g_remove (path);

      if (op->dialog)
        screencast_dialog_set_item_preview (op->dialog, op->preview_index, preview);
      op->preview_index++;

      maybe_present_dialog (op);

      keep_going = op->preview_index < op->items->len;
    }
  else
    {
      maybe_present_dialog (op);
    }

  if (!start_operation_busy_leave (op))
    return G_SOURCE_REMOVE;

  if (!keep_going)
    op->preview_idle_id = 0;

  return keep_going ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

static gboolean
handle_start (XdpImplScreenCast *object,
              GDBusMethodInvocation *invocation,
              const char *arg_handle,
              const char *arg_session_handle,
              const char *arg_app_id,
              const char *arg_parent_window,
              GVariant *arg_options)
{
  ScreenCastSession *session;
  StartOperation *op;
  GDBusConnection *connection;
  guint i;

  g_debug ("Got new Start request");

  connection = g_dbus_method_invocation_get_connection (invocation);

  session = (ScreenCastSession *)lookup_session (arg_session_handle);
  if (!session ||
      !G_TYPE_CHECK_INSTANCE_TYPE (session, screencast_session_get_type ()))
    {
      g_warning ("No session found for %s", arg_session_handle);
      xdp_impl_screen_cast_complete_start (object, invocation, 2,
                                           new_empty_results ());
      return TRUE;
    }

  if (session->started || session->start_pending)
    {
      g_warning ("Session already started");
      xdp_impl_screen_cast_complete_start (object, invocation, 2,
                                           new_empty_results ());
      return TRUE;
    }

  op = g_new0 (StartOperation, 1);
  op->session = g_object_ref (session);
  op->impl = object;
  op->invocation = invocation;
  op->request = request_new (g_dbus_method_invocation_get_sender (invocation),
                             arg_app_id,
                             arg_handle);
  op->items = g_ptr_array_new_with_free_func (picker_item_free);

  if (session->selection.source_types & SCREEN_CAST_SOURCE_TYPE_MONITOR)
    {
      g_autoptr(GPtrArray) monitors = NULL;

      monitors = get_monitors (connection);
      for (i = 0; monitors && i < monitors->len; i++)
        {
          MuffinMonitor *monitor = g_ptr_array_index (monitors, i);
          PickerItem *item;

          item = picker_item_new (SCREEN_CAST_SOURCE_TYPE_MONITOR, i);
          item->title = g_strdup (monitor->label);
          item->subtitle = g_strdup_printf ("%d×%d%s",
                                            monitor->rect.width,
                                            monitor->rect.height,
                                            monitor->primary ? _("  (Primary)") : "");
          item->connector = g_strdup (monitor->connector);
          item->rect = monitor->rect;
          g_ptr_array_add (op->items, item);
        }
    }

  if (session->selection.source_types & SCREEN_CAST_SOURCE_TYPE_WINDOW)
    {
      g_autoptr(GPtrArray) windows = NULL;
      guint base_id = op->items->len;

      windows = get_windows (connection);
      for (i = 0; windows && i < windows->len; i++)
        {
          PickerItem *window = g_ptr_array_index (windows, i);

          window->id = base_id + i;
          g_ptr_array_add (op->items, window);
        }
    }

  if (op->items->len == 0)
    {
      start_operation_fail (op, "No shareable sources found");
      return TRUE;
    }

  g_signal_connect (op->request, "handle-close",
                    G_CALLBACK (on_request_close), op);
  request_export (op->request, connection);

  op->dialog = screencast_dialog_new (_("Choose what to share"),
                                      session->selection.multiple);

  op->preview_dir = g_dir_make_tmp ("xdp-xapp-preview-XXXXXX", NULL);

  for (i = 0; i < op->items->len; i++)
    {
      PickerItem *item = g_ptr_array_index (op->items, i);

      screencast_dialog_add_item (op->dialog, item->title, item->subtitle);
    }

  screencast_dialog_set_done_func (op->dialog, on_dialog_done, op);

  if (op->preview_dir)
    op->preview_idle_id = g_idle_add (on_preview_idle, op);
  else
    screencast_dialog_present (op->dialog);

  return TRUE;
}

static gboolean
handle_select_sources (XdpImplScreenCast *object,
                       GDBusMethodInvocation *invocation,
                       const char *arg_handle,
                       const char *arg_session_handle,
                       const char *arg_app_id,
                       GVariant *arg_options)
{
  ScreenCastSession *session;
  guint32 types = 0;
  gboolean multiple = FALSE;
  guint32 cursor_mode = SCREEN_CAST_CURSOR_MODE_HIDDEN;

  g_debug ("Got new SelectSources request");

  session = (ScreenCastSession *)lookup_session (arg_session_handle);
  if (!session ||
      !G_TYPE_CHECK_INSTANCE_TYPE (session, screencast_session_get_type ()))
    {
      g_warning ("No session found for %s", arg_session_handle);
      xdp_impl_screen_cast_complete_select_sources (object, invocation, 2,
                                                    new_empty_results ());
      return TRUE;
    }

  g_variant_lookup (arg_options, "types", "u", &types);
  g_variant_lookup (arg_options, "multiple", "b", &multiple);
  g_variant_lookup (arg_options, "cursor_mode", "u", &cursor_mode);

  session->selection.multiple = multiple;
  session->selection.cursor_mode = cursor_mode;

  if (types)
    session->selection.source_types = types;
  else
    session->selection.source_types =
      SCREEN_CAST_SOURCE_TYPE_MONITOR | SCREEN_CAST_SOURCE_TYPE_WINDOW;

  g_debug ("Screencast selection: types=%u multiple=%d cursor_mode=%u",
           session->selection.source_types,
           session->selection.multiple,
           session->selection.cursor_mode);

  xdp_impl_screen_cast_complete_select_sources (object, invocation, 0,
                                                new_empty_results ());

  return TRUE;
}

static gboolean
handle_create_session (XdpImplScreenCast *object,
                       GDBusMethodInvocation *invocation,
                       const char *arg_handle,
                       const char *arg_session_handle,
                       const char *arg_app_id,
                       GVariant *arg_options)
{
  g_autoptr(GError) error = NULL;
  int response;
  Session *session;

  g_debug ("Got new CreateSession request");

  session = (Session *)screencast_session_new (arg_session_handle);

  if (!session_export (session,
                       g_dbus_method_invocation_get_connection (invocation),
                       &error))
    {
      g_clear_object (&session);
      g_warning ("Failed to create screencast session: %s", error->message);
      response = 2;
    }
  else
    {
      response = 0;
    }

  xdp_impl_screen_cast_complete_create_session (object, invocation, response,
                                                new_empty_results ());

  return TRUE;
}

gboolean
screen_cast_init (GDBusConnection *bus,
                  GError **error)
{
  GDBusInterfaceSkeleton *helper;

  cinnamon = org_cinnamon_proxy_new_sync (bus,
                                          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                          "org.Cinnamon",
                                          "/org/Cinnamon",
                                          NULL,
                                          error);
  if (!cinnamon)
    return FALSE;

  muffin_screen_cast = muffin_screen_cast_new (bus);

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_screen_cast_skeleton_new ());

  g_object_set (helper,
                "version", 3,
                "available-source-types",
                SCREEN_CAST_SOURCE_TYPE_MONITOR | SCREEN_CAST_SOURCE_TYPE_WINDOW,
                "available-cursor-modes",
                SCREEN_CAST_CURSOR_MODE_HIDDEN |
                SCREEN_CAST_CURSOR_MODE_EMBEDDED |
                SCREEN_CAST_CURSOR_MODE_METADATA,
                NULL);

  g_signal_connect (helper, "handle-create-session",
                    G_CALLBACK (handle_create_session), NULL);
  g_signal_connect (helper, "handle-select-sources",
                    G_CALLBACK (handle_select_sources), NULL);
  g_signal_connect (helper, "handle-start",
                    G_CALLBACK (handle_start), NULL);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
