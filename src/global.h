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
#ifndef GLOBAL_H
#define GLOBAL_H

#define FREE_IF_VALID(ptr)                              \
        do {                                            \
            if (ptr)                                    \
            {                                           \
               free(ptr);                               \
            }                                           \
        } while (0);

#define CALL_IF_VALID(ptr, function)                    \
        do {                                            \
            if (ptr)                                    \
            {                                           \
               function(ptr);                           \
            }                                           \
        } while (0);

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) < (b)) ? (b) : (a))

enum ButtonType {
    BT_None,
    BT_Up,
    BT_Down
};

#define UNUSED(var)                                     \
    (void)(var)

extern GQuark g_lcdstuff_quark;

#endif /* GLOBAL_H */

