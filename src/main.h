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
#ifndef MAIN_H
#define MAIN_H

#include <stdbool.h>
#include <pthread.h>
#include <glib.h>

struct size {
    int width;
    int height;
};

struct service_thread;

struct lcd_stuff {
    char                    lcdproc_server[_POSIX_HOST_NAME_MAX];
    int                     lcdproc_port;
    int                     socket;
    struct size             display_size;
    char                    valid_chars[256];
    struct service_thread   *service_thread;
};

extern volatile bool g_exit;

void conf_dec_count(void);

#endif /* MAIN_H */

/* vim: set ts=4 sw=4 et: */
