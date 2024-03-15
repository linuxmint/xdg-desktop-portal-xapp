/*
 * Copyright © 2018 Igalia S.L.
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
 *       Patrick Griffis <pgriffis@igalia.com>
 */

#include <config.h>

#include <time.h>
#include <string.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include "settings.h"
#include "utils.h"

#include "xdg-desktop-portal-dbus.h"

static GHashTable *settings_table;

typedef struct {
  GSettingsSchema *schema;
  GSettings *settings;
} SettingsBundle;

static SettingsBundle *
settings_bundle_new (GSettingsSchema *schema,
                     GSettings       *settings)
{
  SettingsBundle *bundle = g_new (SettingsBundle, 1);
  bundle->schema = schema;
  bundle->settings = settings;
  return bundle;
}

static void
settings_bundle_free (SettingsBundle *bundle)
{
  g_object_unref (bundle->schema);
  g_object_unref (bundle->settings);
  g_free (bundle);
}

// static GVariant *get_gtk_theme (gpointer data);
static GVariant *get_accent_color (gpointer data);
static GVariant *get_color_scheme (gpointer data);
static GVariant *get_high_contrast (gpointer data);

#define XAPP_PORTAL_INTERFACE_SCHEMA "org.x.apps.portal"
#define CINNAMON_DESKTOP_INTERFACE_SCHEMA "org.cinnamon.desktop.interface"

typedef GVariant* (* LookupFunc) (gpointer data);

const gchar *COLOR_SCHEME_NAMES[] = {
    "default",
    "prefer-dark",
    "prefer-light",
    NULL
};

typedef struct
{
  const gchar *portal_ns;
  const gchar *portal_key;
  const gchar *gs_schema_id;
  const gchar *gs_key;
  LookupFunc lookup_func;
} SettingDefinition;

// ******** KEEP THE NAMESPACES GROUPED TOGETHER. See settings_handle_read_all () *******
static const SettingDefinition setting_defs[] = {
    { "org.freedesktop.appearance",       "contrast",             XAPP_PORTAL_INTERFACE_SCHEMA,               "high-contrast",        get_high_contrast },
    { "org.freedesktop.appearance",       "color-scheme",         XAPP_PORTAL_INTERFACE_SCHEMA,               "color-scheme",         get_color_scheme },
    { "org.freedesktop.appearance",       "accent-color",         XAPP_PORTAL_INTERFACE_SCHEMA,               "accent-rgb",           get_accent_color },
    // { "org.gnome.desktop.interface",      "gtk-theme",            CINNAMON_DESKTOP_INTERFACE_SCHEMA,          "gtk-theme",            get_gtk_theme }
};

typedef struct
{
    const gchar *theme_name;
    const gchar *accent_color;
} ColorMatch;

