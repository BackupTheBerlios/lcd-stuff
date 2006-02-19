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
#ifndef HELPFUNCTIONS_H
#define HELPFUNCTIONS_H

#include <shared/LL.h>

#define max(a, b) (((a) < (b)) ? (b) : (a))
#define min(a, b) (((a) < (b)) ? (a) : (b))

/**
 * Converts text to Latin1 as required by lcdproc
 *
 * @param encoding the source encoding
 * @param string the source string
 * @param result the output result
 * @param size the size of result
 */
void to_latin1(char *encoding, char *string, char *result, int size);

/**
 * Frees the contents in the list and deletes them from the list.
 * Doesn't free the list itself.
 *
 * @param list the list
 */
void free_del_LL_contents(LinkedList *list);

#endif /* HELPFUNCTIONS_H */
