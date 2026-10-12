#include <config.h>

#include <math.h>

#include <glib/gi18n.h>

#include "wallpaperdialog.h"

#define PREVIEW_SIZE 400

typedef struct {
    GBytes *image;
    GdkDisplay *display;
    gint width;
    gint height;
    gint scale;
} PreviewData;

static void
preview_data_free (PreviewData *data)
{
    g_bytes_unref (data->image);
    g_object_unref (data->display);

    g_free (data);
}

static void
measure_preview (PreviewData *data)
{
    GdkMonitor *monitor;
    GdkRectangle geometry = { 0, 0, 16, 9 };
    gint i;

    monitor = gdk_display_get_primary_monitor (data->display);

    if (monitor == NULL)
        monitor = gdk_display_get_monitor (data->display, 0);

    if (monitor != NULL)
        gdk_monitor_get_geometry (monitor, &geometry);

    if (geometry.width >= geometry.height)
    {
        data->width = PREVIEW_SIZE;
        data->height = PREVIEW_SIZE * geometry.height / geometry.width;
    }
    else
    {
        data->width = PREVIEW_SIZE * geometry.width / geometry.height;
        data->height = PREVIEW_SIZE;
    }

    data->scale = 1;

    for (i = 0; i < gdk_display_get_n_monitors (data->display); i++)
    {
        monitor = gdk_display_get_monitor (data->display, i);
        data->scale = MAX (data->scale, gdk_monitor_get_scale_factor (monitor));
    }
}

static void
on_size_prepared (GdkPixbufLoader *loader,
                  gint             width,
                  gint             height,
                  PreviewData     *data)
{
    gdouble scale = MAX ((gdouble) data->width * data->scale / width,
                         (gdouble) data->height * data->scale / height);

    if (scale < 1.0)
        gdk_pixbuf_loader_set_size (loader, ceil (width * scale), ceil (height * scale));
}

static void
decode_preview (GTask        *task,
                gpointer      source_object,
                gpointer      task_data,
                GCancellable *cancellable)
{
    PreviewData *data = task_data;
    g_autoptr(GdkPixbufLoader) loader = gdk_pixbuf_loader_new ();
    GError *error = NULL;

    g_signal_connect (loader, "size-prepared", G_CALLBACK (on_size_prepared), data);

    if (!gdk_pixbuf_loader_write_bytes (loader, data->image, &error))
    {
        gdk_pixbuf_loader_close (loader, NULL);
        g_task_return_error (task, error);
        return;
    }

    if (!gdk_pixbuf_loader_close (loader, &error))
    {
        g_task_return_error (task, error);
        return;
    }

    g_task_return_pointer (task,
                           gdk_pixbuf_apply_embedded_orientation (gdk_pixbuf_loader_get_pixbuf (loader)),
                           g_object_unref);
}

static gboolean
draw_preview (GtkWidget       *area,
              cairo_t         *cr,
              cairo_surface_t *image)
{
    gint width = gtk_widget_get_allocated_width (area);
    gint height = gtk_widget_get_allocated_height (area);
    gint image_width = cairo_image_surface_get_width (image);
    gint image_height = cairo_image_surface_get_height (image);
    gdouble scale = MAX ((gdouble) width / image_width, (gdouble) height / image_height);

    cairo_translate (cr, (width - image_width * scale) / 2, (height - image_height * scale) / 2);
    cairo_scale (cr, scale, scale);
    cairo_set_source_surface (cr, image, 0, 0);
    cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_GOOD);
    cairo_paint (cr);

    return FALSE;
}

void
wallpaper_dialog_new_async (GBytes              *image,
                            GdkDisplay          *display,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
    g_autoptr(GTask) task = g_task_new (NULL, cancellable, callback, user_data);
    PreviewData *data = g_new0 (PreviewData, 1);

    data->image = g_bytes_ref (image);
    data->display = g_object_ref (display);
    measure_preview (data);

    g_task_set_task_data (task, data, (GDestroyNotify) preview_data_free);
    g_task_run_in_thread (task, decode_preview);
}

GtkWidget *
wallpaper_dialog_new_finish (GAsyncResult  *result,
                             GError       **error)
{
    PreviewData *data = g_task_get_task_data (G_TASK (result));
    g_autoptr(GdkPixbuf) pixbuf = NULL;
    cairo_surface_t *image;
    GdkScreen *screen;
    GtkWidget *dialog, *fake_parent, *area, *frame, *content;

    pixbuf = g_task_propagate_pointer (G_TASK (result), error);

    if (pixbuf == NULL)
        return NULL;

    screen = gdk_display_get_default_screen (data->display);

    dialog = g_object_new (GTK_TYPE_DIALOG,
                           "title", _("Set Background"),
                           "screen", screen,
                           "modal", TRUE,
                           "resizable", FALSE,
                           NULL);

    gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                            _("_Cancel"), GTK_RESPONSE_CANCEL,
                            _("_Set"), GTK_RESPONSE_APPLY,
                            NULL);
    gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_APPLY);

    // The real parent is foreign, this keeps GtkDialog from complaining about having none.
    fake_parent = g_object_new (GTK_TYPE_WINDOW, "screen", screen, NULL);
    gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (fake_parent));
    g_signal_connect_swapped (dialog, "destroy", G_CALLBACK (gtk_widget_destroy), fake_parent);

    image = gdk_cairo_surface_create_from_pixbuf (pixbuf, 1, NULL);

    area = gtk_drawing_area_new ();
    gtk_widget_set_size_request (area, data->width, data->height);
    g_object_set_data_full (G_OBJECT (area), "image", image, (GDestroyNotify) cairo_surface_destroy);
    g_signal_connect (area, "draw", G_CALLBACK (draw_preview), image);

    frame = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
    gtk_container_add (GTK_CONTAINER (frame), area);

    content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    gtk_container_set_border_width (GTK_CONTAINER (content), 12);
    gtk_container_add (GTK_CONTAINER (content), frame);
    gtk_widget_show_all (frame);

    return dialog;
}
