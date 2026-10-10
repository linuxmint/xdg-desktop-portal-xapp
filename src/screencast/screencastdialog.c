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

#include "screencastdialog.h"

#include <glib/gi18n.h>
#include <pango/pango.h>

struct _ScreenCastDialog
{
  GtkWidget *window;
  GtkWidget *grid;
  GPtrArray *check_buttons;
  gboolean multiple;
  ScreenCastDialogDoneFunc done_func;
  gpointer done_user_data;
  gboolean destroyed;
};

static void
screencast_dialog_update_share_sensitive (ScreenCastDialog *dialog)
{
  GtkWidget *share_button;
  gboolean any_selected = FALSE;
  guint i;

  if (dialog->destroyed)
    return;

  share_button = g_object_get_data (G_OBJECT (dialog->window), "share-button");
  if (!share_button)
    return;

  for (i = 0; i < dialog->check_buttons->len; i++)
    {
      GtkWidget *check = g_ptr_array_index (dialog->check_buttons, i);

      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check)))
        {
          any_selected = TRUE;
          break;
        }
    }

  gtk_widget_set_sensitive (share_button, any_selected);
}

static void
on_check_toggled (GtkToggleButton *toggle,
                  gpointer user_data)
{
  ScreenCastDialog *dialog = user_data;

  if (!dialog->multiple)
    {
      guint i;

      if (gtk_toggle_button_get_active (toggle))
        {
          for (i = 0; i < dialog->check_buttons->len; i++)
            {
              GtkWidget *check = g_ptr_array_index (dialog->check_buttons, i);

              if (check != GTK_WIDGET (toggle))
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), FALSE);
            }
        }
      else
        {
          gboolean any_selected = FALSE;

          for (i = 0; i < dialog->check_buttons->len; i++)
            {
              GtkWidget *check = g_ptr_array_index (dialog->check_buttons, i);

              if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check)))
                {
                  any_selected = TRUE;
                  break;
                }
            }

          if (!any_selected)
            gtk_toggle_button_set_active (toggle, TRUE);
        }
    }

  screencast_dialog_update_share_sensitive (dialog);
}

static void
screencast_dialog_emit_done (ScreenCastDialog *dialog,
                             gboolean accepted)
{
  GArray *selected_indices = NULL;

  if (accepted)
    {
      guint i;

      selected_indices = g_array_new (FALSE, FALSE, sizeof (guint));
      for (i = 0; i < dialog->check_buttons->len; i++)
        {
          GtkWidget *check = g_ptr_array_index (dialog->check_buttons, i);

          if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check)))
            {
              guint selected_index = i;

              g_array_append_val (selected_indices, selected_index);
            }
        }
    }

  if (dialog->done_func)
    dialog->done_func (dialog, accepted, selected_indices, dialog->done_user_data);

  if (selected_indices)
    g_array_free (selected_indices, TRUE);
}

static void
on_share_clicked (GtkButton *button,
                  gpointer user_data)
{
  ScreenCastDialog *dialog = user_data;

  screencast_dialog_emit_done (dialog, TRUE);
  screencast_dialog_destroy (dialog);
}

static void
on_cancel_clicked (GtkButton *button,
                   gpointer user_data)
{
  ScreenCastDialog *dialog = user_data;

  screencast_dialog_emit_done (dialog, FALSE);
  screencast_dialog_destroy (dialog);
}

static gboolean
on_window_delete_event (GtkWidget *widget,
                        GdkEvent *event,
                        gpointer user_data)
{
  ScreenCastDialog *dialog = user_data;

  if (dialog->destroyed)
    return TRUE;

  screencast_dialog_emit_done (dialog, FALSE);
  screencast_dialog_destroy (dialog);

  return TRUE;
}

void
screencast_dialog_destroy (ScreenCastDialog *dialog)
{
  if (dialog->destroyed)
    return;

  dialog->destroyed = TRUE;
  gtk_widget_destroy (dialog->window);
  g_ptr_array_free (dialog->check_buttons, TRUE);
  g_free (dialog);
}

void
screencast_dialog_set_done_func (ScreenCastDialog *dialog,
                                 ScreenCastDialogDoneFunc func,
                                 gpointer user_data)
{
  dialog->done_func = func;
  dialog->done_user_data = user_data;
}

