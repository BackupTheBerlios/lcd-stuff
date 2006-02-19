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
#include <ctype.h>
#include <string.h>
#include <errno.h>

#include <shared/report.h>
#include <shared/configfile.h>
#include <shared/sockets.h>
#include <shared/str.h>
#include <shared/LL.h>

#include <mrss.h>

#include "rss.h"
#include "main.h"
#include "constants.h"
#include "helpfunctions.h"
#include "global.h"
#include "servicethread.h"

/* ---------------------- forward declarations ------------------------------------------------- */
static void rss_ignore_handler(void);
static void rss_key_handler(const char *str);

/* ---------------------- constants ------------------------------------------------------------ */
#define MODULE_NAME           "rss"
#define HEADLINE_LEN          128

/* ---------------------- types ---------------------------------------------------------------- */
struct rss_feed 
{
    char url[MAX_URL_LEN];
    char name[MAX_NAME_LEN];
    int  items;
};

struct newsitem
{
    char *site;
    char headline[HEADLINE_LEN];
};

/* ------------------------variables ----------------------------------------------------------- */
static int         s_interval;
static LinkedList  *s_feeds;
static LinkedList  *s_news;
static int         s_current_screen = 0;
static int         s_total_news = 0;
struct client      rss_client = 
                   {
                       .name            = MODULE_NAME,
                       .key_callback    = rss_key_handler,
                       .listen_callback = NULL,
                       .ignore_callback = rss_ignore_handler
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
static void update_screen_news(void)
{
    int i;

    if (s_current_screen < 0)
    {
        s_current_screen = s_total_news - 1;
    }
    else if (s_current_screen >= s_total_news)
    {
        s_current_screen = 0;
    }

    if (s_total_news != 0)
    {
        i = 0;
        LL_Rewind(s_news);
        do
        {
            struct newsitem *item = (struct newsitem *)LL_Get(s_news);
            if (!item)
            {
                break;
            }

            if (i++ == s_current_screen)
            {
                int cur_line;
                char line[MAX_LINE_LEN];
                char text[MAX_LINE_LEN * LINES];

                memset(text, 0, MAX_LINE_LEN);
                snprintf(text, MAX_LINE_LEN * LINES, "%s: %s", 
                         item->site, item->headline);
                text[MAX_LINE_LEN * LINES - 1] = 0;

                for (cur_line = 0; cur_line < (LINES-1); cur_line++)
                {
                    strncpy(line, text + cur_line*g_display_width, g_display_width);
                    line[g_display_width] = 0;
                    service_thread_command("widget_set %s line%d 1 %d {%s}\n", 
                            MODULE_NAME,  cur_line+1, cur_line+2, line);
                }
                break;
            }
        } while (LL_Next(s_news) == 0);
    }
}

/* --------------------------------------------------------------------------------------------- */
static void rss_check(void)
{
    LL_Rewind(s_feeds);

    s_total_news = 0;
    s_current_screen = 0;

    /* free old mail */
    free_del_LL_contents(s_news);

    LL_Rewind(s_news);
    do 
    {
        struct rss_feed           *feed       = (struct rss_feed *)LL_Get(s_feeds);
        mrss_error_t              err_read;
        mrss_t                    *data_cur = NULL;
        mrss_item_t               *item_cur = NULL;
        int                       i = 0;

        if (!feed)
        {
            break;
        }

        update_screen("Receiving", feed->name, "");
        
        err_read = mrss_parse_url(feed->url, &data_cur);
		if (err_read != MRSS_OK)
		{
			report(RPT_ERR, "Error reading RSS feed: %s", mrss_strerror(err_read));
            continue;
		}

        item_cur = data_cur->item;
        while(item_cur && i++ < feed->items)
        {
            /* create a new newsitem */
            struct newsitem *newsitem = (struct newsitem *)malloc(sizeof(struct newsitem));
            if (!newsitem)
            {
                report(RPT_ERR, MODULE_NAME ": Out of memory");
                goto end_loop;
            }

            to_latin1(data_cur->encoding, item_cur->title, 
                      newsitem->headline, HEADLINE_LEN);
            
            newsitem->site = feed->name;
                    

            item_cur = item_cur->next;

            LL_InsertNode(s_news, newsitem);
            s_total_news ++;
        }

end_loop:
        CALL_IF_VALID(data_cur, mrss_free);
    }
    while (LL_Next(s_feeds) == 0);

}

/* --------------------------------------------------------------------------------------------- */
static void rss_key_handler(const char *str)
{
    if (strcmp(str, "Up") == 0)
    {
        s_current_screen++;
    }
    else
    {
        s_current_screen--;
    }
    update_screen_news();
}

/* --------------------------------------------------------------------------------------------- */
static void rss_ignore_handler(void)
{
    s_current_screen++;
    update_screen_news();
}

/* --------------------------------------------------------------------------------------------- */
static bool rss_init(void)
{
    int      i;
    int      number_of_feeds;
    char     buffer[BUFSIZ];

    /* register client */
    service_thread_register_client(&rss_client);

    /* create the linked list of mailboxes */
    s_news = LL_new();
    if (!s_news)
    {
        report(RPT_ERR, MODULE_NAME ": Could not create s_news: Of ouf memory");
        return false;
    }

    /* and of emails */
    s_feeds = LL_new();
    if (!s_feeds)
    {
        report(RPT_ERR, MODULE_NAME ": Could not create s_feeds: Of ouf memory");
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

    number_of_feeds = config_get_int(MODULE_NAME, "number_of_feeds", 0, 0);
    if (number_of_feeds == 0)
    {
        report(RPT_ERR, MODULE_NAME ": No feed sources specified");
        return false;
    }

    /* process the mailboxes */
    for (i = 1; i <= number_of_feeds; i++)
    {
        struct rss_feed *cur = malloc(sizeof(struct rss_feed));
        if (!cur)
        {
            report(RPT_ERR, MODULE_NAME ": Out of memory");
            return false;
        }

        snprintf(buffer, BUFSIZ, "url%d", i);
        strncpy(cur->url, config_get_string(MODULE_NAME, buffer, 0, ""), MAX_URL_LEN);
        cur->url[MAX_URL_LEN-1] = 0;

        snprintf(buffer, BUFSIZ, "name%d", i);
        strncpy(cur->name, config_get_string(MODULE_NAME, buffer, 0, ""), MAX_NAME_LEN);
        cur->name[MAX_NAME_LEN-1] = 0;

        snprintf(buffer, BUFSIZ, "items%d", i);
        cur->items = config_get_int(MODULE_NAME, buffer, 0, 0);

        LL_AddNode(s_feeds, (void *)cur);
    }

    return true;
}

/* --------------------------------------------------------------------------------------------- */
void *rss_run(void *cookie)
{
    time_t  next_check;
    int     result;
    int     count = 0;

    result = config_has_section(MODULE_NAME);
    if (!result)
    {
        report(RPT_INFO, "rss disabled");
        return NULL;
    }

    if (!rss_init())
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
            rss_check();
            update_screen_news();
            next_check = time(NULL) + s_interval;
        }
    }

    service_thread_unregister_client(MODULE_NAME);
    free_del_LL_contents(s_feeds);
    LL_Destroy(s_feeds);
    free_del_LL_contents(s_news);
    LL_Destroy(s_news);

    return NULL;
}

/* vim: set ts=4 sw=4 et: */