// Instead (or as another fallback) we could get GtkStyleContext and
// Run gtk_style_context_lookup_color() with a reliable color name.
static const ColorMatch colors[] = {
    { "Mint-X",             "#9ab87x"},
    { "Mint-X-Aqua",        "#6cabcd"},
    { "Mint-X-Blue",        "#5b73c4"},
    { "Mint-X-Brown",       "#aa876a"},
    { "Mint-X-Grey",        "#9d9d9d"},
    { "Mint-X-Orange",      "#db9d61"},
    { "Mint-X-Pink",        "#c76199"},
    { "Mint-X-Purple",      "#8c6ec9"},
    { "Mint-X-Red",         "#c15b58"},
    { "Mint-X-Sand",        "#c8ac69"},
    { "Mint-X-Teal",        "#5aaa9a"},

    { "Mint-X-Dark",        "#accd8a"},
    { "Mint-X-Dark-Aqua",   "#6cabcd"},
    { "Mint-X-Dark-Blue",   "#5b73c4"},
    { "Mint-X-Dark-Brown",  "#aa876a"},
    { "Mint-X-Dark-Grey",   "#9d9d9d"},
    { "Mint-X-Dark-Orange", "#db9d61"},
    { "Mint-X-Dark-Pink",   "#c76199"},
    { "Mint-X-Dark-Purple", "#8c6ec9"},
    { "Mint-X-Dark-Red",    "#c15b58"},
    { "Mint-X-Dark-Sand",   "#c8ac69"},
    { "Mint-X-Dark-Teal",   "#5aaa9a"},

    { "Mint-Y",             "#35a854"},
    { "Mint-Y-Aqua",        "#1f9ede"},
    { "Mint-Y-Blue",        "#0c75de"},
    { "Mint-Y-Grey",        "#70737a"},
    { "Mint-Y-Orange",      "#ff7139"},
    { "Mint-Y-Pink",        "#e54980"},
    { "Mint-Y-Purple",      "#8c5dd9"},
    { "Mint-Y-Red",         "#e82127"},
    { "Mint-Y-Sand",        "#c5a07c"},
    { "Mint-Y-Teal",        "#199ca8"},

    { "Mint-Y-Dark",        "#35a854"},
    { "Mint-Y-Dark-Aqua",   "#1f9ede"},
    { "Mint-Y-Dark-Blue",   "#0c75de"},
    { "Mint-Y-Dark-Grey",   "#70737a"},
    { "Mint-Y-Dark-Orange", "#ff7139"},
    { "Mint-Y-Dark-Pink",   "#e54980"},
    { "Mint-Y-Dark-Purple", "#8c5dd9"},
    { "Mint-Y-Dark-Red",    "#e82127"},
    { "Mint-Y-Dark-Sand",   "#c5a07c"},
    { "Mint-Y-Dark-Teal",   "#199ca8"},
};

static gboolean
namespace_matches (const char         *namespace,
                   const char * const *patterns)
{
  size_t i;

  for (i = 0; patterns[i]; ++i)
    {
      size_t pattern_len;
      const char *pattern = patterns[i];

      if (pattern[0] == '\0')
        return TRUE;
      if (strcmp (namespace, pattern) == 0)
        return TRUE;

      pattern_len = strlen (pattern);
      if (pattern[pattern_len - 1] == '*' && strncmp (namespace, pattern, pattern_len - 1) == 0)
        return TRUE;
    }

  if (i == 0) /* Empty array */
    return TRUE;

  return FALSE;
}

// static GVariant *
// get_gtk_theme (gpointer data)
// {
//     SettingDefinition *def = (SettingDefinition *) data;
//     SettingsBundle *bundle = g_hash_table_lookup (settings_table, def->gs_schema_id);

//     GtkSettings *gtk_settings = gtk_settings_get_default ();
//     g_autofree gchar *theme_name = NULL;

//     g_object_get (gtk_settings,
//                   "gtk-theme-name", &theme_name,
//                   NULL);

//     return g_variant_new_string (theme_name);
// }

static GVariant *
get_color_scheme (gpointer data)
{
    SettingDefinition *def = (SettingDefinition *) data;
    SettingsBundle *bundle = g_hash_table_lookup (settings_table, def->gs_schema_id);
    if (bundle != NULL && g_settings_schema_has_key (bundle->schema, "color-scheme"))
    {
        int color_scheme;

        color_scheme = g_settings_get_enum (bundle->settings, "color-scheme");
        g_debug ("Color scheme: %s", COLOR_SCHEME_NAMES[color_scheme]);
        return g_variant_new_uint32 (color_scheme); /* No preference */
    }

    g_debug ("Using default color scheme");
    return g_variant_new_uint32 (COLOR_SCHEME_DEFAULT); /* No preference */
}

static GVariant *
get_high_contrast (gpointer data)
{
    SettingDefinition *def = (SettingDefinition *) data;
    SettingsBundle *bundle = g_hash_table_lookup (settings_table, def->gs_schema_id);

    if (bundle != NULL && g_settings_schema_has_key (bundle->schema, "high-contrast"))
    {
        gboolean high_contrast;

        high_contrast = g_settings_get_boolean (bundle->settings, "high-contrast");
        g_debug ("High contrast: %s", high_contrast ? "true" : "false");
        return g_variant_new_uint32 (high_contrast ? 1 : 0);
    }

    g_debug ("Using default high contrast: false");
    return g_variant_new_uint32 (0); /* No preference */
}

