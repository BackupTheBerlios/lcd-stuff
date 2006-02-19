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
#include <string.h>
#include <iconv.h>
#include <stdlib.h>

#include "helpfunctions.h"

/* --------------------------------------------------------------------------------------------- */
void to_latin1(char *encoding, char *string, char *result, int size)
{
    iconv_t  desc;
    size_t   insize  = 0;
    size_t   outsize;
    char     *dest, *dest_start;
    int      converted;

    insize      = strlen(string);
    dest        = result;
    dest_start  = dest;
    outsize     = size - 1;

    desc = iconv_open("ISO-8859-1", encoding);
    if (desc < (iconv_t)0)
    {
        result[0] = 0;
        return;
    }
    else
    {
        converted = iconv(desc, &string, &insize, &result, &outsize);
        iconv_close(desc);
    }

    dest[size - outsize - 1] = 0;
}

/* --------------------------------------------------------------------------------------------- */
void free_del_LL_contents(LinkedList *list)
{
    void *item;

    /* free old mail */
    LL_Rewind(list);
    do
    {
        item = LL_DeleteNode(list);
        free(item);
    } while (LL_Next(list) == 0);
}

/* vim: set ts=4 sw=4 et: */
