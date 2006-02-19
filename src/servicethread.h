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
#ifndef SERVICETHREAD_H
#define SERVICETHREAD_H

#include <glib.h>

typedef void (*key_callback_fun) (const char *);
typedef void (*listen_callback_fun) (void);
typedef void (*ignore_callback_fun) (void);


/**
 * A client.
 */
struct client
{
    char                        *name;              /**< the unique name of the client */
    key_callback_fun            key_callback;       /**< the callback function for key
                                                         events or NULL */
    listen_callback_fun         listen_callback;    /**< the callback function if the
                                                         client gets displayed on the
                                                         display */
    ignore_callback_fun         ignore_callback;    /**< the callback function if the
                                                         client gets hidden on the display */
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
void service_thread_register_client(struct client *client);

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


#endif /* SERVICETHREAD_H */

/* vim: set ts=4 sw=4 et: */
