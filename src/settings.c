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

#include "settings.h"
#include "utils.h"

#include "xdg-desktop-portal-dbus.h"

#define XAPP_PORTAL_INTERFACE_SCHEMA "org.x.apps.portal"

static GHashTable *settings;
static const gchar *used_interface_schema = NULL;

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

static GVariant *
get_color_scheme (void)
{
  SettingsBundle *bundle = g_hash_table_lookup (settings, used_interface_schema);
  int color_scheme;

  if (!g_settings_schema_has_key (bundle->schema, "color-scheme"))
    return g_variant_new_uint32 (0); /* No preference */

  color_scheme = g_settings_get_enum (bundle->settings, "color-scheme");

  return g_variant_new_uint32 (color_scheme);
}

// static GVariant *
// get_high_contrast (void)
// {
//   SettingsBundle *bundle = g_hash_table_lookup (settings, used_interface_schema);
//   gboolean high_contrast;

//   if (!g_settings_schema_has_key (bundle->schema, "high-contrast"))
//     return g_variant_new_boolean (FALSE);

//   high_contrast = g_settings_get_boolean (bundle->settings, "high-contrast");

//   return g_variant_new_boolean (high_contrast);
// }

static gboolean
settings_handle_read_all (XdpImplSettings       *object,
                          GDBusMethodInvocation *invocation,
                          const char * const    *arg_namespaces,
                          gpointer               data)
{
  g_autoptr(GVariantBuilder) builder = g_variant_builder_new (G_VARIANT_TYPE ("(a{sa{sv}})"));

  g_variant_builder_open (builder, G_VARIANT_TYPE ("a{sa{sv}}"));

  if (namespace_matches ("org.freedesktop.appearance", arg_namespaces))
    {
      GVariantDict dict;

      g_variant_dict_init (&dict, NULL);
      g_variant_dict_insert_value (&dict, "color-scheme", get_color_scheme ());

      g_variant_builder_add (builder, "{s@a{sv}}", "org.freedesktop.appearance", g_variant_dict_end (&dict));
    }

  // if (namespace_matches ("org.gnome.desktop.a11y.interface", arg_namespaces))
  //   {
  //     GVariantDict dict;

  //     g_variant_dict_init (&dict, NULL);
  //     g_variant_dict_insert_value (&dict, "high-contrast", get_high_contrast ());

  //     g_variant_builder_add (builder, "{s@a{sv}}", "org.gnome.desktop.a11y.interface", g_variant_dict_end (&dict));
  //   }

  g_variant_builder_close (builder);

  g_dbus_method_invocation_return_value (invocation, g_variant_builder_end (builder));

  return TRUE;
}

static gboolean
settings_handle_read (XdpImplSettings       *object,
                      GDBusMethodInvocation *invocation,
                      const char            *arg_namespace,
                      const char            *arg_key,
                      gpointer               data)
{
  g_debug ("Read %s %s", arg_namespace, arg_key);

  if (strcmp (arg_namespace, "org.freedesktop.appearance") == 0 &&
           strcmp (arg_key, "color-scheme") == 0)
    {
      g_dbus_method_invocation_return_value (invocation,
                                             g_variant_new ("(v)", get_color_scheme ()));
      return TRUE;
    }
  // else
  // if (strcmp (arg_namespace, "org.gnome.desktop.a11y.interface") == 0 &&
  //          strcmp (arg_key, "high-contrast") == 0)
  //   {
  //     g_dbus_method_invocation_return_value (invocation,
  //                                            g_variant_new ("(v)", get_high_contrast ()));
  //     return TRUE;
  //   }

  g_debug ("Attempted to read unknown namespace/key pair: %s %s", arg_namespace, arg_key);
  g_dbus_method_invocation_return_error_literal (invocation, XDG_DESKTOP_PORTAL_ERROR,
                                                 XDG_DESKTOP_PORTAL_ERROR_NOT_FOUND,
                                                 _("Requested setting not found"));

  return TRUE;
}

typedef struct {
  XdpImplSettings *self;
  const char *namespace;
} ChangedSignalUserData;

static ChangedSignalUserData *
changed_signal_user_data_new (XdpImplSettings *settings,
                              const char      *namespace)
{
  ChangedSignalUserData *data = g_new (ChangedSignalUserData, 1);
  data->self = settings;
  data->namespace = namespace;
  return data;
}

static void
changed_signal_user_data_destroy (gpointer  data,
                                  GClosure *closure)
{
  g_free (data);
}

static void
on_settings_changed (GSettings             *settings,
                     const char            *key,
                     ChangedSignalUserData *user_data)
{
  g_autoptr (GVariant) new_value = g_settings_get_value (settings, key);

  g_debug ("Emitting changed for %s %s", user_data->namespace, key);

  if (strcmp (user_data->namespace, used_interface_schema) == 0 &&
      strcmp (key, "color-scheme") == 0)
  {
    xdp_impl_settings_emit_setting_changed (user_data->self,
                                            "org.freedesktop.appearance", key,
                                            g_variant_new ("v", get_color_scheme ()));
  }

}

static void
init_settings_table (XdpImplSettings *settings,
                     GHashTable      *table)
{
    const gchar *schemas[1] = { 0 };

    schemas[0] = XAPP_PORTAL_INTERFACE_SCHEMA;
    used_interface_schema = XAPP_PORTAL_INTERFACE_SCHEMA;

    size_t i;
    GSettingsSchemaSource *source = g_settings_schema_source_get_default ();

    for (i = 0; i < G_N_ELEMENTS (schemas); ++i)
    {
        GSettings *setting;
        GSettingsSchema *schema;
        SettingsBundle *bundle;
        const char *schema_name = schemas[i];

        schema = g_settings_schema_source_lookup (source, schema_name, TRUE);
        if (!schema)
        {
            g_debug ("%s schema not found", schema_name);
            continue;
        }

        setting = g_settings_new (schema_name);
        bundle = settings_bundle_new (schema, setting);
        g_signal_connect_data (setting, "changed", G_CALLBACK(on_settings_changed),
                               changed_signal_user_data_new (settings, schema_name),
                               changed_signal_user_data_destroy, 0);
        g_hash_table_insert (table, (gchar *) schema_name, bundle);
    }
}

gboolean
settings_init (GDBusConnection  *bus,
               GError          **error)
{
  GDBusInterfaceSkeleton *helper;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_settings_skeleton_new ());

  g_signal_connect (helper, "handle-read", G_CALLBACK (settings_handle_read), NULL);
  g_signal_connect (helper, "handle-read-all", G_CALLBACK (settings_handle_read_all), NULL);

  settings = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify) settings_bundle_free);

  init_settings_table (XDP_IMPL_SETTINGS (helper), settings);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;

}
