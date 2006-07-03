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
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>
#include <errno.h>

#include <glib.h>

#include <shared/str.h>
#include <shared/report.h>
#include <shared/sockets.h>

#include "servicethread.h"
#include "main.h"

static GAsyncQueue   *s_command_queue = NULL;
static GHashTable    *s_clients       = NULL;
static struct client *s_current       = NULL;
static GStaticMutex  s_mutex          = G_STATIC_MUTEX_INIT;
static int           s_client_number  = 0;

/* --------------------------------------------------------------------------------------------- */
void service_thread_register_client(struct client *client)
{
    if (g_exit)
    {
        return;
    }

    g_static_mutex_lock(&s_mutex);
    g_hash_table_insert(s_clients, client->name, client);
    g_static_mutex_unlock(&s_mutex);

    g_atomic_int_inc(&s_client_number);
}

/* --------------------------------------------------------------------------------------------- */
void service_thread_unregister_client(const char *name)
{
    if (g_exit)
    {
        return;
    }
        
    g_static_mutex_lock(&s_mutex);
    g_hash_table_remove(s_clients, name);
    g_static_mutex_unlock(&s_mutex);

    if (g_atomic_int_dec_and_test(&s_client_number))
    {
        g_exit = true;
    }
}

/* --------------------------------------------------------------------------------------------- */
static void key_handler(const char *key)
{
    report(RPT_DEBUG, "Key received, %s", key);
    
    if (s_current && s_current->key_callback)
    {
        s_current->key_callback(key);
    }
}

/* --------------------------------------------------------------------------------------------- */
static void ignore_handler(const char *screen)
{
    report(RPT_DEBUG, "Ignore received for screen", screen);

    if (s_current && s_current->ignore_callback)
    {
        s_current->ignore_callback();
    }
}

/* --------------------------------------------------------------------------------------------- */
static void listen_handler(const char *screen)
{
    report(RPT_DEBUG, "Listen received for screen", screen);

    /* make the new current */
    g_static_mutex_lock(&s_mutex);
    s_current = g_hash_table_lookup(s_clients, screen);
    g_static_mutex_unlock(&s_mutex);

    if (s_current && s_current->listen_callback)
    {
        s_current->listen_callback();
    }
}

/* --------------------------------------------------------------------------------------------- */
void service_thread_command(const char *string, ...)
{
    gchar   *result;
    va_list ap;
    
    if (g_exit)
    {
        return;
    }

    va_start(ap, string);
    result = g_strdup_vprintf(string, ap);
    va_end(ap);

    g_async_queue_push(s_command_queue, result);
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
typedef enum
{
    PR_SUCCESS,
    PR_FAILURE,
    PR_CALLBACK,
    PR_ERR_MISC,
    PR_INVALID
} ProcessResponse;

static ProcessResponse lcd_process_response(char *string)
{
	char        *argv[10];
	int         argc;

    argc = get_args(argv, string, 10);
    if (argc < 1)
    {
        report(RPT_ERR, "Error received");
        return PR_ERR_MISC;
    }

    if (strcmp(argv[0], "key") == 0)
    {
        key_handler(argv[1]);
        return PR_CALLBACK;
    }
    else if (strcmp(argv[0], "listen") == 0)
    {
        listen_handler(argv[1]);
        return PR_CALLBACK;
    }
    else if (strcmp(argv[0], "ignore") == 0)
    {
        ignore_handler(argv[1]);
        return PR_CALLBACK;
    }
    else if (strcmp(argv[0], "success") == 0)
    {
        return PR_SUCCESS;
    }
    else if (strcmp(argv[0], "noop") == 0)
    {
        return PR_SUCCESS;
    }
    else if (strcmp(argv[0], "huh?") == 0)
    {
        report(RPT_ERR, "Error: %s\n", argv[1]);
        return PR_FAILURE;
    }
    else if (strcmp(argv[0], "menuevent") == 0)
    {
        char **id;
        struct client *client;
        gpointer param;
        bool found = false;

        id = g_strsplit(argv[2], "_", 2);

        g_static_mutex_lock(&s_mutex);
        found = g_hash_table_lookup_extended(s_clients, id[0], NULL, &param);
        g_static_mutex_unlock(&s_mutex);
        client = (struct client *)param;

        if (found)
        {
            if (client->menu_callback)
            {
                client->menu_callback(argv[1], id[1], argc == 4 ? argv[3] : "");
            }
        }

        g_strfreev(id);
        return PR_CALLBACK;
    }
    else
    {
        report(RPT_ERR, "Invalid response: %s\n", string);
        return PR_INVALID;
    }
}

/* --------------------------------------------------------------------------------------------- */
static int send_command_succ(gchar *command)
{
    ProcessResponse  process_err;
    int              err;
    int              num_bytes      = 0;
    int              loopcount;
    char             buffer[BUFSIZ];
    
    err = sock_send_string(g_socket, command);
    if (err < 0)
    {
        report(RPT_ERR, "Could not send '%s': %d", command, err);
        return err;
    }

    for (loopcount = 0; loopcount < 10; loopcount++)
    {
        num_bytes = sock_recv_string(g_socket, buffer, BUFSIZ - 1);
        if (num_bytes < 0)
        {
            report(RPT_ERR, "Could not receive string: %s", strerror(errno));
            return err;
        }
        else if (num_bytes > 0)
        {
            process_err = lcd_process_response(buffer);
            switch (process_err)
            {
                case PR_SUCCESS:        return 0;
                case PR_FAILURE:        return -1;
                case PR_CALLBACK:       continue;
                case PR_INVALID:        return -1;
                case PR_ERR_MISC:       return -1;
            }
        }
        g_usleep(100);
    }

    return 0;
}

/* --------------------------------------------------------------------------------------------- */
static int check_for_input(void)
{
    int             num_bytes         = 1;
    char            buffer[BUFSIZ];
    ProcessResponse err;
        
    while (num_bytes != 0)
    {
        num_bytes = sock_recv_string(g_socket, buffer, BUFSIZ - 1);
        if (num_bytes > 0)
        {
            err = lcd_process_response(buffer);
            switch (err)
            {
                case PR_SUCCESS:        return 0;
                case PR_FAILURE:        return -1;
                case PR_CALLBACK:       continue;
                case PR_INVALID:        return -1;
                case PR_ERR_MISC:       return -1;
            }
        }
        else if (num_bytes < 0)
        {
            return -1;
        }
    }

    return 0;
}

/* --------------------------------------------------------------------------------------------- */
void service_thread_init(void)
{
    s_command_queue = g_async_queue_new();
    s_clients       = g_hash_table_new(g_str_hash, g_str_equal);
}

/* --------------------------------------------------------------------------------------------- */
gpointer service_thread_run(gpointer data)
{
    int count = 0;

    while (!g_exit)
    {
        gchar *command;

        command = g_async_queue_try_pop(s_command_queue);
        if (command)
        {
            send_command_succ(command);
            g_free(command);
        }
        else
        {
            g_usleep(100000);

            if (count++ == 30)
            {
                /* still alive? */
                if (send_command_succ("noop\n") < 0)
                {
                    report(RPT_ERR, "Server died");
                    break;
                }
                count = 0;
            }
            else
            {
                if (check_for_input() != 0)
                {
                    report(RPT_ERR, "Error while checking for input, maybe server died");
                    break;
                }
            }
        }
    }
    
    g_async_queue_unref(s_command_queue);
    g_hash_table_destroy(s_clients);

    return NULL;
}


/* vim: set ts=4 sw=4 et: */
