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
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdbool.h>
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#ifdef HAVE_SYS_VFS_H
#  include <sys/vfs.h>
#endif
#ifdef HAVE_SYS_MOUNT_H
#  include <sys/mount.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#  include <sys/param.h>
#endif
#include <sys/types.h>
#include <fcntl.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "util.h"
#include "global.h"

#define BYTES_PER_KBYTE (1024)
#define BYTES_PER_MBYTE (1024*BYTES_PER_KBYTE)
#define BYTES_PER_GBYTE (1024*BYTES_PER_MBYTE)

/* ---------------------- static variables ---------------------------------- */
static char s_valid_chars[256];

/* -------------------------------------------------------------------------- */
void string_canon_init(void)
{
    int i, character;

    /*
     * init the list of valid chars, we don't use utf-8 or any other
     * multibyte encoding
     */
    for (character = (int)' ', i = 0; character <= 255; character++) {
        if (character != '{' && character != '}' && character != '\\') {
            s_valid_chars[i++] = character;
        }
    }

    s_valid_chars[i] = '\0';
}

/* -------------------------------------------------------------------------- */
char *format_time(unsigned long seconds)
{
    unsigned long sec_disp;
    unsigned long min_disp;

    sec_disp = seconds % 60;
    min_disp = seconds / 60;

    return g_strdup_printf("%lu:%02lu", min_disp, sec_disp);
}

/* -------------------------------------------------------------------------- */
char *format_bytes(unsigned long long bytes)
{
    if (bytes > BYTES_PER_GBYTE)
        return g_strdup_printf("%.2f GB", (double)bytes / BYTES_PER_GBYTE);
    else if (bytes > BYTES_PER_MBYTE)
        return g_strdup_printf("%.2f MB", (double)bytes / BYTES_PER_MBYTE);
    else if (bytes > BYTES_PER_KBYTE)
        return g_strdup_printf("%.2f KB", (double)bytes / BYTES_PER_KBYTE);
    else
        return g_strdup_printf("%llu B", bytes);
}

/* -------------------------------------------------------------------------- */
char *string_canon(char *string)
{
    return g_strcanon(string, s_valid_chars, '?');
}

/* -------------------------------------------------------------------------- */
void string_replace(char *string, char from, char to)
{
    char *cur = string;

    while (*cur++ != 0)
        if (*cur == from)
            *cur = to;
}

/* -------------------------------------------------------------------------- */
static GString *stringbuffer_wrap_simple(GString     *buffer,
                                        int         length,
                                        int         maxlines)
{
    GString *new_str;
    char    *remainder;
    int     cur_line = 0;

    if (!buffer)
        return NULL;

    new_str = g_string_new(buffer->str);
    if (!new_str)
        return NULL;

    remainder = new_str->str;

    while (strlen(remainder) > length) {
        int pos = length * (cur_line+1) + cur_line;
        g_string_insert_c(new_str, pos, '\n');
        remainder += length + 1;

        if (remainder[0] == ' ')
            g_string_erase(new_str, pos+1, 1);

        cur_line++;
    }

    return new_str;
}

/* -------------------------------------------------------------------------- */
static inline char *strchr2(const char *string, char a, char b)
{
    char *tmp1, *tmp2;
    tmp1 = strchr(string, a);
    tmp2 = strchr(string, b);

    if (tmp1 == NULL && tmp2 == NULL)
        return NULL;
    else if (tmp1 == NULL)
        return tmp2;
    else if (tmp2 == NULL)
        return tmp1;
    else
        return min(tmp1, tmp2);
}

/* -------------------------------------------------------------------------- */
GString *stringbuffer_wrap_spaces(GString     *buffer,
                                  int         length,
                                  int         maxlines)
{
    GString *new_str;
    char    *remainder;
    int     cur_line = 0;

    if (!buffer)
        return NULL;

    new_str = g_string_new(buffer->str);
    if (!new_str)
        return NULL;

    remainder = new_str->str;

    while (strlen(remainder) > length) {
        char *new = remainder, *old = remainder;

        while (new && (new - remainder <= length)) {
            old = new;
            new = strchr2(new + 1, ' ', '-');
            if (new && *new == '-')
                new++;
        }

        /* for the very rare case that there's no space */
        if (old == remainder) {
            g_string_insert_c(new_str, remainder - new_str->str + length, '\n');
            remainder += length + 1;
        } else {
            if (*(old-1) == '-')
                g_string_insert_c(new_str, old - new_str->str, '\n');
            else
                *old = '\n';

            remainder += old - remainder + 1;

            if (remainder[0] == ' ')
                g_string_erase(new_str, old - new_str->str + 1, 1);
        }

        cur_line++;
    }

    return new_str;
}

