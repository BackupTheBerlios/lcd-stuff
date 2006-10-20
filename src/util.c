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
