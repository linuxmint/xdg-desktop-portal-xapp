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

#include "muffinscreencast.h"
#include "common-dbus.h"
#include "screencast.h"

#include <stdint.h>

enum
{
  SESSION_SIGNAL_READY,
  SESSION_SIGNAL_CLOSED,

  N_SESSION_SIGNALS
};

static guint session_signals[N_SESSION_SIGNALS];

typedef enum
{
  MUFFIN_CURSOR_MODE_HIDDEN,
  MUFFIN_CURSOR_MODE_EMBEDDED,
  MUFFIN_CURSOR_MODE_METADATA,
} MuffinCursorMode;

typedef struct _MuffinScreenCastStream
{
  GObject parent;

  MuffinScreenCastSession *session;

  ScreenCastSourceType source_type;

  guint32 id;
  char *path;
  OrgCinnamonMuffinScreenCastStream *proxy;

  guint32 pipewire_node_id;

  gboolean has_position;
  int x;
  int y;

  gboolean has_size;
  int width;
  int height;
} MuffinScreenCastStream;

typedef struct _MuffinScreenCastStreamClass
{
  GObjectClass parent_class;
} MuffinScreenCastStreamClass;

static void
read_parameters (MuffinScreenCastStream *stream)
{
  GVariant *parameters;

  parameters = org_cinnamon_muffin_screen_cast_stream_get_parameters (stream->proxy);
  if (!parameters)
    {
      g_warning ("Screen cast stream %s missing parameters", stream->path);
      return;
    }

  if (g_variant_lookup (parameters, "position", "(ii)",
                        &stream->x, &stream->y))
    stream->has_position = TRUE;
  if (g_variant_lookup (parameters, "size", "(ii)",
                        &stream->width, &stream->height))
    stream->has_size = TRUE;
}

typedef struct _MuffinScreenCastSession
{
  GObject parent;

  char *path;
  OrgCinnamonMuffinScreenCastSession *proxy;
  gulong closed_handler_id;
  gboolean closed;

  GList *streams;
  int n_needed_stream_node_ids;
} MuffinScreenCastSession;

typedef struct _MuffinScreenCastSessionClass
{
  GObjectClass parent_class;
} MuffinScreenCastSessionClass;

typedef struct _MuffinScreenCast
{
  GObject parent;

  guint screen_cast_name_watch;
  OrgCinnamonMuffinScreenCast *proxy;
} MuffinScreenCast;

typedef struct _MuffinScreenCastClass
{
  GObjectClass parent_class;
} MuffinScreenCastClass;

static GType muffin_screen_cast_stream_get_type (void);
G_DEFINE_TYPE (MuffinScreenCastStream, muffin_screen_cast_stream, G_TYPE_OBJECT)

static GType muffin_screen_cast_session_get_type (void);
G_DEFINE_TYPE (MuffinScreenCastSession, muffin_screen_cast_session, G_TYPE_OBJECT)

static GType muffin_screen_cast_get_type (void);
G_DEFINE_TYPE (MuffinScreenCast, muffin_screen_cast, G_TYPE_OBJECT)

static void
muffin_screen_cast_stream_finalize (GObject *object)
{
  MuffinScreenCastStream *stream = (MuffinScreenCastStream *)object;

  g_clear_object (&stream->proxy);
  g_free (stream->path);

  G_OBJECT_CLASS (muffin_screen_cast_stream_parent_class)->finalize (object);
}

static void
on_pipewire_stream_added (OrgCinnamonMuffinScreenCastStream *stream_proxy,
                          guint arg_node_id,
                          MuffinScreenCastStream *stream)
{
  if (stream->pipewire_node_id != 0 ||
      stream->session->n_needed_stream_node_ids == 0)
    return;

  stream->pipewire_node_id = arg_node_id;
  read_parameters (stream);

  stream->session->n_needed_stream_node_ids--;

  if (stream->session->n_needed_stream_node_ids == 0)
    g_signal_emit (stream->session, session_signals[SESSION_SIGNAL_READY], 0);
}

static void
muffin_screen_cast_stream_init (MuffinScreenCastStream *stream)
{
}

