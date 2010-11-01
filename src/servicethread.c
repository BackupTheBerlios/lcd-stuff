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
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>

#include <glib.h>

#include <shared/str.h>
#include <shared/report.h>
#include <shared/sockets.h>

#include "servicethread.h"
#include "keyfile.h"
#include "main.h"

struct service_thread {
    GAsyncQueue   *command_queue;
    GHashTable    *clients;
    GHashTable    *client_data;
    struct client *current;
    GMutex        *mutex;
    int           client_number;
    int           listen_fd;
    int           current_socket;
};

/* -------------------------------------------------------------------------- */
void service_thread_register_client(struct service_thread   *service_thread,
                                    const struct client     *client,
                                    void                    *cookie)
{
    if (g_exit)
        return;

    g_mutex_lock(service_thread->mutex);
    g_hash_table_insert(service_thread->clients, client->name, (struct client *)client);
    g_hash_table_insert(service_thread->client_data, client->name, cookie);
    g_mutex_unlock(service_thread->mutex);

    g_atomic_int_inc(&service_thread->client_number);
}

/* -------------------------------------------------------------------------- */
void service_thread_unregister_client(struct service_thread     *service_thread,
                                      const char                *name)
{
    if (g_exit)
        return;

    g_mutex_lock(service_thread->mutex);
    g_hash_table_remove(service_thread->clients, name);
    g_hash_table_remove(service_thread->client_data, name);
    g_mutex_unlock(service_thread->mutex);

    if (g_atomic_int_dec_and_test(&service_thread->client_number))
        g_exit = true;
}

/* -------------------------------------------------------------------------- */
static void key_handler(struct service_thread   *service_thread,
                        const char              *key)
{
    report(RPT_DEBUG, "Key received, %s", key);

    if (service_thread->current && service_thread->current->key_callback) {
        void *data = g_hash_table_lookup(service_thread->client_data,
                                         service_thread->current->name);
        service_thread->current->key_callback(key, data);
    }
}

/* -------------------------------------------------------------------------- */
static void ignore_handler(struct service_thread    *service_thread,
                           const char               *screen)
{
    report(RPT_DEBUG, "Ignore received for screen", screen);

    if (service_thread->current && service_thread->current->ignore_callback) {
        void *data = g_hash_table_lookup(service_thread->client_data,
                                         service_thread->current->name);
        service_thread->current->ignore_callback(data);
    }
}

/* -------------------------------------------------------------------------- */
static void listen_handler(struct service_thread    *service_thread,
                           const char               *screen)
{
    report(RPT_DEBUG, "Listen received for screen", screen);

    /* make the new current */
    g_mutex_lock(service_thread->mutex);
    service_thread->current = g_hash_table_lookup(service_thread->clients, screen);
    g_mutex_unlock(service_thread->mutex);


    if (service_thread->current && service_thread->current->listen_callback) {
        void *data = g_hash_table_lookup(service_thread->client_data, screen);
        service_thread->current->listen_callback(data);
    }
}

/* -------------------------------------------------------------------------- */
void service_thread_command(struct service_thread   *service_thread,
                            const char              *string, ...)
{
    gchar   *result;
    va_list ap;

    if (g_exit) {
        return;
    }

    va_start(ap, string);
    result = g_strdup_vprintf(string, ap);
    va_end(ap);

    g_async_queue_push(service_thread->command_queue, result);
}

/* -------------------------------------------------------------------------- */
enum ProcessResponse {
    PR_SUCCESS,
    PR_FAILURE,
    PR_CALLBACK,
    PR_ERR_MISC,
    PR_INVALID
};

