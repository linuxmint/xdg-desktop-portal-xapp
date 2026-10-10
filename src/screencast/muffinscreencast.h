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

typedef struct _MuffinScreenCast MuffinScreenCast;
typedef struct _MuffinScreenCastSession MuffinScreenCastSession;

MuffinScreenCast *muffin_screen_cast_new (GDBusConnection *connection);

int muffin_screen_cast_get_api_version (MuffinScreenCast *screen_cast);

MuffinScreenCastSession *muffin_screen_cast_create_session (MuffinScreenCast *screen_cast,
                                                            GError **error);

gboolean muffin_screen_cast_session_record_selections (MuffinScreenCastSession *session,
                                                       GPtrArray *streams,
                                                       guint32 cursor_mode,
                                                       GError **error);

gboolean muffin_screen_cast_session_start (MuffinScreenCastSession *session,
                                           GError **error);

gboolean muffin_screen_cast_session_stop (MuffinScreenCastSession *session,
                                          GError **error);

void muffin_screen_cast_session_add_stream_properties (MuffinScreenCastSession *session,
                                                       GVariantBuilder *streams_builder);