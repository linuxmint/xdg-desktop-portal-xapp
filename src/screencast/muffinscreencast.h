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

#pragma once

#include <glib-object.h>
#include <gio/gio.h>
#include <stdint.h>

#include "screencast.h"

typedef struct _MuffinScreenCast MuffinScreenCast;
typedef struct _MuffinScreenCastSession MuffinScreenCastSession;

MuffinScreenCast *muffin_screen_cast_new (GDBusConnection *connection);

MuffinScreenCastSession *muffin_screen_cast_create_session (MuffinScreenCast *screen_cast,
                                                            GError **error);

gboolean muffin_screen_cast_session_record_monitor (MuffinScreenCastSession *session,
                                                    guint32 id,
                                                    const char *connector,
                                                    ScreenCastCursorMode cursor_mode,
                                                    GError **error);

gboolean muffin_screen_cast_session_record_window (MuffinScreenCastSession *session,
                                                   guint32 id,
                                                   guint64 window_id,
                                                   ScreenCastCursorMode cursor_mode,
                                                   GError **error);

gboolean muffin_screen_cast_session_start (MuffinScreenCastSession *session,
                                           GError **error);

gboolean muffin_screen_cast_session_stop (MuffinScreenCastSession *session,
                                          GError **error);

void muffin_screen_cast_session_add_stream_properties (MuffinScreenCastSession *session,
                                                       GVariantBuilder *streams_builder);
