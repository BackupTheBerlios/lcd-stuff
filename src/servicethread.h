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
#ifndef SERVICETHREAD_H
#define SERVICETHREAD_H

#include <glib.h>

typedef void (*key_callback_fun) (const char *, void *);
typedef void (*listen_callback_fun) (void *);
typedef void (*ignore_callback_fun) (void *);
typedef void (*menu_callback_fun) (const char *, const char *, const char *, void *);
typedef void (*net_callback_fun) (char **args, int fd, void *);


/**
 * A client.
 */
struct client
{
    char                 *name;              /**< the unique name of the client */
    key_callback_fun     key_callback;       /**< the callback function for key
                                                  events or NULL */
    listen_callback_fun  listen_callback;    /**< the callback function if the
                                                  client gets displayed on the
                                                  display */
    ignore_callback_fun  ignore_callback;    /**< the callback function if the
                                                  client gets hidden on the display */
    menu_callback_fun    menu_callback;      /**< the callback function for menu
                                                  events */
    net_callback_fun     net_callback;       /**< callback for network commands */
};

/**
 * Run method of the service thread.
 */
gpointer service_thread_run(gpointer data);

/**
 * Registers a client.
 *
 * @param client the client to register
 */
void service_thread_register_client(const struct client *client, void *data);

/**
 * Unregisters a client.
 *
 * @param name the name of the client
 */
void service_thread_unregister_client(const char *name);

/**
 * Sends a command
 *
 * @param string the command to send
 */
void service_thread_command(const char *string, ...);

/**
 * Initialzies the service thread.
 */
void service_thread_init(void);


#endif /* SERVICETHREAD_H */

/* vim: set ts=4 sw=4 et: */
