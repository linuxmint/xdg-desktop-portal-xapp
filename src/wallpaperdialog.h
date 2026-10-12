#pragma once

#include <gtk/gtk.h>

void       wallpaper_dialog_new_async  (GBytes              *image,
                                        GdkDisplay          *display,
                                        GCancellable        *cancellable,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data);

GtkWidget *wallpaper_dialog_new_finish (GAsyncResult        *result,
                                        GError             **error);
