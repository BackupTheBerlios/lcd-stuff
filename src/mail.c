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
#include <unistd.h>
#include <string.h>
#include <time.h>

#include <shared/report.h>
#include <shared/sockets.h>
#include <shared/str.h>

#include <libetpan/libetpan.h>

#include "mail.h"
#include "main.h"
#include "constants.h"
#include "maillib.h"
#include "global.h"
#include "servicethread.h"
#include "keyfile.h"
#include "util.h"

/* ---------------------- forward declarations ------------------------------ */
static void mail_ignore_handler(void);
static void mail_key_handler(const char *str);

/* ---------------------- constants ----------------------------------------- */
#define MODULE_NAME           "mail"

/* ---------------------- types --------------------------------------------- */
struct mailbox {
    char            *server;
    char            *username;
    char            *password;
    char            *name;
    char            *type;          /* pop3, imap */
    char            *mailbox_name;  /* imap only */
    unsigned int    messages_seen;
    unsigned int    messages_unseen;
    unsigned int    messages_total;
    bool            hidden;
};

struct email {
    struct mailbox  *box;
    int             message_number_in_box;
    char            *subject;
    char            *from;
};

/* ------------------------variables ---------------------------------------- */
static int              s_interval;
static GPtrArray        *s_mailboxes;
static GList            *s_email         = NULL;
static int              s_current_screen = 0;
static char             *s_title_prefix  = NULL;
struct client           mail_client = 
                        {
                            .name            = MODULE_NAME,
                            .key_callback    = mail_key_handler,
                            .listen_callback = NULL,
                            .ignore_callback = mail_ignore_handler
                        }; 

/* -------------------------------------------------------------------------- */
static void update_screen(const char    *title,
                          char          *line1,
                          char          *line2,
                          char          *line3)
{
    if (title) {
        if (strlen(title) > 0) {
            service_thread_command("widget_set %s title {%s: %s}\n", MODULE_NAME, 
                    s_title_prefix, title);
        } else {
            service_thread_command("widget_set %s title {%s}\n", MODULE_NAME, 
                    s_title_prefix);
        }
    }

    if (line1) {
        service_thread_command("widget_set %s line1 1 2 {%s}\n", MODULE_NAME, line1);
    }

    if (line2) {
        service_thread_command("widget_set %s line2 1 3 {%s}\n", MODULE_NAME, line2);
    }

    if (line3) {
        service_thread_command("widget_set %s line3 1 4 {%s}\n", MODULE_NAME, line3);
    }
}

/* -------------------------------------------------------------------------- */
static void show_screen(void)
{
    int     tot             = g_list_length(s_email);
    char    *line1          = NULL;
    char    *line1_old      = NULL;
    char    *line2          = NULL;
    char    *line3          = NULL;
    char    *title          = NULL;
    int     i;

    /* build the first line */
    for (i = 0; i < s_mailboxes->len; i++) {
        struct mailbox *box = g_ptr_array_index(s_mailboxes, i);

        line1_old = line1 ? line1 : g_strdup("");
        line1 = g_strdup_printf("%s%s:%d ", line1_old, box->name, 
                box->messages_unseen);
        g_free(line1_old);
    }

    if (s_current_screen < 0) {
        s_current_screen = tot - 1;
    } else if (s_current_screen >= tot) {
        s_current_screen = 0;
    }

    if (tot != 0) {
        GList *cur = g_list_first(s_email);

        /* build the second line */
        i = 0;
        while (cur) {
            struct email *mail = (struct email *)cur->data;
            if (!mail) {
                break;
            }

            if (i++ == s_current_screen) {
                line2 = mail->from;
                line3 = mail->subject;
                title = g_strdup_printf("%s %d", mail->box->name,
                                        mail->message_number_in_box);
                break;
            }
            cur = cur->next;
        }
    }

    if (!title) {
        title = g_strdup("");
    }

    update_screen(title, line1, line2 ? line2 : "", line3 ? line3 : "");
    g_free(title);
    g_free(line1);
}

/* -------------------------------------------------------------------------- */
void free_emails(void)
{
    GList *cur = g_list_first(s_email);
    while (cur) {
        g_free(((struct email *)cur->data)->from);
        g_free(((struct email *)cur->data)->subject);
        free(cur->data);
        cur = cur->next;
    }
    g_list_free(s_email);
    s_email = NULL;
}