static GdkPixbuf *
scale_to_fit (GdkPixbuf *pixbuf,
              int max_width,
              int max_height)
{
  int width = gdk_pixbuf_get_width (pixbuf);
  int height = gdk_pixbuf_get_height (pixbuf);
  double scale;

  if (width <= 0 || height <= 0)
    return NULL;

  scale = MIN ((double) max_width / width, (double) max_height / height);

  if (scale >= 1.0)
    return (GdkPixbuf *) g_object_ref (pixbuf);

  return gdk_pixbuf_scale_simple (pixbuf,
                                  MAX (1, (int) (width * scale)),
                                  MAX (1, (int) (height * scale)),
                                  GDK_INTERP_BILINEAR);
}

static gboolean
on_pointer_enter (GtkWidget *widget,
                  GdkEventCrossing *event,
                  gpointer user_data)
{
  GdkCursor *cursor;

  cursor = gdk_cursor_new_from_name (gtk_widget_get_display (widget), "pointer");
  if (cursor)
    {
      gdk_window_set_cursor (gtk_widget_get_window (widget), cursor);
      g_object_unref (cursor);
    }

  return FALSE;
}

static gboolean
on_pointer_leave (GtkWidget *widget,
                  GdkEventCrossing *event,
                  gpointer user_data)
{
  if (event->detail == GDK_NOTIFY_INFERIOR)
    return FALSE;

  gdk_window_set_cursor (gtk_widget_get_window (widget), NULL);

  return FALSE;
}

