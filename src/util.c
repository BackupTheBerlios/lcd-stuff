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
#include <sys/types.h>
#include <fcntl.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "util.h"
#include "global.h"

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
    char *remainder;
    int cur_line = 0;

    if (!buffer)
        return NULL;

    new_str = g_string_new(buffer->str);
    if (!new_str)
        return NULL;

    remainder = new_str->str;

    while (strlen(remainder) > (size_t)length) {
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
static GString *stringbuffer_wrap_spaces(GString     *buffer,
                                         size_t      length,
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

        while (new && (new - remainder <= (int)length)) {
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
    if (length * maxlines < (int)buffer->len)
        return stringbuffer_wrap_simple(buffer, length, maxlines);
    else {
        GString *try = NULL;

        try = stringbuffer_wrap_spaces(buffer, length, maxlines);
        if (stringbuffer_get_lines(try) <= maxlines)
            return try;
        else {
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

/* vim: set ts=4 sw=4 et: */
