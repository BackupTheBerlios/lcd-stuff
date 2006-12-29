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
#ifndef KEYFILE_H
#define KEYFILE_H

gboolean key_file_load_from_file(        const gchar     *file);

void key_file_close(                     void);

gboolean key_file_has_group(             const gchar     *group_name);

gchar *key_file_get_string_default(      const gchar     *group_name,
                                         const gchar     *key,
                                         const gchar     *default_value);

gchar *key_file_get_string(              const gchar     *group_name,
                                         const gchar     *key);

gint key_file_get_integer_default(       const gchar     *group_name,
                                         const gchar     *key,
                                         gint            default_value);

gchar **key_file_get_string_list(        const gchar     *group_name,
                                         const gchar     *key,
                                         gsize           *length);

gchar **key_file_get_string_list_default(const gchar     *group_name,
                                         const gchar     *key,
                                         const gchar     *default_value,
                                         gsize           *length);

#endif /* KEYFILE_H */

/* vim: set ts=4 sw=4 et: */
