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
#include <ctype.h>
#include <string.h>
#include <errno.h>

#include <shared/report.h>
#include <shared/sockets.h>
#include <shared/str.h>

#include <mrss.h>

#include "rss.h"
#include "main.h"
#include "constants.h"
#include "global.h"
#include "servicethread.h"
#include "keyfile.h"

/* ---------------------- forward declarations ------------------------------ */
static void rss_ignore_handler(void);
static void rss_key_handler(const char *str);

/* ---------------------- constants ----------------------------------------- */
#define MODULE_NAME           "rss"

/* ---------------------- types --------------------------------------------- */
struct rss_feed {
    char *url;
    char *name;
    int  items;
};

struct newsitem {
    char *site;
    char *headline;
};

/* ------------------------variables ---------------------------------------- */
static int              s_interval;
static GPtrArray        *s_feeds;
static GList            *s_news          = NULL;
static int              s_current_screen = 0;
static struct client    rss_client = {
                           .name            = MODULE_NAME,
                           .key_callback    = rss_key_handler,
                           .listen_callback = NULL,
                           .ignore_callback = rss_ignore_handler
                        };

/* -------------------------------------------------------------------------- */
static void update_screen(const char *title, 
                          const char *line1, 
                          const char *line2, 
                          const char *line3)
{
    if (title) {
        service_thread_command("widget_set %s title {%s}\n", MODULE_NAME, title);
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
static void update_screen_news(void)
{
    int     i;
    int     tot = g_list_length(s_news);

    if (s_current_screen < 0) {
        s_current_screen = tot - 1;
    } else if (s_current_screen >= tot) {
        s_current_screen = 0;
    }

    if (tot != 0) {
        GList *cur = g_list_first(s_news);
        i = 0;

        while (cur) {
            struct newsitem *item = (struct newsitem *)cur->data;
            if (!item) {
                break;
            }

            if (i++ == s_current_screen) {
                int     cur_line;
                char    line[MAX_LINE_LEN];
                char    text[MAX_LINE_LEN * LINES];
                char    *text_start = text;
                int     len = strlen(item->headline);

                memset(text, 0, MAX_LINE_LEN);
                snprintf(text, MAX_LINE_LEN * LINES, "%s", item->headline);
                text[MAX_LINE_LEN * LINES - 1] = 0;

                service_thread_command("widget_set %s title {%s}\n", 
                        MODULE_NAME,  item->site);

                for (cur_line = 0; cur_line < (LINES-1); cur_line++) {
                    char *linestart = line;

                    if (text_start - text < len) {
                        strncpy(linestart, text_start, MAX_LINE_LEN);

                        /* strip leading spaces */
                        while (*linestart == ' ') {
                            linestart++;
                            text_start++;
                        }

                        linestart[g_display_width] = 0;
                        text_start += g_display_width;
                    } else {
                        linestart = "";
                    }

                    service_thread_command("widget_set %s line%d 1 %d {%s}\n",
                            MODULE_NAME,  cur_line+1, cur_line+2, linestart);
                }
                break;
            }

            cur = cur->next;
        }
    }
}

/* -------------------------------------------------------------------------- */
void free_news(void)
{
    GList *cur = g_list_first(s_news);
    while (cur) {
        struct newsitem *item = (struct newsitem *)cur->data;
        free(item->headline);
        free(cur->data);
        cur = cur->next;
    }
    g_list_free(s_news);
    s_news = NULL;
}

/* -------------------------------------------------------------------------- */
static void rss_check(void)
{
    int nf;

    s_current_screen = 0;

    /* free old mail */
    free_news();

    for (nf = 0; nf < s_feeds->len; nf++) {
        struct rss_feed           *feed       = g_ptr_array_index(s_feeds, nf);
        mrss_error_t              err_read;
        mrss_t                    *data_cur = NULL;
        mrss_item_t               *item_cur = NULL;
        int                       i = 0;

        if (!feed) {
            break;
        }

        update_screen(feed->name, "", "  Receiving ...", "");
        
        err_read = mrss_parse_url(feed->url, &data_cur);
		if (err_read != MRSS_OK) {
			report(RPT_ERR, "Error reading RSS feed: %s", mrss_strerror(err_read));
            continue;
		}

        item_cur = data_cur->item;
        while(item_cur && i++ < feed->items) {
            gsize written;

            /* create a new newsitem */
            struct newsitem *newsitem = (struct newsitem *)malloc(sizeof(struct newsitem));
            if (!newsitem) {
                report(RPT_ERR, MODULE_NAME ": Out of memory");
                goto end_loop;
            }

            newsitem->headline = g_convert(item_cur->title, -1, "ISO-8859-1",
                                           data_cur->encoding, NULL, &written, NULL);
            if (!newsitem->headline) {
                newsitem->headline = g_strdup("");
            }
            
            newsitem->site = feed->name;

            s_news = g_list_append(s_news, newsitem);
            item_cur = item_cur->next;
        }

end_loop:
        if (data_cur)
            mrss_free(data_cur);
    }
}

/* -------------------------------------------------------------------------- */
static void rss_key_handler(const char *str)
{
    if (strcmp(str, "Up") == 0) {
        s_current_screen++;
    } else {
        s_current_screen--;
    }
    update_screen_news();
}

/* -------------------------------------------------------------------------- */
static void rss_ignore_handler(void)
{
    s_current_screen++;
    update_screen_news();
}

/* -------------------------------------------------------------------------- */
static bool rss_init(void)
{
    int      i;
    int      number_of_feeds;
    char     *tmp;

    /* register client */
    service_thread_register_client(&rss_client);

    /* add a screen */
    service_thread_command("screen_add " MODULE_NAME "\n");

    /* add the title */
    service_thread_command("widget_add " MODULE_NAME " title title\n");

    /* add three lines */
    service_thread_command("widget_add " MODULE_NAME " line1 string\n");
    service_thread_command("widget_add " MODULE_NAME " line2 string\n");
    service_thread_command("widget_add " MODULE_NAME " line3 string\n");

    /* register keys */
    service_thread_command("client_add_key Up\n");
    service_thread_command("client_add_key Down\n");

    /* get config items */
    s_interval = key_file_get_integer_default(MODULE_NAME, "interval", 1800);

    number_of_feeds = key_file_get_integer_default(MODULE_NAME, "number_of_feeds", 0);
    if (number_of_feeds == 0) {
        report(RPT_ERR, MODULE_NAME ": No feed sources specified");
        return false;
    }

    /* create the linked list of mailboxes */
    s_feeds = g_ptr_array_sized_new(number_of_feeds);

    /* process the mailboxes */
    for (i = 1; i <= number_of_feeds; i++) {
        struct rss_feed *cur = malloc(sizeof(struct rss_feed));
        if (!cur) {
            report(RPT_ERR, MODULE_NAME ": Out of memory");
            return false;
        }

        tmp = g_strdup_printf("url%d", i);
        cur->url = key_file_get_string_default(MODULE_NAME, tmp, "");
        g_free(tmp);

        tmp = g_strdup_printf("name%d", i);
        cur->name = key_file_get_string_default_l1(MODULE_NAME, tmp, "");
        g_free(tmp);

        tmp = g_strdup_printf("items%d", i);
        cur->items = key_file_get_integer_default(MODULE_NAME, tmp, 0);
        g_free(tmp);

        g_ptr_array_add(s_feeds, cur);
    }

    return true;
}

/* -------------------------------------------------------------------------- */
void *rss_run(void *cookie)
{
    int     i;
    time_t  next_check;
    int     result;

    result = key_file_has_group(MODULE_NAME);
    if (!result) {
        report(RPT_INFO, "rss disabled");
        conf_dec_count();
        return NULL;
    }

    if (!rss_init()) {
        report(RPT_ERR, "rss_init failed");
        return NULL;
    }
    conf_dec_count();

    /* check mails instantly */
    next_check = time(NULL);

    /* dispatcher */
    while (!g_exit) {
        g_usleep(1000000);

        /* check emails? */
        if (time(NULL) > next_check) {
            rss_check();
            update_screen_news();
            next_check = time(NULL) + s_interval;
        }
    }

    service_thread_unregister_client(MODULE_NAME);

    for (i = 0; i < s_feeds->len; i++) {
        struct rss_feed *cur = (struct rss_feed *)g_ptr_array_index(s_feeds, i);
        g_free(cur->url);
        g_free(cur->name);
        free(cur);
    }
    g_ptr_array_free(s_feeds, true);
    
    free_news();

    return NULL;
}

/* vim: set ts=4 sw=4 et: */
