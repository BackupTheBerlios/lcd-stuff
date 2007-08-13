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
#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <glib.h>

/*
 * string functions ------------------------------------------------------------
 */

void string_canon_init(void);
char *string_canon(char *string);
char *format_time(unsigned long seconds);
char *format_bytes(unsigned long long bytes);
void string_replace(char *string, char from, char to);
GString *stringbuffer_wrap(GString *buffer, int length, int maxlines);
char *stringbuffer_get_line(GString *buffer, int line);
int stringbuffer_get_lines(GString *buffer);

/*
 * file walking functions, replacement for ftw ---------------------------------
 */

typedef enum {
    FWF_NO_FLAGS            = 0,
    FWF_INCLUDE_DIRS        = 1 << 0,
    FWF_NO_RECURSION        = 1 << 1,
    FWF_INCLUDE_DEAD_LINK   = 1 << 2,
    FWF_NO_SYMLINK_FOLLOW   = 1 << 3
} FilewalkFlags;

typedef bool (*filewalk_function)(const char    *filename, 
                                  void          *cookie,
                                  GError        **err);

bool filewalk(const char            *basedir, 
              filewalk_function     fn,
              void                  *cookie, 
              FilewalkFlags         flags,
              GError                **err);

bool delete_directory_recursively(const char *directory, GError **err);

/*
 * Other file functions --------------------------------------------------------
 */

long copy_file(const char       *src_name,
               const char       *dest_dir,
               const char       *dest_name,
               GError           **gerr);

/*
 * disk functions --------------------------------------------------------------
 */

unsigned long long get_free_bytes(const char *path, GError **err);

#endif /* UTIL_H */

/* vim: set ts=4 sw=4 et: */