static void
install_pointer_cursor (GtkWidget *widget)
{
  gtk_widget_add_events (widget, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
  g_signal_connect (widget, "enter-notify-event", G_CALLBACK (on_pointer_enter), NULL);
  g_signal_connect (widget, "leave-notify-event", G_CALLBACK (on_pointer_leave), NULL);
}

static GtkWidget *
screencast_dialog_make_card (ScreenCastDialog *dialog,
                             const char *title,
                             const char *subtitle,
                             GdkPixbuf *preview)
{
  GtkWidget *check;
  GtkWidget *card;
  GtkWidget *title_label;

  check = gtk_toggle_button_new ();
  gtk_widget_set_size_request (check, 256, -1);
  install_pointer_cursor (check);

  card = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_margin_start (card, 8);
  gtk_widget_set_margin_end (card, 8);
  gtk_widget_set_margin_top (card, 8);
  gtk_widget_set_margin_bottom (card, 8);
  gtk_container_add (GTK_CONTAINER (check), card);

  if (preview)
    {
      GdkPixbuf *scaled;
      GtkWidget *image;

      scaled = scale_to_fit (preview, 240, 150);
      if (scaled)
        {
          image = gtk_image_new_from_pixbuf (scaled);
          gtk_widget_set_halign (image, GTK_ALIGN_CENTER);
          gtk_box_pack_start (GTK_BOX (card), image, FALSE, FALSE, 0);
          g_object_unref (scaled);
        }
    }

  title_label = gtk_label_new (title);
  gtk_label_set_xalign (GTK_LABEL (title_label), 0.5);
  gtk_label_set_justify (GTK_LABEL (title_label), GTK_JUSTIFY_CENTER);
  gtk_label_set_line_wrap (GTK_LABEL (title_label), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (title_label), 24);
  gtk_label_set_ellipsize (GTK_LABEL (title_label), PANGO_ELLIPSIZE_END);
  gtk_box_pack_start (GTK_BOX (card), title_label, FALSE, FALSE, 0);

  if (subtitle && subtitle[0])
    {
      GtkWidget *subtitle_label;

      subtitle_label = gtk_label_new (subtitle);
      gtk_label_set_xalign (GTK_LABEL (subtitle_label), 0.5);
      gtk_widget_set_opacity (subtitle_label, 0.7);
      gtk_box_pack_start (GTK_BOX (card), subtitle_label, FALSE, FALSE, 0);
    }

  g_signal_connect (check, "toggled", G_CALLBACK (on_check_toggled), dialog);
  g_ptr_array_add (dialog->check_buttons, check);

  return check;
}

void
screencast_dialog_add_item (ScreenCastDialog *dialog,
                            const char *title,
                            const char *subtitle,
                            GdkPixbuf *preview)
{
  GtkWidget *card;
  GtkWidget *child;

  card = screencast_dialog_make_card (dialog, title, subtitle, preview);
  child = gtk_flow_box_child_new ();
  gtk_container_add (GTK_CONTAINER (child), card);
  gtk_flow_box_insert (GTK_FLOW_BOX (dialog->grid), child, -1);
}

void
screencast_dialog_set_action_label (ScreenCastDialog *dialog,
                                    const char *label)
{
  GtkWidget *share_button;

  share_button = g_object_get_data (G_OBJECT (dialog->window), "share-button");
  if (share_button)
    gtk_button_set_label (GTK_BUTTON (share_button), label);
}

void
screencast_dialog_present (ScreenCastDialog *dialog)
{
  gtk_widget_show_all (dialog->window);
  gtk_window_present (GTK_WINDOW (dialog->window));
}

ScreenCastDialog *
screencast_dialog_new (const char *heading,
                       gboolean multiple)
{
  ScreenCastDialog *dialog;
  GtkWidget *content_box;
  GtkWidget *heading_label;
  GtkWidget *scrolled_window;
  GtkWidget *button_box;
  GtkWidget *cancel_button;
  GtkWidget *share_button;

  dialog = g_new0 (ScreenCastDialog, 1);
  dialog->check_buttons = g_ptr_array_new ();
  dialog->multiple = multiple;

  dialog->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (dialog->window), _("Record Screen"));
  gtk_window_set_default_size (GTK_WINDOW (dialog->window), 760, 600);
  gtk_window_set_modal (GTK_WINDOW (dialog->window), TRUE);
  gtk_window_set_position (GTK_WINDOW (dialog->window), GTK_WIN_POS_CENTER);

  g_signal_connect (dialog->window, "delete-event",
                    G_CALLBACK (on_window_delete_event), dialog);

  content_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_start (content_box, 16);
  gtk_widget_set_margin_end (content_box, 16);
  gtk_widget_set_margin_top (content_box, 16);
  gtk_widget_set_margin_bottom (content_box, 16);
  gtk_container_add (GTK_CONTAINER (dialog->window), content_box);

  heading_label = gtk_label_new (heading);
  gtk_label_set_xalign (GTK_LABEL (heading_label), 0.0);
  gtk_label_set_line_wrap (GTK_LABEL (heading_label), TRUE);
  PangoAttrList *attrs = pango_attr_list_new ();
  pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
  pango_attr_list_insert (attrs, pango_attr_scale_new (1.2));
  gtk_label_set_attributes (GTK_LABEL (heading_label), attrs);
  pango_attr_list_unref (attrs);
  gtk_box_pack_start (GTK_BOX (content_box), heading_label, FALSE, FALSE, 0);

  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand (scrolled_window, TRUE);
  gtk_box_pack_start (GTK_BOX (content_box), scrolled_window, TRUE, TRUE, 0);

  dialog->grid = gtk_flow_box_new ();
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (dialog->grid), GTK_SELECTION_NONE);
  gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (dialog->grid), TRUE);
  gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (dialog->grid), 1);
  gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (dialog->grid), 12);
  gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (dialog->grid), 12);
  gtk_widget_set_valign (dialog->grid, GTK_ALIGN_START);
  gtk_container_add (GTK_CONTAINER (scrolled_window), dialog->grid);

  button_box = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_END);
  gtk_box_pack_start (GTK_BOX (content_box), button_box, FALSE, FALSE, 0);

  cancel_button = gtk_button_new_with_label (_("Cancel"));
  install_pointer_cursor (cancel_button);
  gtk_box_pack_start (GTK_BOX (button_box), cancel_button, FALSE, FALSE, 0);
  g_signal_connect (cancel_button, "clicked", G_CALLBACK (on_cancel_clicked), dialog);

  share_button = gtk_button_new_with_label (_("Share"));
  gtk_widget_set_sensitive (share_button, FALSE);
  gtk_widget_set_margin_start (share_button, 12);
  install_pointer_cursor (share_button);
  g_object_set_data (G_OBJECT (dialog->window), "share-button", share_button);
  gtk_box_pack_start (GTK_BOX (button_box), share_button, FALSE, FALSE, 0);
  g_signal_connect (share_button, "clicked", G_CALLBACK (on_share_clicked), dialog);

  return dialog;
}