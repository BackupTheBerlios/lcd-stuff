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
#ifndef MAILLIB_H
#define MAILLIB_H

#include <libetpan/libetpan.h>

enum {
    POP3_STORAGE = 0,
    IMAP_STORAGE,
    NNTP_STORAGE,
    MBOX_STORAGE,
    MH_STORAGE,
    MAILDIR_STORAGE,
};


int get_driver(char * name);

int init_storage(struct mailstorage * storage,
    int driver, char * server, int port,
    int connection_type, char * user, char * password, int auth_type,
    char * path, char * cache_directory, char * flags_directory);

char *mail_decode(const char *string);
char *display_from(struct mailimf_from * from);
char *display_subject(struct mailimf_subject *subject);

#endif /* MAILLIB_H */

/* vim: set ts=4 sw=4 et: */
