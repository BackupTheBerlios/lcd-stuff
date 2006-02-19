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
#include <shared/LL.h>

#include <libetpan/libetpan.h>

#include "mail.h"
#include "main.h"
#include "constants.h"
#include "maillib.h"
#include "global.h"
#include "helpfunctions.h"
#include "servicethread.h"

/* ---------------------- forward declarations ------------------------------------------------- */
static void mail_ignore_handler(void);
static void mail_key_handler(const char *str);

/* ---------------------- constants ------------------------------------------------------------ */
#define MODULE_NAME           "mail"

/* ---------------------- types ---------------------------------------------------------------- */
struct mailbox 
{
    char server[BUFSIZ];
    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN];
    char name[MAX_NAME_LEN];
    unsigned int  messages_seen;
    unsigned int  messages_unseen;
    unsigned int  messages_total;
};

struct email
{
    char subject[MAX_LINE_LEN];
    char from[MAX_LINE_LEN];
};

/* ------------------------variables ----------------------------------------------------------- */
static int          s_interval;
static LinkedList   *s_mailboxes;
static LinkedList   *s_email;
static int          s_current_screen = 0;
static int          s_total_messages = 0;
struct client       mail_client = 
                    {
                        .name            = MODULE_NAME,
                        .key_callback    = mail_key_handler,
                        .listen_callback = NULL,
                        .ignore_callback = mail_ignore_handler
                    }; 

/* --------------------------------------------------------------------------------------------- */
static void update_screen(const char *line1, const char *line2, const char *line3)
{
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
    char    line1[MAX_LINE_LEN] = "";
    char    *line2          = NULL;
    char    *line3          = NULL;
    char    buffer[BUFSIZ];
    int     i;

    /* build the first line */
    LL_Rewind(s_mailboxes);
    do
    {
        struct mailbox *box = (struct mailbox *)LL_Get(s_mailboxes);
        if (!box)
        {
            break;
        }

        snprintf(buffer, BUFSIZ, "%s:%d ", box->name, box->messages_total);
        strncat(line1, buffer, MAX_LINE_LEN);
    } while (LL_Next(s_mailboxes) == 0);

    if (s_current_screen < 0)
    {
        s_current_screen = s_total_messages - 1;
    }
    else if (s_current_screen >= s_total_messages)
    {
        s_current_screen = 0;
    }

    if (s_total_messages != 0)
    {
        /* build the second line */
        i = 0;
        LL_Rewind(s_email);
        do
        {
            struct email *mail = (struct email *)LL_Get(s_email);
            if (!mail)
            {
                break;
            }

            if (i++ == s_current_screen)
            {
                line2 = mail->from;
                line3 = mail->subject;
                break;
            }
        } while (LL_Next(s_email) == 0);
    }

    update_screen(line1, line2 ? line2 : "", line3 ? line3 : "");
}


/* --------------------------------------------------------------------------------------------- */
static void mail_check(void)
{
    LL_Rewind(s_mailboxes);

    s_total_messages = 0;
    s_current_screen = 0;

    /* free old mail */
    free_del_LL_contents(s_email);

    LL_Rewind(s_email);
    do 
    {
        struct mailbox           *box       = (struct mailbox *)LL_Get(s_mailboxes);
        struct mailfolder        *folder    = NULL;
        struct mailmessage_list  *messages  = NULL;
        struct mailmessage       *message   = NULL;
        struct mailstorage       *storage   = NULL;
        int                      r, i;

        if (!box)
        {
            break;
        }

        update_screen("Receiving", box->name, "");

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
                          display_from(field->fld_data.fld_from, mail->from, 
                                       MAX_LINE_LEN);
                          break;

                      case MAILIMF_FIELD_SUBJECT:
                          display_subject(field->fld_data.fld_subject, mail->subject, 
                                          MAX_LINE_LEN);
                          break;
                }
            }

            s_total_messages++;
            LL_InsertNode(s_email, mail);
            CALL_IF_VALID(hdr, mailimf_fields_free);
        }

end_loop:
        CALL_IF_VALID(messages, mailmessage_list_free);
        CALL_IF_VALID(folder, mailfolder_disconnect);
        CALL_IF_VALID(folder, mailfolder_free);
        CALL_IF_VALID(storage, mailstorage_free);
    }
    while (LL_Next(s_mailboxes) == 0);
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
    int i;
    int number_of_mailboxes;
    char buffer[BUFSIZ];

    /* register client */
    service_thread_register_client(&mail_client);

    /* create the linked list of mailboxes */
    s_mailboxes = LL_new();
    if (!s_mailboxes)
    {
        report(RPT_ERR, MODULE_NAME ": Could not create mailbox: Of ouf memory");
        return false;
    }

    /* and of emails */
    s_email = LL_new();
    if (!s_email)
    {
        report(RPT_ERR, MODULE_NAME ": Could not create email: Of ouf memory");
        return false;
    }

    /* add a screen */
    service_thread_command("screen_add " MODULE_NAME "\n");
    service_thread_command("screen_set %s -name %s\n", MODULE_NAME,
                          config_get_string(MODULE_NAME, "name", 0, "Mail"));

    /* add the title */
    service_thread_command("widget_add " MODULE_NAME " title title\n");
    service_thread_command("widget_set %s title %s\n", 
                          MODULE_NAME, config_get_string(MODULE_NAME, "name", 0, "Mail"));

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

    /* process the mailboxes */
    for (i = 1; i <= number_of_mailboxes; i++)
    {
        struct mailbox *cur = malloc(sizeof(struct mailbox));
        if (!cur)
        {
            report(RPT_ERR, MODULE_NAME ": Out of memory");
            return false;
        }

        snprintf(buffer, BUFSIZ, "server%d", i);
        strncpy(cur->server, config_get_string(MODULE_NAME, buffer, 0, ""), 
                BUFSIZ);
        cur->server[BUFSIZ-1] = 0;

        snprintf(buffer, BUFSIZ, "user%d", i);
        strncpy(cur->username, config_get_string(MODULE_NAME, buffer, 0, ""), 
                MAX_USERNAME_LEN);
        cur->username[BUFSIZ-1] = 0;

        snprintf(buffer, BUFSIZ, "password%d", i);
        strncpy(cur->password, config_get_string(MODULE_NAME, buffer, 0, ""),
                MAX_PASSWORD_LEN);
        cur->password[BUFSIZ-1] = 0;

        snprintf(buffer, BUFSIZ, "name%d", i);
        strncpy(cur->name, config_get_string(MODULE_NAME, buffer, 0, cur->server), 
                MAX_NAME_LEN);
        cur->name[BUFSIZ-1] = 0;

        LL_AddNode(s_mailboxes, (void *)cur);
    }

    return true;
}

/* --------------------------------------------------------------------------------------------- */
void *mail_run(void *cookie)
{
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
    free_del_LL_contents(s_email);
    LL_Destroy(s_email);
    free_del_LL_contents(s_mailboxes);
    LL_Destroy(s_mailboxes);

    return NULL;
}

/* vim: set ts=4 sw=4 et: */