static void
muffin_screen_cast_stream_class_init (MuffinScreenCastStreamClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = muffin_screen_cast_stream_finalize;
}

void
muffin_screen_cast_session_add_stream_properties (MuffinScreenCastSession *session,
                                                  GVariantBuilder *streams_builder)
{
  char id[64] = { 0, };
  GList *streams;
  GList *l;

  streams = session->streams;
  for (l = streams; l; l = l->next)
    {
      MuffinScreenCastStream *stream = l->data;
      GVariantBuilder stream_properties_builder;

      g_variant_builder_init (&stream_properties_builder, G_VARIANT_TYPE_VARDICT);

      g_snprintf (id, G_N_ELEMENTS (id), "%u", stream->id);
      g_variant_builder_add (&stream_properties_builder, "{sv}",
                             "id",
                             g_variant_new_string (id));
      g_variant_builder_add (&stream_properties_builder, "{sv}",
                             "source_type",
                             g_variant_new_uint32 (stream->source_type));

      if (stream->has_position)
        g_variant_builder_add (&stream_properties_builder, "{sv}",
                               "position",
                               g_variant_new ("(ii)", stream->x, stream->y));
      if (stream->has_size)
        g_variant_builder_add (&stream_properties_builder, "{sv}",
                               "size",
                               g_variant_new ("(ii)",
                                              stream->width,
                                              stream->height));

      g_variant_builder_add (streams_builder, "(ua{sv})",
                             stream->pipewire_node_id,
                             &stream_properties_builder);
    }
}

static guint32
cursor_mode_to_muffin_cursor_mode (ScreenCastCursorMode cursor_mode)
{
  switch (cursor_mode)
    {
    case SCREEN_CAST_CURSOR_MODE_HIDDEN:
      return MUFFIN_CURSOR_MODE_HIDDEN;
    case SCREEN_CAST_CURSOR_MODE_EMBEDDED:
      return MUFFIN_CURSOR_MODE_EMBEDDED;
    case SCREEN_CAST_CURSOR_MODE_METADATA:
      return MUFFIN_CURSOR_MODE_METADATA;
    default:
      g_warning ("Unknown cursor mode %u, using hidden", (guint) cursor_mode);
      return MUFFIN_CURSOR_MODE_HIDDEN;
    }
}

static void
add_cursor_mode_property (GVariantBuilder *properties_builder,
                          ScreenCastCursorMode cursor_mode)
{
  if (cursor_mode == SCREEN_CAST_CURSOR_MODE_NONE)
    return;

  g_variant_builder_add (properties_builder, "{sv}",
                         "cursor-mode",
                         g_variant_new_uint32 (cursor_mode_to_muffin_cursor_mode (cursor_mode)));
}

static gboolean
muffin_screen_cast_session_add_stream (MuffinScreenCastSession *session,
                                       const char *stream_path,
                                       guint32 id,
                                       ScreenCastSourceType source_type,
                                       GError **error)
{
  GDBusConnection *connection;
  OrgCinnamonMuffinScreenCastStream *stream_proxy;
  MuffinScreenCastStream *stream;

  connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (session->proxy));
  stream_proxy =
    org_cinnamon_muffin_screen_cast_stream_proxy_new_sync (connection,
                                                           G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                           "org.cinnamon.Muffin.ScreenCast",
                                                           stream_path,
                                                           NULL,
                                                           error);
  if (!stream_proxy)
    return FALSE;

  stream = g_object_new (muffin_screen_cast_stream_get_type (), NULL);
  stream->source_type = source_type;
  stream->session = session;
  stream->path = g_strdup (stream_path);
  stream->proxy = stream_proxy;
  stream->id = id;

  read_parameters (stream);

  g_signal_connect (stream_proxy, "pipewire-stream-added",
                    G_CALLBACK (on_pipewire_stream_added),
                    stream);

  session->streams = g_list_append (session->streams, stream);
  session->n_needed_stream_node_ids++;

  return TRUE;
}

