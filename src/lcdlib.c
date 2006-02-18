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
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include <shared/report.h>
#include <shared/configfile.h>
#include <shared/sockets.h>
#include <shared/str.h>
#include <shared/LL.h>

#include "lcdlib.h"

/* --------------------------------------------------------------------------------------------- */
void lcd_init(lcdlib_t *instance, int socket)
{
    instance->socket             = socket;
    instance->key_callback       = NULL;
    instance->listen_callback    = NULL;
    instance->ignore_callback    = NULL;
    instance->menuevent_callback = NULL;
}

/* --------------------------------------------------------------------------------------------- */
void lcd_register_callback(lcdlib_t                  *instance,
                           key_callback_fun          key_callback,
                           listen_callback_fun       listen_callback,
                           ignore_callback_fun       ignore_callback,
                           menuevent_callback_fun    menuevent_callback)
{
    instance->key_callback       = key_callback;
    instance->listen_callback    = listen_callback;
    instance->ignore_callback    = ignore_callback;
    instance->menuevent_callback = menuevent_callback;
}


/* --------------------------------------------------------------------------------------------- */
typedef enum
{
    PR_SUCCESS,
    PR_FAILURE,
    PR_CALLBACK,
    PR_ERR_MISC,
    PR_INVALID
} ProcessResponse;

static ProcessResponse lcd_process_response(lcdlib_t *instance, char *string)
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
        if (instance->key_callback)
        {
            instance->key_callback(argv[1]);
        }
        return PR_CALLBACK;
    }
    else if (strcmp(argv[0], "listen") == 0)
    {
        if (instance->listen_callback)
        {
            instance->listen_callback();
        }
        return PR_CALLBACK;
    }
    else if (strcmp(argv[0], "ignore") == 0)
    {
        if (instance->ignore_callback)
        {
            instance->ignore_callback();
        }
        return PR_CALLBACK;
    }
    else if (strcmp(argv[0], "menuevent") == 0)
    {
        if (instance->menuevent_callback)
        {
            instance->menuevent_callback(argv[1], argv[2], argc < 4 ? "" : argv[3]);
        }
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
    else
    {
        report(RPT_ERR, "Invalid response: %s\n", string);
        return PR_INVALID;
    }
}


/* --------------------------------------------------------------------------------------------- */
int lcd_send_command_succ(lcdlib_t *instance, const char *format, ... )
{
    va_list          ap;
    char             buffer[BUFSIZ];
    ProcessResponse  process_err;
    int              err;
    int              num_bytes      = 0;
    int              loopcount;
    
    va_start(ap, format);
    vsnprintf(buffer, BUFSIZ, format, ap);
    va_end(ap);
    buffer[BUFSIZ-1] = 0;

    err = sock_send_string(instance->socket, buffer);
    if (err < 0)
    {
        report(RPT_ERR, "Could not send '%s': %d", buffer, err);
        return err;
    }

    for (loopcount = 0; loopcount < 10; loopcount++)
    {
        num_bytes = sock_recv_string(instance->socket, buffer, BUFSIZ - 1);
        if (num_bytes < 0)
        {
            report(RPT_ERR, "Could not receive string: %d", err);
            return err;
        }
        else if (num_bytes > 0)
        {
            process_err = lcd_process_response(instance, buffer);
            switch (process_err)
            {
                case PR_SUCCESS:        return 0;
                case PR_FAILURE:        return -1;
                case PR_CALLBACK:       continue;
                case PR_INVALID:        return -1;
                case PR_ERR_MISC:       return -1;
            }
        }
        usleep(100);
    }

    return 0;
}


/* --------------------------------------------------------------------------------------------- */
int lcd_send_command_rec_resp(lcdlib_t *instance, char *result, int size, const char *format, ...)
{
    va_list     ap;
    char        buffer[BUFSIZ];
    int         err;
    int         num_bytes        = 0;
    
    va_start(ap, format);
    vsnprintf(buffer, BUFSIZ, format, ap);
    va_end(ap);
    buffer[BUFSIZ-1] = 0;

    err = sock_send_string(instance->socket, buffer);
    if (err < 0)
    {
        report(RPT_ERR, "Could not send '%s': %d", buffer, err);
        return err;
    }
    
    while (num_bytes == 0)
    {
        num_bytes = sock_recv_string(instance->socket, result, size - 1);
        if (num_bytes < 0)
        {
            report(RPT_ERR, "Could not receive string: %d", err);
            return err;
        }
    }

    return num_bytes;
}

/* --------------------------------------------------------------------------------------------- */
int lcd_check_for_input(lcdlib_t *instance)
{
    int             num_bytes         = 1;
    char            buffer[BUFSIZ];
    ProcessResponse err;
        
    while (num_bytes != 0)
    {
        num_bytes = sock_recv_string(instance->socket, buffer, BUFSIZ - 1);
        if (num_bytes > 0)
        {
            err = lcd_process_response(instance, buffer);
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
void lcd_finish(lcdlib_t *instance)
{
    close(instance->socket);
    lcd_init(instance, 0);
}

/* vim: set ts=4 sw=4 et: */
