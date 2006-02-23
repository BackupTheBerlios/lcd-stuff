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

#include <libmpd-0.01/libmpd/libmpd.h>

#include "rss.h"
#include "main.h"
#include "constants.h"
#include "global.h"
#include "servicethread.h"

/* ---------------------- forward declarations ------------------------------------------------- */
static void mpd_key_handler(const char *str);

/* ---------------------- constants ------------------------------------------------------------ */
#define MODULE_NAME           "mpd"

/* ---------------------- types ---------------------------------------------------------------- */

/* ------------------------variables ----------------------------------------------------------- */
static MpdObj      *s_mpd;
static int         s_error          = 0;
static int         s_current_state  = 0;
struct client      mpd_client = 
                   {
                       .name            = MODULE_NAME,
                       .key_callback    = mpd_key_handler,
                       .listen_callback = NULL,
                       .ignore_callback = NULL
                   };

/* --------------------------------------------------------------------------------------------- */
static void mpd_key_handler(const char *str)
{
    if (strcmp(str, "Up") == 0)
    {
        /* if playing, next song */
        if (s_current_state == MPD_OB_PLAYER_PLAY)
        {
            mpd_ob_player_next(s_mpd);
        }
        else
        {
            printf("stop\n");
            mpd_ob_player_stop(s_mpd);
        }
    }
    else
    {
        if (s_current_state == MPD_OB_PLAYER_PLAY)
        {
            mpd_ob_player_pause(s_mpd);
        }
        else
        {
            mpd_ob_player_play(s_mpd);
        }
    }
}

/* --------------------------------------------------------------------------------------------- */
static void *mpd_error_handler(MpdObj *mpd, int id, char *msg, void *ptr)
{
    s_error = true;
    report(RPT_ERR, "MPD Error: %s", msg);

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */
static void *mpd_song_changed_handler(MpdObj *mi, int old, int new,void *pointer)
{
    mpd_Song *current;
    gchar    **strings;
    char     *artist;
    char     *title;

    current = mpd_ob_playlist_get_current_song(s_mpd);
    if (s_current_state != MPD_OB_PLAYER_PLAY || !current)
    {
        return NULL;
    }

    if (!current->title)
    {
        artist = "(unknown)";
        title = "";
    }
    else
    {

        strings = g_strsplit(current->title, " - ", 2);
        if (g_strv_length(strings) == 2)
        {
            artist = strings[0];
            title = strings[1];
        }
        else
        {
            artist = current->artist;
            title = current->title;
        }
    }

    service_thread_command("widget_set %s line1 1 2 {%s}\n", MODULE_NAME, artist);
    service_thread_command("widget_set %s line2 1 3 {%s}\n", MODULE_NAME, title);

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */
static void mpd_update_status_time(void)
{
    int elapsed, total;
    char *line3;

    if (s_current_state != MPD_OB_PLAYER_PLAY)
    {
        return;
    }

    elapsed = mpd_ob_status_get_elapsed_song_time(s_mpd);
    total = mpd_ob_status_get_total_song_time(s_mpd);

    line3 = g_strdup_printf("%d:%2.2d/%d:%2.2d", elapsed / 60, elapsed % 60,
                                                 total / 60,   total   % 60);

    service_thread_command("widget_set %s line3 1 4 {%s}\n", MODULE_NAME, line3);
    g_free(line3);
}

/* --------------------------------------------------------------------------------------------- */
static void *mpd_state_changed_handler(MpdObj *mi, int old, int new, void *pointer)
{
    char *str;
    s_current_state = new;
    report(RPT_DEBUG, "State changed, %d\n", new);

    switch (new)
    {
        case MPD_OB_PLAYER_PAUSE:
            str = "paused";
            break;
        case MPD_OB_PLAYER_UNKNOWN:
            str = "unknown";
            break;
        case MPD_OB_PLAYER_STOP:
            str = "stopped";
            break;
        case MPD_OB_PLAYER_PLAY:
            mpd_song_changed_handler(mi, 0, 0, NULL);
            return NULL;
    }

    service_thread_command("widget_set %s line1 1 2 {}\n", MODULE_NAME);
    service_thread_command("widget_set %s line2 1 3 {}\n", MODULE_NAME);
    service_thread_command("widget_set %s line3 1 4 {%s}\n", MODULE_NAME, str);

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */
static void *mpd_disconnect_handler(MpdObj *mi, void *pointer)
{
    s_error = true;
    report(RPT_ERR, "Disconnected\n");

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */
static void *mpd_connect_handler(MpdObj *mi, void *pointer)
{
    report(RPT_DEBUG, "Connected\n");

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */
static bool mpd_init(void)
{
    char     *host;
    char     *password;
    int      port;

    /* register client */
    service_thread_register_client(&mpd_client);

    /* get config items */
    host = g_strdup(config_get_string(MODULE_NAME, "server", 0, "localhost"));
    password = g_strdup(config_get_string(MODULE_NAME, "password", 0, ""));
    port = config_get_int(MODULE_NAME, "port", 0, 6600);

    /* create the object */
    s_mpd = mpd_ob_new(host, port, password);
    g_free(host);
    g_free(password);

    /* connect signal handlers */
    mpd_ob_signal_set_error(s_mpd, mpd_error_handler, NULL);
    mpd_ob_signal_set_song_changed(s_mpd, mpd_song_changed_handler, NULL);
    mpd_ob_signal_set_state_changed(s_mpd, mpd_state_changed_handler, NULL);
    mpd_ob_signal_set_connect(s_mpd, mpd_connect_handler, NULL);
    mpd_ob_signal_set_disconnect(s_mpd, mpd_disconnect_handler, NULL);
    
    /* connect */
    mpd_ob_connect(s_mpd);
    if (s_error)
    {
        return false;
    }

    /* set timeout */
    mpd_ob_set_connection_timeout(s_mpd, config_get_int(MODULE_NAME, "timeout", 0, 10));
    if (s_error)
    {
        mpd_ob_disconnect(s_mpd);
        return false;
    }

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

    /* set the title */
    service_thread_command("widget_set %s title {%s}\n", MODULE_NAME, 
            config_get_string(MODULE_NAME, "name", 0, "Music Player"));


    return true;
}

/* --------------------------------------------------------------------------------------------- */
void *mpd_run(void *cookie)
{
    time_t  next_check;
    int     result;

    result = config_has_section(MODULE_NAME);
    if (!result)
    {
        report(RPT_INFO, "rss disabled");
        goto out;
    }

    if (!mpd_init())
    {
        goto out;
    }
    conf_dec_count();

    /* check mails instantly */
    next_check = time(NULL);

    /* dispatcher */
    while (!g_exit && !s_error)
    {
        g_usleep(1000000);
        mpd_ob_status_queue_update(s_mpd);
        mpd_ob_status_check(s_mpd);
        mpd_update_status_time();
    }

out:
    CALL_IF_VALID(s_mpd, mpd_ob_free);
    service_thread_unregister_client(MODULE_NAME);
    
    return NULL;
}

/* vim: set ts=4 sw=4 et: */
