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

#include <glib.h>
#include <gio/gio.h>
#include <stdint.h>

typedef enum _ScreenCastSourceType
{
  SCREEN_CAST_SOURCE_TYPE_MONITOR = 1,
  SCREEN_CAST_SOURCE_TYPE_WINDOW = 2,
  SCREEN_CAST_SOURCE_TYPE_VIRTUAL = 4,
} ScreenCastSourceType;

typedef enum _ScreenCastCursorMode
{
  SCREEN_CAST_CURSOR_MODE_NONE = 0,
  SCREEN_CAST_CURSOR_MODE_HIDDEN = 1,
  SCREEN_CAST_CURSOR_MODE_EMBEDDED = 2,
  SCREEN_CAST_CURSOR_MODE_METADATA = 4,
} ScreenCastCursorMode;

typedef struct _ScreenCastSelection
{
  gboolean multiple;
  guint32 source_types;
  ScreenCastCursorMode cursor_mode;
} ScreenCastSelection;

typedef struct _MuffinMonitor
{
  char *connector;
  char *label;
  gboolean primary;
  int x;
  int y;
  int width;
  int height;
} MuffinMonitor;

typedef struct _PickerItem
{
  ScreenCastSourceType type;
  guint32 id;
  char *title;
  char *subtitle;
  char *connector;
  guint64 window_id;
  int x;
  int y;
  int width;
  int height;
} PickerItem;

void picker_item_free (gpointer data);

gboolean screen_cast_init (GDBusConnection *connection,
                           GError **error);