/* -------------------------------------------------------------------------- */
GString *stringbuffer_wrap(GString *buffer, int length, int maxlines)
{
    if (length * maxlines < buffer->len)
        return stringbuffer_wrap_simple(buffer, length, maxlines);
    else {
        GString *try = NULL;

        try = stringbuffer_wrap_spaces(buffer, length, maxlines);
        if (stringbuffer_get_lines(try) <= maxlines) {
            return try;
        } else {
            g_string_free(try, true);
            return stringbuffer_wrap_simple(buffer, length, maxlines);
        }
    }
}

/* -------------------------------------------------------------------------- */
char *lcd_stuff_strndup(const char *s, size_t n)
{
    size_t len = strlen(s);
    char *ret;

    if (len <= n)
       return strdup(s);

    ret = malloc(n + 1);
    strncpy(ret, s, n);
    ret[n] = '\0';

    return ret;
}


/* -------------------------------------------------------------------------- */
char *stringbuffer_get_line(GString *buffer, int line)
{
    char *ret = buffer->str;
    char *next_line = ret;

    /* 0 = first line */

    while (line >= 0 && next_line) {
        line--;
        ret = next_line;
        next_line = strchr(ret, '\n');

        /* strip the newline itself */
        if (next_line) {
            next_line++;

            if (*next_line == '\0')
                break;
        }
    }

    if (line == -1) {
        if (next_line)
            ret = lcd_stuff_strndup(ret, next_line - ret - 1);
        else
            ret = strdup(ret);

        return ret;
    }

    return NULL;
}

/* -------------------------------------------------------------------------- */
int stringbuffer_get_lines(GString *buffer)
{
    char *tmp;
    int count = 0;

    if (!buffer)
        return -EINVAL;

    tmp = buffer->str;

    while (tmp) {
        tmp = strchr(tmp, '\n');

        /* strip the newline itself */
        if (tmp) {
            tmp++;
            if (*tmp == '\0')
                count--;
        }

        count++;
    }

    return count;
}

/* -------------------------------------------------------------------------- */
bool starts_with(const char *string, const char *start)
{
    if (!string || !start)
        return false;
    else
        return strncmp(string, start, strlen(start)) == 0;
}

/* -------------------------------------------------------------------------- */
bool filewalk(const char            *basedir,
              filewalk_function     fn,
              void                  *cookie,
              FilewalkFlags         flags,
              GError                **gerr)
{
    GDir        *dir;
    GError      *gerr_result = NULL;
    const char  *name;
    bool        end_loop = false;
    bool        result = true;

    dir = g_dir_open(basedir, 0, &gerr_result);
    if (!dir) {
        g_propagate_error(gerr, gerr_result);
        return false;
    }

    while ( (name = g_dir_read_name(dir)) && !end_loop) {
        char *path = g_build_filename(basedir, name, NULL);

        if (!(flags & FWF_INCLUDE_DEAD_LINK)) {
            if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
                goto endloop; /* continue */
            }
        }

        if (g_file_test(path, G_FILE_TEST_IS_DIR) &&
                !(g_file_test(path, G_FILE_TEST_IS_SYMLINK) &&
                    (flags & FWF_NO_SYMLINK_FOLLOW))) {
            if (!(flags & FWF_NO_RECURSION)) {
                if (!filewalk(path, fn, cookie, flags, gerr)) {
                    result = false;
                    end_loop = true;
                    goto endloop;
                }
            }

            if (flags & FWF_INCLUDE_DIRS) {
                if (!fn(path, cookie, &gerr_result)) {
                    g_propagate_error(gerr, gerr_result);
                    result = false;
                    end_loop = true;
                    goto endloop;
                }
            }
        } else {
            if (!fn(path, cookie, &gerr_result)) {
                g_propagate_error(gerr, gerr_result);
                result = false;
                end_loop = true;
                goto endloop;
            }
        }

endloop:
        g_free(path);
    }

    g_dir_close(dir);

    return result;
}

