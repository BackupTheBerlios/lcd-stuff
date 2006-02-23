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
#include <unistd.h>
#include <string.h>
#include <time.h>

#include <shared/report.h>
#include <shared/configfile.h>
#include <shared/sockets.h>
#include <shared/str.h>

#include <libetpan/libetpan.h>

#include "mail.h"
#include "main.h"
#include "constants.h"
#include "maillib.h"
#include "global.h"
#include "servicethread.h"

/* ---------------------- forward declarations ------------------------------------------------- */
static void mail_ignore_handler(void);
static void mail_key_handler(const char *str);

/* ---------------------- constants ------------------------------------------------------------ */
#define MODULE_NAME           "mail"

/* ---------------------- types ---------------------------------------------------------------- */
struct mailbox 
{
    char            *server;
    char            *username;
    char            *password;
    char            *name;
    unsigned int    messages_seen;
    unsigned int    messages_unseen;
    unsigned int    messages_total;
};

struct email
{
    struct mailbox  *box;
    int             message_number_in_box;
    char            subject[MAX_LINE_LEN];
    char            from[MAX_LINE_LEN];
};

/* ------------------------variables ----------------------------------------------------------- */
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

/* --------------------------------------------------------------------------------------------- */
static void update_screen(const char *title, 
                          const char *line1, 
                          const char *line2, 
                          const char *line3)
{
    if (title)
    {
        service_thread_command("widget_set %s title {%s: %s}\n", MODULE_NAME, 
                s_title_prefix, title);
    }

    if (line1)
    {
        service_thread_command("widget_set %s line1 1 2 {%s}\n", MODULE_NAME, line1);
    }

    if (line2)
    {
        service_thread_command("widget_set %s line2 1 3 {%s}\n", MODULE_NAME, line2);
    }

    if (line3)
    {
        service_thread_command("widget_set %s line3 1 4 {%s}\n", MODULE_NAME, line3);
    }
}

/* --------------------------------------------------------------------------------------------- */
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
    for (i = 0; i < s_mailboxes->len; i++)
    {
        struct mailbox *box = g_ptr_array_index(s_mailboxes, i);

        line1_old = line1 ? line1 : "";
        line1 = g_strdup_printf("%s%s:%d ", line1_old, box->name, box->messages_total);

        if (i != 0)
        {
            g_free(line1_old);
        }
    }

    if (s_current_screen < 0)
    {
        s_current_screen = tot - 1;
    }
    else if (s_current_screen >= tot)
    {
        s_current_screen = 0;
    }

    if (tot != 0)
    {
        GList *cur = g_list_first(s_email);

        /* build the second line */
        i = 0;
        while (cur)
        {
            struct email *mail = (struct email *)cur->data;
            if (!mail)
            {
                break;
            }

            if (i++ == s_current_screen)
            {
                line2 = mail->from;
                line3 = mail->subject;
                title = g_strdup_printf("%s %d", mail->box->name,
                                        mail->message_number_in_box);
                break;
            }
            cur = cur->next;
        }
    }

    update_screen(title, line1, line2 ? line2 : "", line3 ? line3 : "");
    g_free(title);
    g_free(line1);
}

/* --------------------------------------------------------------------------------------------- */
void free_emails(void)
{
    GList *cur = g_list_first(s_email);
    while (cur)
    {
        free(cur->data);
        cur = cur->next;
    }
    g_list_free(s_email);
    s_email = NULL;
}