static enum ProcessResponse lcd_process_response(struct service_thread  *service_thread,
                                                 char                   *string)
{
	char        *argv[10];
	int         argc;

    argc = get_args(argv, string, 10);
    if (argc < 1) {
        report(RPT_ERR, "Error received");
        return PR_ERR_MISC;
    }

    if (strcmp(argv[0], "key") == 0) {
        key_handler(service_thread, argv[1]);
        return PR_CALLBACK;
    } else if (strcmp(argv[0], "listen") == 0) {
        listen_handler(service_thread, argv[1]);
        return PR_CALLBACK;
    } else if (strcmp(argv[0], "ignore") == 0) {
        ignore_handler(service_thread, argv[1]);
        return PR_CALLBACK;
    } else if (strcmp(argv[0], "success") == 0) {
        return PR_SUCCESS;
    } else if (strcmp(argv[0], "noop") == 0) {
        return PR_SUCCESS;
    } else if (strcmp(argv[0], "huh?") == 0) {
        char    errorstring[512] = "";
        int     i;

        for (i = 1; i < argc; i++) {
            g_strlcat(errorstring, argv[i], 512);
            g_strlcat(errorstring, " ", 512);
        }

        report(RPT_ERR, "Error: %s\n", errorstring);
        return PR_FAILURE;
    } else if (strcmp(argv[0], "menuevent") == 0) {
        char **id;
        struct client *client;
        gpointer param;
        bool found = false;

        id = g_strsplit(argv[2], "_", 2);

        g_mutex_lock(service_thread->mutex);
        found = g_hash_table_lookup_extended(service_thread->clients, id[0], NULL, &param);
        g_mutex_unlock(service_thread->mutex);
        client = (struct client *)param;

        if (found) {
            if (client->menu_callback) {
                void *data = g_hash_table_lookup(service_thread->client_data, client);
                client->menu_callback(argv[1],
                                      id[1],
                                      argc == 4 ? argv[3] : "",
                                      data);
            }
        }

        g_strfreev(id);
        return PR_CALLBACK;
    } else {
        report(RPT_ERR, "Invalid response: %s\n", string);
        return PR_INVALID;
    }
}

/* -------------------------------------------------------------------------- */
static int send_command_succ(struct lcd_stuff *lcd, gchar *command)
{
    enum ProcessResponse process_err;
    int err;
    int num_bytes = 0;
    int loopcount;
    char buffer[BUFSIZ];
    struct service_thread *service_thread = lcd->service_thread;

    err = sock_send_string(lcd->socket, command);
    if (err < 0) {
        report(RPT_ERR, "Could not send '%s': %d", command, err);
        return err;
    }

    for (loopcount = 0; loopcount < 10; loopcount++) {
        num_bytes = sock_recv_string(lcd->socket, buffer, BUFSIZ - 1);
        if (num_bytes < 0) {
            report(RPT_ERR, "Could not receive string: %s", strerror(errno));
            return err;
        } else if (num_bytes > 0) {

            process_err = lcd_process_response(service_thread, buffer);
            switch (process_err)
            {
                case PR_SUCCESS:
                    return 0;

                case PR_FAILURE:
                    return -1;

                case PR_CALLBACK:
                    continue;
                case PR_INVALID:
                    return -1;

                case PR_ERR_MISC:
                    return -1;
            }
        }
        g_usleep(100);
    }

    return 0;
}

