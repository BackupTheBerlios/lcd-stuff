/*
 * This file is part of lcd-stuff.
 *
 * lcd-stuff is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * lcd-stuff is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with lcd-stuff; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#include <glib.h>

#include "keyfile.h"

static GKeyFile *s_key_file;

/* -------------------------------------------------------------------------- */
gboolean key_file_load_from_file(const gchar *file)
{
    s_key_file = g_key_file_new();
    return g_key_file_load_from_file(s_key_file, file, G_KEY_FILE_NONE, NULL);
}

/* -------------------------------------------------------------------------- */
void key_file_close(void)
{
    g_key_file_free(s_key_file);
    s_key_file = NULL;
}

/* -------------------------------------------------------------------------- */
gboolean key_file_has_group(const gchar *group_name)
{
    return g_key_file_has_group(s_key_file, group_name);
}

/* -------------------------------------------------------------------------- */
gchar *key_file_get_string_default_l1(const gchar      *group_name,
                                      const gchar      *key,
                                      const gchar      *default_value)
{
	gchar *string;
	gchar *ret;

	string = key_file_get_string_default(group_name, key, default_value);

	ret = g_convert_with_fallback(string, -1, "iso-8859-1", "utf-8",
			"?", NULL, NULL, NULL);
	if (!ret) {
		ret = g_strdup(default_value);
	}

	g_free(string);

	return ret;
}

/* -------------------------------------------------------------------------- */
gchar *key_file_get_string_default(const gchar      *group_name,
                                   const gchar      *key,
                                   const gchar      *default_value)
{
    gchar *ret;

    ret = g_key_file_get_string(s_key_file, group_name, key, NULL);
    if (!ret) {
        ret = default_value ? g_strdup(default_value) : NULL;
    }

    return ret;
}

/* -------------------------------------------------------------------------- */
gchar *key_file_get_string(const gchar      *group_name,
                           const gchar      *key)
{
    return g_key_file_get_string(s_key_file, group_name, key, NULL);
}

/* -------------------------------------------------------------------------- */
gint key_file_get_integer_default(const gchar       *group_name,
                                  const gchar       *key,
                                  gint              default_value)
{
    GError *err = NULL;
    gint   ret;

    ret = g_key_file_get_integer(s_key_file, group_name, key, &err);
    if (err) {
        g_error_free(err);
        ret = default_value;
    }

    return ret;
}

/* -------------------------------------------------------------------------- */
gchar **key_file_get_string_list_default(const gchar        *group_name,
                                         const gchar        *key,
                                         const gchar        *default_value,
                                         gsize              *length)
{
    gchar  **ret = NULL;
    GError *err = NULL;

    g_assert(length != NULL);
    g_assert(default_value != NULL);

    ret = g_key_file_get_string_list(s_key_file, group_name, key, length, &err);
    if (err) {
        char **tmp;

        g_error_free(err);

        /* parse default value */
        ret = g_strsplit(default_value, ";", 100);
        tmp = ret;
        while (*tmp++ != NULL) {
            (*length)++;
        }
    }

    return ret;
}

/* -------------------------------------------------------------------------- */
gchar **key_file_get_string_list(const gchar        *group_name,
                                 const gchar        *key,
                                 gsize              *length)
{
    gchar  **ret;
    GError *err = NULL;

    g_assert(length != NULL);

    ret = g_key_file_get_string_list(s_key_file, group_name, key, length, &err);
    if (err) {
        g_error_free(err);
        ret = NULL;
        *length = 0;
    }

    return ret;
}