/* --------------------------------------------------------------------------------------------- */
static void mail_check(void)
{
    int mb;

    s_current_screen = 0;

    /* free old mail */
    free_emails();

    for (mb = 0; mb < s_mailboxes->len; mb++)
    {
        struct mailbox           *box       = g_ptr_array_index(s_mailboxes, mb);
        struct mailfolder        *folder    = NULL;
        struct mailmessage_list  *messages  = NULL;
        struct mailmessage       *message   = NULL;
        struct mailstorage       *storage   = NULL;
        int                      r, i;

        if (!box)
        {
            break;
        }

        update_screen(box->name, "", "  Receiving ...", "");

        storage = mailstorage_new(NULL);
        if (!storage) 
        {
            report(RPT_ERR, "error initializing storage\n");
            goto end_loop;
        }

        r = init_storage(storage, get_driver("pop3"), box->server, 110,
                CONNECTION_TYPE_PLAIN, box->username, box->password,
                POP3_AUTH_TYPE_PLAIN, NULL, NULL, NULL);
        if (r != MAIL_NO_ERROR) 
        {
            report(RPT_ERR, "error initializing storage\n");
            goto end_loop;
        }

        /* get the folder structure */
        folder = mailfolder_new(storage, NULL, NULL);
        if (folder == NULL)
        {
            report(RPT_ERR, "mailfolder_new failed\n");
            goto end_loop;
        }

        r = mailfolder_connect(folder);
        if (r != MAIL_NO_ERROR) 
        {
            report(RPT_ERR, "mailfolder_connect failed\n");
            goto end_loop;
        }

        r = mailfolder_status(folder, &box->messages_total, &box->messages_seen,
                &box->messages_unseen);
        if (r != MAIL_NO_ERROR) 
        {
            report(RPT_ERR, "mailfolder_status failed\n");
            goto end_loop;
        }

        r = mailfolder_get_messages_list(folder, &messages);
        if (r != MAIL_NO_ERROR) 
        {
            report(RPT_ERR, "mailfolder_get_message failed\n");
            goto end_loop;
        }

        r = mailfolder_get_envelopes_list(folder, messages);
        if (r != MAIL_NO_ERROR) 
        {
            report(RPT_ERR, "mailfolder_get_mailmessages_list failed\n");
            goto end_loop;
        }

        for (i = 0; i < carray_count(messages->msg_tab); i++)
        {
            clistiter             *cur  = NULL;
            struct mailimf_fields *hdr  = NULL;
            struct email          *mail = NULL;

            message = (struct mailmessage *)carray_get(messages->msg_tab, i);

            /* allocate the email */
            mail = (struct email *)malloc(sizeof(struct email));
            if (!mail)
            {
                report(RPT_ERR, MODULE_NAME ": Out of memory");
                goto end_loop;
            }

            r = mailmessage_fetch_envelope(message, &hdr);
            if (r != MAIL_NO_ERROR) 
            {
                report(RPT_ERR, "mailmessage_fetch_envelope failed\n");
                goto end_loop;
            }

            for (cur = clist_begin(hdr->fld_list) ; cur != NULL; cur = clist_next(cur)) 
            {
                struct mailimf_field *field = (struct mailimf_field *)clist_content(cur);
    
                switch (field->fld_type) 
                {
                      case MAILIMF_FIELD_FROM:
                          display_from(field->fld_data.fld_from, mail->from, MAX_LINE_LEN);
                          break;

                      case MAILIMF_FIELD_SUBJECT:
                          display_subject(field->fld_data.fld_subject, mail->subject, MAX_LINE_LEN);
                          break;
                }
            }

            mail->message_number_in_box = i + 1;
            mail->box = box;
            s_email = g_list_append(s_email, mail);
            CALL_IF_VALID(hdr, mailimf_fields_free);
        }

end_loop:
        CALL_IF_VALID(messages, mailmessage_list_free);
        CALL_IF_VALID(folder, mailfolder_disconnect);
        CALL_IF_VALID(folder, mailfolder_free);
        CALL_IF_VALID(storage, mailstorage_free);
    }
}

/* --------------------------------------------------------------------------------------------- */
static void mail_key_handler(const char *str)
{
    if (strcmp(str, "Up") == 0)
    {
        s_current_screen++;
    }
    else
    {
        s_current_screen--;
    }
    show_screen();
}

/* --------------------------------------------------------------------------------------------- */
static void mail_ignore_handler(void)
{
    s_current_screen++;
    show_screen();
}