gboolean
muffin_screen_cast_session_record_window (MuffinScreenCastSession *session,
                                          guint32 id,
                                          guint64 window_id,
                                          ScreenCastCursorMode cursor_mode,
                                          GError **error)
{
  GVariantBuilder properties_builder;
  GVariant *properties;
  g_autofree char *stream_path = NULL;

  g_variant_builder_init (&properties_builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&properties_builder, "{sv}",
                         "window-id",
                         g_variant_new_uint64 (window_id));
  add_cursor_mode_property (&properties_builder, cursor_mode);
  properties = g_variant_builder_end (&properties_builder);

  if (!org_cinnamon_muffin_screen_cast_session_call_record_window_sync (session->proxy,
                                                                        properties,
                                                                        &stream_path,
                                                                        NULL,
                                                                        error))
    return FALSE;

  return muffin_screen_cast_session_add_stream (session,
                                                stream_path,
                                                id,
                                                SCREEN_CAST_SOURCE_TYPE_WINDOW,
                                                error);
}

gboolean
muffin_screen_cast_session_record_monitor (MuffinScreenCastSession *session,
                                           guint32 id,
                                           const char *connector,
                                           ScreenCastCursorMode cursor_mode,
                                           GError **error)
{
  GVariantBuilder properties_builder;
  GVariant *properties;
  g_autofree char *stream_path = NULL;

  g_variant_builder_init (&properties_builder, G_VARIANT_TYPE_VARDICT);
  add_cursor_mode_property (&properties_builder, cursor_mode);
  properties = g_variant_builder_end (&properties_builder);

  if (!org_cinnamon_muffin_screen_cast_session_call_record_monitor_sync (session->proxy,
                                                                         connector,
                                                                         properties,
                                                                         &stream_path,
                                                                         NULL,
                                                                         error))
    return FALSE;

  return muffin_screen_cast_session_add_stream (session,
                                                stream_path,
                                                id,
                                                SCREEN_CAST_SOURCE_TYPE_MONITOR,
                                                error);
}

void
muffin_screen_cast_session_abort (MuffinScreenCastSession *session)
{
  g_autoptr(GError) error = NULL;

  if (!session)
    return;

  if (!muffin_screen_cast_session_stop (session, &error))
    g_warning ("Failed to stop muffin screencast session: %s",
               error->message);
}

gboolean
muffin_screen_cast_session_stop (MuffinScreenCastSession *session,
                                 GError **error)
{
  if (session->closed_handler_id)
    {
      g_signal_handler_disconnect (session->proxy, session->closed_handler_id);
      session->closed_handler_id = 0;
    }

  if (session->closed)
    return TRUE;

  if (!org_cinnamon_muffin_screen_cast_session_call_stop_sync (session->proxy,
                                                               NULL,
                                                               error))
    return FALSE;

  return TRUE;
}

gboolean
muffin_screen_cast_session_start (MuffinScreenCastSession *session,
                                  GError **error)
{
  OrgCinnamonMuffinScreenCastSession *session_proxy = session->proxy;

  if (!org_cinnamon_muffin_screen_cast_session_call_start_sync (session_proxy,
                                                                NULL,
                                                                error))
    return FALSE;

  return TRUE;
}

static void
muffin_screen_cast_session_finalize (GObject *object)
{
  MuffinScreenCastSession *session = (MuffinScreenCastSession *)object;

  g_list_free_full (session->streams, g_object_unref);
  g_clear_object (&session->proxy);
  g_free (session->path);

  G_OBJECT_CLASS (muffin_screen_cast_session_parent_class)->finalize (object);
}

static void
muffin_screen_cast_session_init (MuffinScreenCastSession *session)
{
}

static void
muffin_screen_cast_session_class_init (MuffinScreenCastSessionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = muffin_screen_cast_session_finalize;

  session_signals[SESSION_SIGNAL_READY] = g_signal_new ("ready",
                                                        G_TYPE_FROM_CLASS (klass),
                                                        G_SIGNAL_RUN_LAST,
                                                        0,
                                                        NULL, NULL,
                                                        NULL,
                                                        G_TYPE_NONE, 0);
  session_signals[SESSION_SIGNAL_CLOSED] = g_signal_new ("closed",
                                                         G_TYPE_FROM_CLASS (klass),
                                                         G_SIGNAL_RUN_LAST,
                                                         0,
                                                         NULL, NULL,
                                                         NULL,
                                                         G_TYPE_NONE, 0);
}