/* -------------------------------------------------------------------------- */
static void mail_check(void)
{
    int mb;

    s_current_screen = 0;

    /* free old mail */
    free_emails();

    for (mb = 0; mb < s_mailboxes->len; mb++) {
        struct mailbox           *box       = g_ptr_array_index(s_mailboxes, mb);
        struct mailfolder        *folder    = NULL;
        struct mailmessage_list  *messages  = NULL;
        struct mailmessage       *message   = NULL;
        struct mailstorage       *storage   = NULL;
        int                      r, i;
        int                      message_number = 1;

        if (!box) {
            break;
        }

        if (!is_local(box->type)) {
            update_screen(box->name, "", "  Receiving ...", "");
            box->messages_seen = box->messages_total = box->messages_unseen = 0;
        }

        storage = mailstorage_new(NULL);
        if (!storage) {
            report(RPT_ERR, "error initializing storage\n");
            goto end_loop;
        }

        r = init_storage(storage, get_driver(box->type), box->server, 0,
                CONNECTION_TYPE_PLAIN, box->username, box->password,
                POP3_AUTH_TYPE_PLAIN, box->mailbox_name, NULL, NULL);
        if (r != MAIL_NO_ERROR) {
            report(RPT_ERR, "error initializing storage");
            goto end_loop;
        }

        /* get the folder structure */
        folder = mailfolder_new(storage, box->mailbox_name, NULL);
        if (folder == NULL) {
            report(RPT_ERR, "mailfolder_new failed");
            goto end_loop;
        }

        r = mailfolder_connect(folder);
        if (r != MAIL_NO_ERROR) {
            report(RPT_ERR, "mailfolder_connect failed");
            goto end_loop;
        }

        r = mailfolder_status(folder, &box->messages_total, &box->messages_seen,
                &box->messages_unseen);
        if (r != MAIL_NO_ERROR) {
            report(RPT_ERR, "mailfolder_status failed");
            goto end_loop;
        }

        /* end here when no message fetching is required */
        if (box->hidden) {
            goto end_loop;
        }

        r = mailfolder_get_messages_list(folder, &messages);
        if (r != MAIL_NO_ERROR) {
            report(RPT_ERR, "mailfolder_get_message failed");
            goto end_loop;
        }

        r = mailfolder_get_envelopes_list(folder, messages);
        if (r != MAIL_NO_ERROR) {
            report(RPT_ERR, "mailfolder_get_mailmessages_list failed");
            goto end_loop;
        }

        for (i = 0; i < carray_count(messages->msg_tab); i++) {
            clistiter             *cur   = NULL;
            struct mailimf_fields *hdr   = NULL;
            struct email          *mail  = NULL;
            struct mail_flags     *flags = NULL;

            message = (struct mailmessage *)carray_get(messages->msg_tab, i);

            r = mailmessage_fetch_envelope(message, &hdr);
            if (r != MAIL_NO_ERROR) {
                report(RPT_ERR, "mailmessage_fetch_envelope failed\n");
                goto end_inner;
            }

            /* get the flags */
            r = mailmessage_get_flags(message, &flags);
            if (r == MAIL_NO_ERROR) {
                /* check if message is 'seen' */
                if (flags->fl_flags & MAIL_FLAG_SEEN) {
                    /* skip this, move to next message */
                    goto end_inner;
                }
            }

            /* allocate the email */
            mail = malloc(sizeof(struct email));
            if (!mail) {
                report(RPT_ERR, MODULE_NAME ": Out of memory");
                goto end_loop;
            }
            memset(mail, 0, sizeof(struct email));

            for (cur = clist_begin(hdr->fld_list) ; cur != NULL; cur = clist_next(cur)) {
                struct mailimf_field *field = (struct mailimf_field *)clist_content(cur);
    
                switch (field->fld_type) {
                      case MAILIMF_FIELD_FROM:
                          mail->from = display_from(field->fld_data.fld_from);
                          string_canon(mail->from);
                          break;

                      case MAILIMF_FIELD_SUBJECT:
                          mail->subject = display_subject(field->fld_data.fld_subject);
                          string_canon(mail->subject);
                          break;
                }
            }

            mail->message_number_in_box = message_number++;
            mail->box = box;
            s_email = g_list_append(s_email, mail);

end_inner:
            if (hdr)
                mailimf_fields_free(hdr);
        }

end_loop:
        /* workaround to prevent maildir messages from being marked as 'old' */
        if (strcmp(box->type, "maildir") == 0) {
            free(storage->sto_session);
            storage->sto_session = NULL;
        }

        if (messages)
            mailmessage_list_free(messages);
        if (folder) {
            mailfolder_disconnect(folder);
            mailfolder_free(folder);
        }
        if (storage)
            mailstorage_free(storage);
        ;
    }
}

/* -------------------------------------------------------------------------- */
static void mail_key_handler(const char *str)
{
    if (strcmp(str, "Up") == 0) {
        s_current_screen++;
    } else {
        s_current_screen--;
    }
    show_screen();
}

