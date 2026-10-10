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

#include <gtk/gtk.h>

typedef struct _ScreenCastDialog ScreenCastDialog;

typedef void (*ScreenCastDialogDoneFunc) (ScreenCastDialog *dialog,
                                          gboolean accepted,
                                          GArray *selected_indices,
                                          gpointer user_data);

ScreenCastDialog *screencast_dialog_new (const char *heading,
                                         gboolean multiple);

void screencast_dialog_add_item (ScreenCastDialog *dialog,
                                 const char *title,
                                 const char *subtitle,
                                 GdkPixbuf *preview);

void screencast_dialog_set_action_label (ScreenCastDialog *dialog,
                                         const char *label);

void screencast_dialog_set_done_func (ScreenCastDialog *dialog,
                                      ScreenCastDialogDoneFunc func,
                                      gpointer user_data);

void screencast_dialog_present (ScreenCastDialog *dialog);

void screencast_dialog_destroy (ScreenCastDialog *dialog);