static void
on_muffin_session_closed (OrgCinnamonMuffinScreenCastSession *session_proxy,
                          MuffinScreenCastSession *session)
{
  session->closed = TRUE;
  g_signal_emit (session, session_signals[SESSION_SIGNAL_CLOSED], 0);
}

MuffinScreenCastSession *
muffin_screen_cast_create_session (MuffinScreenCast *screen_cast,
                                   GError **error)
{
  GVariantBuilder properties_builder;
  GVariant *properties;
  g_autofree char *session_path = NULL;
  GDBusConnection *connection;
  OrgCinnamonMuffinScreenCastSession *session_proxy;
  MuffinScreenCastSession *session;

  if (!screen_cast->proxy)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Muffin ScreenCast is not available");
      return NULL;
    }

  g_variant_builder_init (&properties_builder, G_VARIANT_TYPE_VARDICT);
  properties = g_variant_builder_end (&properties_builder);
  if (!org_cinnamon_muffin_screen_cast_call_create_session_sync (screen_cast->proxy,
                                                                 properties,
                                                                 &session_path,
                                                                 NULL,
                                                                 error))
    return NULL;

  connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (screen_cast->proxy));
  session_proxy =
    org_cinnamon_muffin_screen_cast_session_proxy_new_sync (connection,
                                                            G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                            "org.cinnamon.Muffin.ScreenCast",
                                                            session_path,
                                                            NULL,
                                                            error);
  if (!session_proxy)
    return NULL;

  session = g_object_new (muffin_screen_cast_session_get_type (), NULL);
  session->path = g_steal_pointer (&session_path);
  session->proxy = g_steal_pointer (&session_proxy);
  session->closed_handler_id =
    g_signal_connect (session->proxy,
                      "closed", G_CALLBACK (on_muffin_session_closed),
                      session);

  return session;
}

static void
muffin_screen_cast_name_appeared (GDBusConnection *connection,
                                  const char *name,
                                  const char *name_owner,
                                  gpointer user_data)
{
  MuffinScreenCast *screen_cast = user_data;
  g_autoptr(GError) error = NULL;

  screen_cast->proxy =
    org_cinnamon_muffin_screen_cast_proxy_new_sync (connection,
                                                    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                    "org.cinnamon.Muffin.ScreenCast",
                                                    "/org/cinnamon/Muffin/ScreenCast",
                                                    NULL,
                                                    &error);
  if (!screen_cast->proxy)
    {
      g_warning ("Failed to acquire org.cinnamon.Muffin.ScreenCast proxy: %s",
                 error->message);
      return;
    }
}

static void
muffin_screen_cast_name_vanished (GDBusConnection *connection,
                                  const char *name,
                                  gpointer user_data)
{
  MuffinScreenCast *screen_cast = user_data;

  g_clear_object (&screen_cast->proxy);
}

static void
muffin_screen_cast_finalize (GObject *object)
{
  MuffinScreenCast *screen_cast = (MuffinScreenCast *)object;

  if (screen_cast->screen_cast_name_watch)
    g_bus_unwatch_name (screen_cast->screen_cast_name_watch);
  g_clear_object (&screen_cast->proxy);

  G_OBJECT_CLASS (muffin_screen_cast_parent_class)->finalize (object);
}

static void
muffin_screen_cast_init (MuffinScreenCast *screen_cast)
{
}

static void
muffin_screen_cast_class_init (MuffinScreenCastClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = muffin_screen_cast_finalize;
}

MuffinScreenCast *
muffin_screen_cast_new (GDBusConnection *connection)
{
  MuffinScreenCast *screen_cast;

  screen_cast = g_object_new (muffin_screen_cast_get_type (), NULL);
  screen_cast->screen_cast_name_watch =
    g_bus_watch_name_on_connection (connection,
                                    "org.cinnamon.Muffin.ScreenCast",
                                    G_BUS_NAME_WATCHER_FLAGS_NONE,
                                    muffin_screen_cast_name_appeared,
                                    muffin_screen_cast_name_vanished,
                                    screen_cast,
                                    NULL);

  return screen_cast;
}
