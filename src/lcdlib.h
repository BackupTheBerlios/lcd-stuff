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
#ifndef LCDLIB_H
#define LCDLIB_H

typedef void (*key_callback_fun) (const char *);
typedef void (*menuevent_callback_fun) (const char *, const char *, const char *);
typedef void (*listen_callback_fun) (void);
typedef void (*ignore_callback_fun) (void);

/**
 * Callback function.
 */

/**
 * Structure for the lcdlib.
 */
typedef struct 
{
    int                         socket;
    key_callback_fun            key_callback;
    menuevent_callback_fun      menuevent_callback;
    listen_callback_fun         listen_callback;
    ignore_callback_fun         ignore_callback;
} lcdlib_t;

/**
 * Call this function before doing any operations in lcdlib.
 *
 * @param instance the libclib instance
 */
void lcd_init(lcdlib_t *instance, int socket);

/**
 * Closes the socket
 *
 * @param instance the lcdlib instance
 */
void lcd_finish(lcdlib_t *instance);

/**
 * Register callback functions.
 *
 * @param instance the libclib instance
 * @param key_callback the callback function which gets called if a key
 *        is received or NULL if key events are not important
 * @param listen_callback the callback function which gets called if a
 *        listen event is received or NULL if listen events are not important
 * @param ignore_callback the callback function which gets called if a
 *        ignore event is received or NULL if ignore events are not important
 * @param menuevent_callback the callback function which gets called if a
 *        menu event is received or NULL if menu events are not important
 */
void lcd_register_callback(lcdlib_t                  *instance,
                           key_callback_fun          key_callback,
                           listen_callback_fun       listen_callback,
                           ignore_callback_fun       ignore_callback,
                           menuevent_callback_fun    menuevent_callback);

/**
 * Sends a command using sock_send_string internally.
 *
 * @param instance the libclib instance
 * @param format the format string, followed by the string arguments similar to printf()
 * @return 0 on success, an error code on failure
 */
int lcd_send_command_succ(lcdlib_t         *instance,
                          const char      *format, ... );

/**
 * Sends a command using sock_send_string interanally and receives the response
 * if @p resonse is not NULL.
 *
 * @param instance the libclib instance
 * @param result the buffer for the result or @c NULL if no result should be received
 * @param size the number of bytes that have been allocared for buffer
 * @param format the format string, followed by the string arguments similar to printf()
 */
int lcd_send_command_rec_resp(lcdlib_t      *instance,
                              char          *result, 
                              int           size,
                              const char    *format, ... );

/**
 * Checks for input, call this regulary if the callback functions if you registered
 * callback function.
 * 
 * @param instance the libclib instance
 */
int lcd_check_for_input(lcdlib_t *instance);

#endif /* LCDLIB_H */
