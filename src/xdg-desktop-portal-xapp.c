/*
 * Copyright © 2016 Red Hat, Inc
 * Copyright © 2021 Endless OS Foundation
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
 * Authors:
 *       Georges Basile Stavracas Neto <georges@endlessos.org>
 *       Matthias Clasen <mclasen@redhat.com>
 */

#define _GNU_SOURCE 1

#include <config.h>

#include <errno.h>
#include <locale.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gunixfdlist.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <locale.h>

#include "xdg-desktop-portal-dbus.h"

#include "utils.h"
#include "inhibit.h"
#include "lockdown.h"
#include "request.h"
#include "screenshot.h"
#include "settings.h"
#include "wallpaper.h"

static GMainLoop *loop = NULL;
static GHashTable *outstanding_handles = NULL;

static gboolean opt_verbose;
static gboolean opt_replace;
static gboolean show_version;
const gchar *mode = NULL;
gchar *desktop_arg;

const gchar *desktops[] = { "cinnamon", "mate", "xfce", NULL };

static GOptionEntry entries[] = {
  { "desktop", 'd', 0, G_OPTION_ARG_STRING, &desktop_arg, "Specify desktop to represent (cinnamon | mate | xfce), if unspecified, XDG_CURRENT_DESKTOP is used", NULL },
  { "verbose", 'v', 0, G_OPTION_ARG_NONE, &opt_verbose, "Print debug information during command processing", NULL },
  { "replace", 'r', 0, G_OPTION_ARG_NONE, &opt_replace, "Replace a running instance", NULL },
  { "version", 0, 0, G_OPTION_ARG_NONE, &show_version, "Show program version.", NULL},
  { NULL }
};

static void
message_handler (const gchar *log_domain,
                 GLogLevelFlags log_level,
                 const gchar *message,
                 gpointer user_data)
{
  /* Make this look like normal console output */
  if (log_level & G_LOG_LEVEL_DEBUG)
    printf ("XDP: %s\n", message);
  else
    printf ("%s: %s\n", g_get_prgname (), message);
}

static void
printerr_handler (const gchar *string)
{
  int is_tty = isatty (1);
  const char *prefix = "";
  const char *suffix = "";
  if (is_tty)
    {
      prefix = "\x1b[31m\x1b[1m"; /* red, bold */
      suffix = "\x1b[22m\x1b[0m"; /* bold off, color reset */
    }
  fprintf (stderr, "%serror: %s%s\n", prefix, suffix, string);
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  GError *error = NULL;

  if ((CINNAMON_MODE || XFCE_MODE) && !screenshot_init (connection, &error))
    {
      g_warning ("error: %s\n", error->message);
      g_clear_error (&error);
    }

  if (!settings_init (connection, &error))
    {
      g_warning ("error: %s\n", error->message);
      g_clear_error (&error);
    }

  if (!inhibit_init (connection, &error))
    {
      g_warning ("error: %s\n", error->message);
      g_clear_error (&error);
    }

  if ((CINNAMON_MODE || MATE_MODE) && !lockdown_init (connection, &error))
    {
      g_warning ("error: %s\n", error->message);
      g_clear_error (&error);
    }

  if (!wallpaper_init (connection, &error))
    {
      g_warning ("error: %s\n", error->message);
      g_clear_error (&error);
    }
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  g_debug ("org.freedesktop.impl.portal.desktop.xapp acquired");
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  g_debug ("name lost");
  g_main_loop_quit (loop);
}

int
main (int argc, char *argv[])
{
  guint owner_id;
  g_autoptr(GError) error = NULL;
  GDBusConnection  *session_bus;
  g_autoptr(GOptionContext) context = NULL;

  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  /* Avoid pointless and confusing recursion */
  g_unsetenv ("GTK_USE_PORTAL");
  g_setenv ("ADW_DISABLE_PORTAL", "1", TRUE);
  g_setenv ("GSK_RENDERER", "cairo", TRUE);

  gtk_init (&argc, &argv);

  context = g_option_context_new ("- portal backends");
  g_option_context_set_summary (context,
      "A backend implementation for xdg-desktop-portal.");
  g_option_context_set_description (context,
      "xdg-desktop-portal-xapp provides D-Bus interfaces that\n"
      "are used by xdg-desktop-portal to implement portals in Cinnamon, MATE or Xfce\n"
      "\n"
      "Documentation for the available D-Bus interfaces can be found at\n"
      "https://flatpak.github.io/xdg-desktop-portal/portal-docs.html\n"
      "\n"
      "Please report issues at https://github.com/linuxmint/xdg-desktop-portal-xapp/issues");
  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s: %s", g_get_application_name (), error->message);
      g_printerr ("\n");
      g_printerr ("Try \"%s --help\" for more information.",
                  g_get_prgname ());
      g_printerr ("\n");
      return 1;
    }

  if (show_version)
    {
      g_print (PACKAGE_STRING "\n");
      return 0;
    }

  if (desktop_arg)
  {
    if (!g_strv_contains (desktops, desktop_arg))
    {
        g_printerr ("Desktop argument must be cinnamon, mate or xfce\n");
        return 1;
    }

    mode = desktop_arg;
  }
  else
  {
      const gchar *xdg_desktop = g_getenv ("XDG_CURRENT_DESKTOP");

      if (g_strcmp0 (xdg_desktop, "X-Cinnamon") == 0)
          mode = "cinnamon";
      else if (g_strcmp0 (xdg_desktop, "MATE") == 0)
          mode = "mate";
      else if (g_strcmp0 (xdg_desktop, "XFCE") == 0)
          mode = "xfce";
      else
      {
          g_printerr ("Current desktop (XDG_CURRENT_DESKTOP) is unsupported: %s\n", xdg_desktop);
          return 1;
      }
  }


  g_set_printerr_handler (printerr_handler);

  if (opt_verbose)
    g_log_set_handler (NULL, G_LOG_LEVEL_DEBUG, message_handler, NULL);

  g_set_prgname ("xdg-desktop-portal-xapp");

  loop = g_main_loop_new (NULL, FALSE);

  outstanding_handles = g_hash_table_new (g_str_hash, g_str_equal);

  session_bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (session_bus == NULL)
    {
      g_printerr ("No session bus: %s\n", error->message);
      return 2;
    }

  owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                             "org.freedesktop.impl.portal.desktop.xapp",
                             G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT | (opt_replace ? G_BUS_NAME_OWNER_FLAGS_REPLACE : 0),
                             on_bus_acquired,
                             on_name_acquired,
                             on_name_lost,
                             NULL,
                             NULL);

  g_main_loop_run (loop);

  g_bus_unown_name (owner_id);

  return 0;
}
