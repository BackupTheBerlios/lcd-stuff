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
#include <stdbool.h>
#include <glib.h>

#include "util.h"

/* ---------------------- static variables ----------------------------------------------------- */
static char s_valid_chars[256];

/* --------------------------------------------------------------------------------------------- */
void string_canon_init(void)
{
    int i, character;

    /* init the list of valid chars, we don't use utf-8 or any other multibyte encoding */
    for (character = (int)' ', i = 0; character <= 255; character++)
    {
        if (character != '{' && character != '}')
        {
            s_valid_chars[i++] = character;
        }
    }

    s_valid_chars[i] = '\0';
}


/* --------------------------------------------------------------------------------------------- */
char *string_canon(char *string)
{
    return g_strcanon(string, s_valid_chars, '?');
}

/* --------------------------------------------------------------------------------------------- */
bool filewalk(const char *basedir, filewalk_function fn, GError **err)
{
    GDir        *dir;
    GError      *err_result = NULL;
    const char  *name;

    dir = g_dir_open(basedir, 0, &err_result);
    if (!dir) 
    {
        g_propagate_error(err, err_result);
        return false;
    }

    while ( (name = g_dir_read_name(dir)) )
    {
        char *path = g_build_filename(basedir, name, NULL);

        if (!g_file_test(path, G_FILE_TEST_EXISTS))
        {
            g_free(path);
            g_dir_close(dir);
            continue;
        }

        if (g_file_test(path, G_FILE_TEST_IS_DIR))
        {
            if (!filewalk(path, fn, err))
            {
                g_free(path);
                g_dir_close(dir);
                return false;
            }
        }
        else
        {
            fn(path);
        }

        g_free(path);
    }

    g_dir_close(dir);

    return true;
}