/* --------------------------------------------------------------------------------------------- */
static bool mail_init(void)
{
    int        i;
    int        number_of_mailboxes;
    char       *tmp;

    /* register client */
    service_thread_register_client(&mail_client);

    /* add a screen */
    service_thread_command("screen_add " MODULE_NAME "\n");
    service_thread_command("screen_set %s -name %s\n", MODULE_NAME,
                          config_get_string(MODULE_NAME, "name", 0, "Mail"));

    /* add the title */
    service_thread_command("widget_add " MODULE_NAME " title title\n");
    s_title_prefix = g_strdup(config_get_string(MODULE_NAME, "name", 0, "Mail"));

    /* add three lines */
    service_thread_command("widget_add " MODULE_NAME " line1 string\n");
    service_thread_command("widget_add " MODULE_NAME " line2 string\n");
    service_thread_command("widget_add " MODULE_NAME " line3 string\n");

    /* register keys */
    service_thread_command("client_add_key Up\n");
    service_thread_command("client_add_key Down\n");

    /* get config items */
    s_interval = config_get_int(MODULE_NAME, "interval", 0, 300);

    number_of_mailboxes = config_get_int(MODULE_NAME, "number_of_servers", 0, 0);
    if (number_of_mailboxes == 0)
    {
        report(RPT_ERR, MODULE_NAME ": No mailboxes specified");
        return false;
    }

    /* create the linked list of mailboxes */
    s_mailboxes = g_ptr_array_sized_new(number_of_mailboxes);

    /* process the mailboxes */
    for (i = 1; i <= number_of_mailboxes; i++)
    {
        struct mailbox *cur = (struct mailbox *)malloc(sizeof(struct mailbox));
        if (!cur)
        {
            report(RPT_ERR, MODULE_NAME ": Could not create mailbox: Of ouf memory");
            return false;
        }

        tmp = g_strdup_printf("server%d", i);
        cur->server = g_strdup(config_get_string(MODULE_NAME, tmp, 0, ""));
        g_free(tmp);

        tmp = g_strdup_printf("user%d", i);
        cur->username = g_strdup(config_get_string(MODULE_NAME, tmp, 0, ""));
        g_free(tmp);

        tmp = g_strdup_printf("password%d", i);
        cur->password = g_strdup(config_get_string(MODULE_NAME, tmp, 0, ""));
        g_free(tmp);

        tmp = g_strdup_printf("name%d", i);
        cur->name = g_strdup(config_get_string(MODULE_NAME, tmp, 0, cur->server));
        g_free(tmp);

        g_ptr_array_add(s_mailboxes, cur);
    }

    return true;
}

/* --------------------------------------------------------------------------------------------- */
void *mail_run(void *cookie)
{
    int     i;
    time_t  next_check;
    int     result;

    result = config_has_section(MODULE_NAME);
    if (!result)
    {
        report(RPT_INFO, "mail disabled");
        return NULL;
    }

    if (!mail_init())
    {
        return NULL;
    }
    conf_dec_count();

    /* check mails instantly */
    next_check = time(NULL);

    /* dispatcher */
    while (!g_exit)
    {
        g_usleep(100000);

        /* check emails? */
        if (time(NULL) > next_check)
        {
            mail_check();
            show_screen();
            next_check = time(NULL) + s_interval;
        }
    }

    service_thread_unregister_client(MODULE_NAME);
    free_emails();

    for (i = 0; i < s_mailboxes->len; i++)
    {
        struct mailbox *cur = (struct mailbox *)g_ptr_array_index(s_mailboxes, i);
        g_free(cur->server);
        g_free(cur->username);
        g_free(cur->password);
        g_free(cur->name);
        free(cur);
    }
    g_ptr_array_free(s_mailboxes, true);
    g_free(s_title_prefix);

    return NULL;
}

/* vim: set ts=4 sw=4 et: */