/* -------------------------------------------------------------------------- */
static void mail_ignore_handler(void)
{
    s_current_screen++;
    show_screen();
}

/* -------------------------------------------------------------------------- */
static bool mail_init(void)
{
    int        i;
    int        number_of_mailboxes;
    char       *tmp;

    /* register client */
    service_thread_register_client(&mail_client);

    /* add a screen */
    service_thread_command("screen_add " MODULE_NAME "\n");

    /* set the name */
    tmp = key_file_get_string_default(MODULE_NAME, "name", "Mail");
    service_thread_command("screen_set %s -name %s\n", MODULE_NAME, tmp);
    g_free(tmp);

    /* add the title */
    service_thread_command("widget_add " MODULE_NAME " title title\n");
    s_title_prefix = key_file_get_string_default_l1(MODULE_NAME, "name", "Mail");

    /* add three lines */
    service_thread_command("widget_add " MODULE_NAME " line1 string\n");
    service_thread_command("widget_add " MODULE_NAME " line2 string\n");
    service_thread_command("widget_add " MODULE_NAME " line3 string\n");

    /* register keys */
    service_thread_command("client_add_key Up\n");
    service_thread_command("client_add_key Down\n");

    /* get config items */
    s_interval = key_file_get_integer_default(MODULE_NAME, "interval", 300);

    number_of_mailboxes = key_file_get_integer_default(MODULE_NAME,
            "number_of_servers", 0);
    if (number_of_mailboxes == 0) {
        report(RPT_ERR, MODULE_NAME ": No mailboxes specified");
        return false;
    }

    /* create the linked list of mailboxes */
    s_mailboxes = g_ptr_array_sized_new(number_of_mailboxes);

    /* process the mailboxes */
    for (i = 1; i <= number_of_mailboxes; i++) {
        struct mailbox *cur = (struct mailbox *)malloc(sizeof(struct mailbox));
        if (!cur) {
            report(RPT_ERR, MODULE_NAME ": Could not create mailbox: Of ouf memory");
            return false;
        }
        memset(cur, 0, sizeof(struct mailbox));

        tmp = g_strdup_printf("server%d", i);
        cur->server = key_file_get_string_default(MODULE_NAME, tmp, "");
        g_free(tmp);

        tmp = g_strdup_printf("user%d", i);
        cur->username = key_file_get_string_default(MODULE_NAME, tmp, "");
        g_free(tmp);

        tmp = g_strdup_printf("type%d", i);
        cur->type = key_file_get_string_default(MODULE_NAME, tmp, "pop3");
        g_free(tmp);

        tmp = g_strdup_printf("password%d", i);
        cur->password = key_file_get_string_default(MODULE_NAME, tmp, "");
        g_free(tmp);

        tmp = g_strdup_printf("mailbox_name%d", i);
        cur->mailbox_name = key_file_get_string_default(MODULE_NAME, tmp, "INBOX");
        g_free(tmp);

        tmp = g_strdup_printf("hidden%d", i);
        cur->hidden = key_file_get_boolean_default(MODULE_NAME, tmp, false);
        g_free(tmp);

        tmp = g_strdup_printf("name%d", i);
        cur->name = key_file_get_string_default(MODULE_NAME, tmp, cur->server);
        g_free(tmp);

        g_ptr_array_add(s_mailboxes, cur);
    }

    return true;
}

/* -------------------------------------------------------------------------- */
void *mail_run(void *cookie)
{
    int     i;
    time_t  next_check;
    int     result;

    result = key_file_has_group(MODULE_NAME);
    if (!result) {
        report(RPT_INFO, "mail disabled");
        return NULL;
    }

    if (!mail_init()) {
        return NULL;
    }
    conf_dec_count();

    /* check mails instantly */
    next_check = time(NULL);

    /* dispatcher */
    while (!g_exit) {
        g_usleep(100000);

        /* check emails? */
        if (time(NULL) > next_check) {
            mail_check();
            show_screen();
            next_check = time(NULL) + s_interval;
        }
    }

    service_thread_unregister_client(MODULE_NAME);
    free_emails();

    for (i = 0; i < s_mailboxes->len; i++) {
        struct mailbox *cur = (struct mailbox *)g_ptr_array_index(s_mailboxes, i);
        g_free(cur->server);
        g_free(cur->username);
        g_free(cur->mailbox_name);
        g_free(cur->password);
        g_free(cur->name);
        g_free(cur->type);
        free(cur);
    }
    g_ptr_array_free(s_mailboxes, true);
    g_free(s_title_prefix);

    return NULL;
}

/* vim: set ts=4 sw=4 et: */