/* -------------------------------------------------------------------------- */
static bool directory_delete_function(const char    *filename,
                                      void          *cookie,
                                      GError        **gerr)
{
    UNUSED(cookie);
    int     err = 0;

    if (g_file_test(filename, G_FILE_TEST_IS_SYMLINK)) {
        /*printf("g_unlink(%s)\n", filename);*/
        err = g_unlink(filename);
    } else if (g_file_test(filename, G_FILE_TEST_IS_DIR)) {
        /*printf("g_rmdir(%s)\n", filename);*/
        err = g_rmdir(filename);
    } else {
        /*printf("g_unlink(%s)\n", filename);*/
        err = g_unlink(filename);
    }

    if (err < 0) {
        char buffer[1024];
        strerror_r(errno, buffer, 1024);
        g_set_error(gerr, g_lcdstuff_quark, errno, "Deletion failed: %s",
                buffer);
        return false;
    }

    return true;
}

/* -------------------------------------------------------------------------- */
bool delete_directory_recursively(const char *directory, GError **gerr)
{
    GError *gerr_result = NULL;

    if (!filewalk(directory, directory_delete_function,
            NULL, FWF_INCLUDE_DIRS | FWF_INCLUDE_DEAD_LINK |
                  FWF_NO_SYMLINK_FOLLOW,
            &gerr_result))
    {
        g_propagate_error(gerr, gerr_result);
        return false;
    }

    return true;
}

/* -------------------------------------------------------------------------- */
long copy_file(const char       *src_name,
               const char       *dest_dir,
               const char       *dest_name,
               GError           **gerr)
{
    int             src_fd = 0, dest_fd = 0;
    long            retval = -1;
    char            *dest_path = NULL;
    char            *filename = NULL;
    unsigned char   buffer[BUFSIZ];
    int             chars_read = 0;

    src_fd = g_open(src_name, O_RDONLY, 0);
    if (src_fd <= 0) {
        char buffer[1024];
        strerror_r(errno, buffer, 1024);
        g_set_error(gerr, 5, errno, "Opening '%s' for read failed: %s",
                src_name, buffer);
        goto end_copy;
    }

    filename = dest_name
        ? g_strdup(dest_name)
        : g_path_get_basename(src_name);

    dest_path = g_build_filename(dest_dir, filename, NULL);
    g_free(filename);
    dest_fd = g_open(dest_path, O_CREAT|O_WRONLY, 0644);
    if (dest_fd <= 0) {
        char buffer[1024];
        strerror_r(errno, buffer, 1024);
        g_set_error(gerr, g_lcdstuff_quark, errno, "Opening '%s' for "
                "write failed: %s", dest_path, buffer);
        goto end_copy;
    }

    do {
        int     chars_written = 0;
        int     tmp;

        chars_read = read(src_fd, buffer, BUFSIZ);
        if (chars_read < 0) {
            char buffer[1024];
            strerror_r(errno, buffer, 1024);
            g_set_error(gerr, g_lcdstuff_quark, errno, "read failed: %s", buffer);
            retval = -1;
            goto end_copy;
        }

        while (chars_written < chars_read) {
            tmp = write(dest_fd, buffer + chars_written, chars_read - chars_written);
            if (tmp < 0) {
                char buffer[1024];
                strerror_r(errno, buffer, 1024);
                g_set_error(gerr, g_lcdstuff_quark, errno, "Write failed: %s", buffer);
                retval = -1;
                goto end_copy;
            }

            chars_written += tmp;
        }
        retval += chars_written;
    } while (chars_read > 0);


end_copy:
    if (dest_fd != 0) {
        close(dest_fd);
    }
    if (dest_path)
        g_free(dest_path);
    if (src_fd != 0) {
        close(src_fd);
    }

    return retval;
}

/* -------------------------------------------------------------------------- */
unsigned long long get_free_bytes(const char *path, GError **gerr)
{
    struct statfs   my_statfs;
    int             err;

    err = statfs(path, &my_statfs);
    if (err < 0) {
        char buffer[1024];
        strerror_r(errno, buffer, 1024);
        g_set_error(gerr, g_lcdstuff_quark, errno, "statfs failed: %s", buffer);
        return 0;
    }
    return (unsigned long long)my_statfs.f_bavail * my_statfs.f_bsize;
}

/* vim: set ts=4 sw=4 et: */