static GVariant *
get_accent_color (gpointer data)
{
    SettingDefinition *def = (SettingDefinition *) data;
    SettingsBundle *bundle = g_hash_table_lookup (settings_table, def->gs_schema_id);
    GtkSettings *gtk_settings = gtk_settings_get_default ();
    g_autofree gchar *theme_name = NULL;
    g_autofree gchar *out_color = NULL;
    gboolean prefer_dark;
    GdkRGBA rgba;
    gint i;

    g_object_get (gtk_settings,
                  "gtk-theme-name", &theme_name,
                  "gtk-application-prefer-dark-theme", &prefer_dark,
                  NULL);

    g_debug ("Looking up the accent color for gtk theme '%s'", theme_name);

    for (i = 0; i < G_N_ELEMENTS (colors); i++)
    {
        ColorMatch match = colors[i];

        if (g_strcmp0 (theme_name, match.theme_name) == 0)
        {
            out_color = g_strdup (match.accent_color);
        }
    }

    if (out_color == NULL)
    {
        g_debug ("No matching theme found for accent-rgb setting, checking gsettings");

        if (bundle != NULL && g_settings_schema_has_key (bundle->schema, "accent-rgb"))
        {
            out_color = g_settings_get_string (bundle->settings, "accent-rgb");
        }
    }

    if (out_color != NULL && gdk_rgba_parse (&rgba, out_color))
    {
        g_debug ("Using accent color: '%s' (r%.3f, g%.3f, b%.3f)", out_color, rgba.red, rgba.green, rgba.blue);
        return g_variant_new ("(ddd)", rgba.red, rgba.green, rgba.blue);
    }

    g_debug ("No accent color");
    return g_variant_new ("(ddd)", -1.0, -1.0, -1.0); /* Out of 0-1.0 range = no preference */
}

static gboolean
settings_handle_read_all (XdpImplSettings       *object,
                          GDBusMethodInvocation *invocation,
                          const char * const    *arg_namespaces,
                          gpointer               data)
{
    g_autoptr(GVariantBuilder) builder = g_variant_builder_new (G_VARIANT_TYPE ("(a{sa{sv}})"));
    gint i;

    // This goes through our setting_defs and assumes that a portal namespace change
    // means there will be no more entries with the previous namespace further down setting_defs.

    g_variant_builder_open (builder, G_VARIANT_TYPE ("a{sa{sv}}"));

    GVariantDict dict;
    const gchar *current_ns = NULL;

    for (i = 0; i < G_N_ELEMENTS (setting_defs); i++)
    {
        if (!namespace_matches (setting_defs[i].portal_ns, arg_namespaces))
          continue;

        if (g_strcmp0 (current_ns, setting_defs[i].portal_ns) != 0)
        {
            if (current_ns != NULL)
            {
                g_variant_builder_add (builder, "{s@a{sv}}", current_ns, g_variant_dict_end (&dict));
            }

            current_ns = setting_defs[i].portal_ns;
            g_variant_dict_init (&dict, NULL);
        }

        g_variant_dict_insert_value (&dict, setting_defs[i].portal_key, setting_defs[i].lookup_func ((gpointer) &setting_defs[i]));
  }

  if (current_ns != NULL)
  {
    g_variant_builder_add (builder, "{s@a{sv}}", current_ns, g_variant_dict_end (&dict));
  }

  g_variant_builder_close (builder);
  g_dbus_method_invocation_return_value (invocation, g_variant_builder_end (builder));

  return TRUE;
}

