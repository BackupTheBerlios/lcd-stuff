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
#ifndef MPD_H
#define MPD_H

#include <glib.h>

/**
 * Run function of the MPD daemon.
 */
void *mpd_run(void *cookie);

/**
 * Connection with host, password and port
 */
struct connection {
    char            *host;
    char            *password;
    unsigned short  port;
};

/**
 * Allocates a new connection.
 */
static inline struct connection *connection_new(const char       *host,
                                                const char       *password,
                                                unsigned short   port)
{
    struct connection *ret;

    ret = g_malloc(sizeof(struct connection));
    if (!ret)
        return NULL;

    ret->host = g_strdup(host);
    ret->password = g_strdup(password);
    ret->port = port;

    return ret;
}

/**
 * Frees the memory occupied by the connection
 */
static inline void connection_delete(struct connection *connection)
{
    if (connection) {
        g_free(connection->host);
        g_free(connection->password);
        g_free(connection);
    }
}

#endif /* MPD_H */

/* vim: set ts=4 sw=4 et: */
