/*
 * This program is free software; you can redistribute it and/or modify it under the terms of the 
 * GNU General Public License as published by the Free Software Foundation; You may only use 
 * version 2 of the License, you have no option to use any other version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; 
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See 
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program; if 
 * not, write to the Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * ------------------------------------------------------------------------------------------------- 
 */
#ifndef KEYFILE_H
#define KEYFILE_H

gboolean key_file_load_from_file(const gchar *file);

void key_file_close(void);

gboolean key_file_has_group(const gchar *group_name);

gchar *key_file_get_string_default(const gchar      *group_name,
                                   const gchar      *key,
                                   const gchar      *default_value);

gint key_file_get_integer_default(const gchar       *group_name,
                                  const gchar       *key,
                                  gint              default_value);

#endif /* KEYFILE_H */