static gboolean
settings_handle_read (XdpImplSettings       *object,
                      GDBusMethodInvocation *invocation,
                      const char            *arg_namespace,
                      const char            *arg_key,
                      gpointer               user_data)
{
    g_debug ("Read %s %s", arg_namespace, arg_key);
    GVariant *out = NULL;
    gint i;

    for (i = 0; i < G_N_ELEMENTS (setting_defs); i++)
    {
        if (g_strcmp0 (arg_namespace, setting_defs[i].portal_ns) == 0 &&
            g_strcmp0 (arg_key, setting_defs[i].portal_key) == 0)
        {
            out = g_variant_new ("(v)", setting_defs[i].lookup_func ((gpointer) &setting_defs[i]));
            g_dbus_method_invocation_return_value (invocation, out);
            break;
        }
    }

    if (out == NULL)
    {
        g_debug ("Attempted to read unknown namespace/key pair: %s %s", arg_namespace, arg_key);
        g_dbus_method_invocation_return_error_literal (invocation, XDG_DESKTOP_PORTAL_ERROR,
                                                       XDG_DESKTOP_PORTAL_ERROR_NOT_FOUND,
                                                       _("Requested setting not found"));
    }

    return TRUE;
}

static void
on_settings_changed (GSettings             *gsettings,
                     const char            *key,
                     gpointer               user_data)
{
    XdpImplSettings *xdg_settings = XDP_IMPL_SETTINGS (user_data);
    gchar *schema_id;
    gint i;

    g_object_get (gsettings, "schema-id", &schema_id, NULL);
    g_debug ("Emitting changed for %s %s", schema_id, key);

    for (i = 0; i < G_N_ELEMENTS (setting_defs); i++)
    {
        if (g_strcmp0 (schema_id, setting_defs[i].gs_schema_id) == 0 &&
            g_strcmp0 (key, setting_defs[i].gs_key) == 0)
        {
            GVariant *out = g_variant_new ("v", setting_defs[i].lookup_func ((gpointer) &setting_defs[i]));
            xdp_impl_settings_emit_setting_changed (xdg_settings, setting_defs[i].portal_ns, setting_defs[i].portal_key, out);
            break;
        }
    }
}

static void
init_settings_table (XdpImplSettings *settings,
                     GHashTable      *table)
{
    size_t i;
    GSettingsSchemaSource *source = g_settings_schema_source_get_default ();

    for (i = 0; i < G_N_ELEMENTS (setting_defs); i++)
    {
        GSettings *setting;
        GSettingsSchema *schema;
        SettingsBundle *bundle;
        schema = g_settings_schema_source_lookup (source, setting_defs[i].gs_schema_id, TRUE);
        if (!schema)
        {
            g_debug ("%s schema not found", setting_defs[i].gs_schema_id);
            continue;
        }

        if (g_hash_table_contains (settings_table, setting_defs[i].gs_schema_id))
        {
            g_debug ("Already monitoring %s", setting_defs[i].gs_schema_id);
            continue;
        }
        g_debug ("Initing GSettings for '%s'", setting_defs[i].gs_schema_id);
        setting = g_settings_new (setting_defs[i].gs_schema_id);
        bundle = settings_bundle_new (schema, setting);
        g_signal_connect (setting, "changed", G_CALLBACK(on_settings_changed), settings);
        g_hash_table_insert (settings_table, (gpointer) setting_defs[i].gs_schema_id, bundle);
    }

    g_settings_schema_source_unref (source);
}

gboolean
settings_init (GDBusConnection  *bus,
               GError          **error)
{
  GDBusInterfaceSkeleton *helper;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_settings_skeleton_new ());

  g_object_set (helper, "version", 2, NULL);

  g_signal_connect (helper, "handle-read", G_CALLBACK (settings_handle_read), NULL);
  g_signal_connect (helper, "handle-read-all", G_CALLBACK (settings_handle_read_all), NULL);

  settings_table= g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify) settings_bundle_free);

  init_settings_table (XDP_IMPL_SETTINGS (helper), settings_table);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;

}