/* -------------------------------------------------------------------------- */
static int check_for_input(struct lcd_stuff *lcd)
{
    int                     num_bytes = 1;
    char                    buffer[BUFSIZ];
    enum ProcessResponse    err;

    while (num_bytes != 0) {
        num_bytes = sock_recv_string(lcd->socket, buffer, BUFSIZ - 1);

        if (num_bytes > 0) {
            err = lcd_process_response(lcd->service_thread, buffer);
            switch (err) {
                case PR_SUCCESS:
                    return 0;

                case PR_FAILURE:
                    return -1;

                case PR_CALLBACK:
                    continue;

                case PR_INVALID:
                    return -1;

                case PR_ERR_MISC:
                    return -1;
            }
        } else if (num_bytes < 0) {
            return -1;
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------- */
static bool set_nonblocking(int fd)
{
    int flags, result;

    /* use non-blocking IO */
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        report(RPT_ERR, "fcntl() failed");
        return false;
    }

    result = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (flags < 0) {
        report(RPT_ERR, "2nd fcntl() failed");
        return false;
    }

    return true;
}

/* -------------------------------------------------------------------------- */
void service_thread_init(struct service_thread **p_service_thread)
{
    int result;
    struct sockaddr_in addr;
    int port;
    struct service_thread *service_thread;

    *p_service_thread = calloc(1, sizeof(struct service_thread));
    service_thread = *p_service_thread;

    service_thread->command_queue = g_async_queue_new();
    service_thread->clients       = g_hash_table_new(g_str_hash, g_str_equal);
    service_thread->client_data   = g_hash_table_new(g_str_hash, g_str_equal);
    service_thread->mutex         = g_mutex_new();

    /*
     * init network connection
     */

    /* read port */
    result = key_file_has_group("network");
    if (!result)
        return;

    port = key_file_get_integer_default("network", "port", 12454);
    conf_dec_count();

    service_thread->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (service_thread->listen_fd < 0) {
        report(RPT_ERR, "socket() failed");
        return;
    }

    /* fill the address structure */
    memset(&addr, 0, sizeof(addr));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    result = bind(service_thread->listen_fd, (struct sockaddr*)&addr, sizeof(addr));
    if (result < 0) {
        report(RPT_ERR, "bind() failed");
        return;
    }

    /* set into listening state */
    result = listen(service_thread->listen_fd, 5);
    if (result < 0)
        report(RPT_ERR, "listen() failed");

    set_nonblocking(service_thread->listen_fd);
}

/* -------------------------------------------------------------------------- */
static int check_for_net_input(struct service_thread *service_thread)
{
    char          **input;
    char          buffer[1025];
    int           ret;
    struct client *client;

    ret = read(service_thread->current_socket, buffer, 1024);
    if (ret < 0)
        return errno;
    else if (ret == 0)
        return -1;
    buffer[ret] = 0;

    input = g_strsplit(buffer, " ", 0);
    if (!input[0])
        return 0;

    g_mutex_lock(service_thread->mutex);
    client = g_hash_table_lookup(service_thread->clients, input[0]);
    g_mutex_unlock(service_thread->mutex);

    if (client && client->net_callback) {
        void *data = g_hash_table_lookup(service_thread->client_data, client->name);
        client->net_callback(input + 1, service_thread->current_socket, data);
    }

    g_strfreev(input);

    return 0;
}

/* -------------------------------------------------------------------------- */
gpointer service_thread_run(gpointer data)
{
    int count = 0;
    int ret;
    GTimeVal timeout;
    struct lcd_stuff *lcd = (struct lcd_stuff *)data;
    struct service_thread *service_thread = lcd->service_thread;

    while (!g_exit) {
        gchar *command;
        g_get_current_time(&timeout);
        g_time_val_add(&timeout, 100000);

        command = g_async_queue_timed_pop(service_thread->command_queue, &timeout);
        if (command) {
            send_command_succ(lcd, command);
            g_free(command);
        } else {
            if (count++ == 30) {
                /* still alive? */
                if (send_command_succ(lcd, "noop\n") < 0) {
                    report(RPT_ERR, "Server died");
                    break;
                }
                count = 0;
            } else {
                if (check_for_input(lcd) != 0) {
                    report(RPT_ERR, "Error while checking for input, maybe server died");
                    break;
                }
            }
        }

        /* check for incoming network connections */
        if (service_thread->listen_fd > 0) {
            if (service_thread->current_socket <= 0) {
                service_thread->current_socket = accept(service_thread->listen_fd, NULL, 0);
                if (service_thread->current_socket > 0)
                    set_nonblocking(service_thread->current_socket);
            } else {
                ret = check_for_net_input(service_thread);
                if (ret < 0 && ret != EAGAIN) {
                    close(service_thread->current_socket);
                    service_thread->current_socket = 0;
                }
            }
        }
    }

    if (service_thread->current_socket > 0)
        close(service_thread->current_socket);
    if (service_thread->listen_fd > 0)
        close(service_thread->listen_fd);
    g_async_queue_unref(service_thread->command_queue);
    g_hash_table_destroy(service_thread->clients);
    g_mutex_free(service_thread->mutex);

    return NULL;
}


/* vim: set ts=4 sw=4 et: */
