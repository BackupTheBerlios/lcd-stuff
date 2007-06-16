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
#include <libetpan/libetpan.h>
#include <string.h>

#include <glib.h>
#include <stdlib.h>

#include <shared/report.h>

#include "maillib.h"

/* from http://cvs.sourceforge.net/viewcvs.py/libetpan/libetpan/tests/readmsg-simple.c?rev=1.7&view=markup */

struct storage_name {
    int id;
    char * name;
};

static struct storage_name storage_tab[] = {
    {POP3_STORAGE, "pop3"},
    {IMAP_STORAGE, "imap"},
    {NNTP_STORAGE, "nntp"},
    {MBOX_STORAGE, "mbox"},
    {MH_STORAGE, "mh"},
    {MAILDIR_STORAGE, "maildir"},
};

/* -------------------------------------------------------------------------- */
int get_driver(char * name)
{
    int driver_type;
    unsigned int i;

    driver_type = -1;
    for(i = 0 ; i < sizeof(storage_tab) / sizeof(struct storage_name) ; i++) {
        if (strcasecmp(name, storage_tab[i].name) == 0) {
            driver_type = i;
            break;
        }
    }

    return driver_type;
}

/* -------------------------------------------------------------------------- */
bool is_local(const char *driver)
{
    return strcmp(driver, "mh") == 0 ||
        strcmp(driver, "maildir") == 0 ||
        strcmp(driver, "mbox") == 0;
}

/* -------------------------------------------------------------------------- */
int init_storage(struct mailstorage     *storage,
                 int                    driver,
                 char                   *server,
                 int                    port,
                 int                    connection_type,
                 char                   *user,
                 char                   *password,
                 int                    auth_type,
                 char                   *path,
                 char                   *cache_directory,
                 char                   *flags_directory)
{
    int r;
    int cached;

    cached = (cache_directory != NULL);

    switch (driver) {
        case POP3_STORAGE:
            r = pop3_mailstorage_init(storage, server, port, NULL, connection_type,
                    auth_type, user, password, cached, cache_directory,
                    flags_directory);
            if (r != MAIL_NO_ERROR) {
                report(RPT_ERR, "error initializing POP3 storage\n");
                goto err;
            }
            break;

        case IMAP_STORAGE:
            r = imap_mailstorage_init(storage, server, port, NULL, connection_type,
                    IMAP_AUTH_TYPE_PLAIN, user, password, cached, cache_directory);
            if (r != MAIL_NO_ERROR) {
                report(RPT_ERR, "error initializing IMAP storage\n");
                goto err;
            }
            break;

        case NNTP_STORAGE:
            r = nntp_mailstorage_init(storage, server, port, NULL, connection_type,
                    NNTP_AUTH_TYPE_PLAIN, user, password, cached, cache_directory,
                    flags_directory);
            if (r != MAIL_NO_ERROR) {
                report(RPT_ERR, "error initializing NNTP storage\n");
                goto err;
            }
            break;

        case MBOX_STORAGE:
            r = mbox_mailstorage_init(storage, path, cached, cache_directory,
                    flags_directory);
            if (r != MAIL_NO_ERROR) {
                report(RPT_ERR, "error initializing mbox storage\n");
                goto err;
            }
            break;

        case MH_STORAGE:
            r = mh_mailstorage_init(storage, path, cached, cache_directory,
                    flags_directory);
            if (r != MAIL_NO_ERROR) {
                report(RPT_ERR, "error initializing MH storage\n");
                goto err;
            }
            break;
        case MAILDIR_STORAGE:
            r = maildir_mailstorage_init(storage, path, cached, cache_directory,
                    flags_directory);
            if (r != MAIL_NO_ERROR) {
                report(RPT_ERR, "error initializing maildir storage\n");
                goto err;
            }
            break;
    }

    return MAIL_NO_ERROR;

err:
    return r;
}

/* -------------------------------------------------------------------------- */
char *mail_decode(const char *string)
{
    size_t cur_token;
    char *decoded_subject;

    if (!string)
    {
        return g_strdup("");
    }

    cur_token = 0;
    mailmime_encoded_phrase_parse("iso-8859-1",
            string, strlen(string),
            &cur_token, "iso-8859-1", &decoded_subject);

    return decoded_subject;
}

/* -------------------------------------------------------------------------- */
char *display_from(struct mailimf_from * from)
{
    clistiter * cur;

    for (cur = clist_begin(from->frm_mb_list->mb_list) ; cur != NULL ; cur = clist_next(cur)) 
    {
        struct mailimf_mailbox * mb;

        mb = clist_content(cur);

        return mail_decode(mb->mb_display_name);
    } 
    return g_strdup("");
}

/* -------------------------------------------------------------------------- */
char *display_subject(struct mailimf_subject * subject)
{
    return mail_decode(subject->sbj_value);
}


/* vim: set ts=4 sw=4 et: */
