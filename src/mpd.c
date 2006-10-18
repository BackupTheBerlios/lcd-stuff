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
#include <shared/sockets.h>
#include <shared/str.h>

#include <libmpd/libmpd.h>

#include "rss.h"
#include "main.h"
#include "constants.h"
#include "global.h"
#include "servicethread.h"
#include "keyfile.h"

/* ---------------------- forward declarations ------------------------------------------------- */
static void mpd_key_handler(const char *str);
static void mpd_menu_handler(const char *event, const char *id, const char *arg);

/* ---------------------- constants ------------------------------------------------------------ */
#define MODULE_NAME           "mpd"

/* ---------------------- types ---------------------------------------------------------------- */
struct song 
{
    char            *title;
    char            *artist;
};

/* ------------------------variables ----------------------------------------------------------- */
static MpdObj      *s_mpd;
static int         s_error          = 0;
static int         s_current_state  = 0;
static bool        s_song_displayed = false;
static struct song *s_current_song;
static guint       s_stop_time      = UINT_MAX;
static GPtrArray   *s_current_list  = NULL;
struct client      mpd_client = 
                   {
                       .name            = MODULE_NAME,
                       .key_callback    = mpd_key_handler,
                       .listen_callback = NULL,
                       .ignore_callback = NULL,
                       .menu_callback   = mpd_menu_handler
                   };


/* --------------------------------------------------------------------------------------------- */
struct song *mpd_song_new(const char *title, const char *artist)
{
    struct song     *song;

    song = g_malloc(sizeof(struct song));
    if (!song)
    {
        return NULL;
    }

    song->title = g_strdup(title);
    song->artist = g_strdup(artist);

    return song;
}

/* --------------------------------------------------------------------------------------------- */
void mpd_song_delete(struct song *song)
{
    if (song) 
    {
        g_free(song->title);
        g_free(song->artist);
        g_free(song);
    }
}

/* --------------------------------------------------------------------------------------------- */
void mpd_free_playlist(GPtrArray *playlist)
{
    int i;

    for (i = 0; i < playlist->len; i++)
    {
        g_free(playlist->pdata[i]);
    }

    g_ptr_array_free(playlist, true);
}

/* --------------------------------------------------------------------------------------------- */
GPtrArray *mpd_get_playlists(void)
{
    MpdData     *data;
    GPtrArray   *array;

    array = g_ptr_array_new();

    for (data = mpd_database_get_directory(s_mpd, "/"); 
            data != NULL; 
            data = mpd_data_get_next(data))
    {
        if (data->type == MPD_DATA_TYPE_PLAYLIST)
        {
            g_ptr_array_add(array, g_path_get_basename(data->playlist));
        }
    }

    return array;
}

/* --------------------------------------------------------------------------------------------- */
bool mpd_playlists_equals(GPtrArray *a, GPtrArray *b)
{
    int i;

    if (a->len != b->len)
    {
        return false;
    }

    for (i = 0; i < a->len; i++)
    {
        if (g_ascii_strcasecmp((char *)a->pdata[i], (char *)b->pdata[i]) != 0)
        {
            return false;
        }
    }

    return true;
}

/* --------------------------------------------------------------------------------------------- */
void mpd_update_playlist_menu(void)
{
    bool        add = false;
    int         i;
    GPtrArray   *old, *new;
    
    old = s_current_list;
    new = mpd_get_playlists();

    /* if no old playlist, simply add the whole playlist */
    if (!old)
    {
        add = true;
    }
    else if (!mpd_playlists_equals(old, new))
    {
        service_thread_command("menu_del_item \"\" mpd_pl\n");
        add = true;
    }

    if (add)
    {
        service_thread_command("menu_add_item \"\" mpd_pl menu {Playlists}\n");
        service_thread_command("menu_add_item mpd_pl mpd_pl_0 action {%s}\n",
                "== Clear ==");

        for (i = 0; i < new->len; i++)
        {
            service_thread_command("menu_add_item mpd_pl mpd_pl_%d action {%s}\n",
                    i + 1, (char *)new->pdata[i]);
        }
    }

    s_current_list = new;
    CALL_IF_VALID(old, mpd_free_playlist);
}

/* --------------------------------------------------------------------------------------------- */
bool mpd_song_compare(const struct song *a, const struct song *b)
{
    return a && b && (g_ascii_strcasecmp(a->title, b->title) == 0) &&
           (g_ascii_strcasecmp(a->artist, b->artist) == 0);
}

/* --------------------------------------------------------------------------------------------- */
struct song *mpd_get_current_song(void)
{
    mpd_Song        *current;
    gchar           **strings;
    struct song     *ret;

    current = mpd_playlist_get_current_song(s_mpd);
    if (s_current_state != MPD_PLAYER_PLAY || !current)
    {
        ret = mpd_song_new("", "");
    }

    if (!current->title)
    {
        ret = mpd_song_new("(unknown)", "");
    }
    else
    {
        strings = g_strsplit(current->title, " - ", 2);
        if (g_strv_length(strings) == 2)
        {
            ret = mpd_song_new(strings[0], strings[1]);
        }
        else
        {
            ret = mpd_song_new(current->artist ? current->artist : "",
                    current->title ? current->title : "");
        }
        g_strfreev(strings);
    }

    return ret;
}

/* --------------------------------------------------------------------------------------------- */
static void mpd_key_handler(const char *str)
{
    if (g_ascii_strcasecmp(str, "Up") == 0)
    {
        /* if playing, next song */
        if (s_current_state == MPD_PLAYER_PLAY)
        {
            mpd_player_next(s_mpd);
        }
        else
        {
            mpd_player_stop(s_mpd);
        }
    }
    else
    {
        if (s_current_state == MPD_PLAYER_PLAY)
        {
            mpd_player_pause(s_mpd);
        }
        else
        {
            mpd_player_play(s_mpd);
        }
    }
}

/* --------------------------------------------------------------------------------------------- */
static void mpd_menu_handler(const char *event, const char *id, const char *arg)
{
    char **ids;

    if (strlen(id) == 0)
    {
        return;
    }

    ids = g_strsplit(id, "_", 2);

    if ((g_ascii_strcasecmp(ids[0], "pl") == 0) && (ids[1] != NULL))
    {
        int no = atoi(ids[1]) - 1;

        if (no == -1)
        {
            mpd_playlist_clear(s_mpd);
            mpd_playlist_queue_commit(s_mpd);
        }
        else if (s_current_list && (no < s_current_list->len))
        {
            char *list;
            list = g_strconcat( s_current_list->pdata[no], NULL);
            mpd_playlist_queue_load(s_mpd, list);
            mpd_playlist_queue_commit(s_mpd);

            if (s_current_state != MPD_PLAYER_PLAY)
            {
                mpd_player_play(s_mpd);
            }
            
            g_free(list);
        }
    }
    else if ((g_ascii_strcasecmp(ids[0], "standby") == 0))
    {
        int min = atoi(arg) * 15;
        if (min == 0)
        {
            s_stop_time = INT_MAX;
        }
        else
        {
            s_stop_time = time(NULL) + 60 * min;
        }
    }

    g_strfreev(ids);
}

/* --------------------------------------------------------------------------------------------- */
static void mpd_error_handler(MpdObj *mpd, int id, char *msg, void *ptr)
{
    report(RPT_ERR, "MPD Error: %s", msg);
}

/* --------------------------------------------------------------------------------------------- */
static void mpd_update_status_time(void)
{
    int             elapsed, total;
    char            *line3;
    struct song     *cur_song;

    if (s_current_state != MPD_PLAYER_PLAY)
    {
        return;
    }

    /* song ? */
    cur_song = mpd_get_current_song();
    if (!mpd_song_compare(cur_song, s_current_song) || !s_song_displayed)
    {
        mpd_song_delete(s_current_song);
        s_current_song = cur_song;

        service_thread_command("widget_set %s line1 1 2 {%s}\n", MODULE_NAME, cur_song->artist);
        service_thread_command("widget_set %s line2 1 3 {%s}\n", MODULE_NAME, cur_song->title);

        s_song_displayed = true;
    }
    else
    {
        mpd_song_delete(s_current_song);
        s_current_song = NULL;
    }

    elapsed = mpd_status_get_elapsed_song_time(s_mpd);
    total = mpd_status_get_total_song_time(s_mpd);

    line3 = g_strdup_printf("%d:%2.2d/%d:%2.2d     %s%s", 
                            elapsed / 60, elapsed % 60,
                            total / 60,   total   % 60,
                            mpd_player_get_repeat(s_mpd) ? "R" : "_",
                            mpd_player_get_random(s_mpd) ? "S" : "_");

    service_thread_command("widget_set %s line3 1 4 {%10s}\n", MODULE_NAME, line3);
    g_free(line3);
}

/* --------------------------------------------------------------------------------------------- */
static void mpd_state_changed_handler(MpdObj *mi, ChangedStatusType what, void *userdata)
{
    char *str;
    s_current_state = mpd_player_get_state(s_mpd);
    report(RPT_DEBUG, "State changed, %d\n", s_current_state);

    s_song_displayed = false;

    switch (s_current_state)
    {
        case MPD_PLAYER_PAUSE:
            str = "paused";
            break;
        case MPD_PLAYER_UNKNOWN:
            str = "unknown";
            break;
        case MPD_PLAYER_STOP:
            str = "stopped";
            break;
        case MPD_PLAYER_PLAY:
            return;
        default:
            str = "";
            break;
    }

    service_thread_command("widget_set %s line1 1 2 {}\n", MODULE_NAME);
    service_thread_command("widget_set %s line2 1 3 {}\n", MODULE_NAME);
    service_thread_command("widget_set %s line3 1 4 {%s}\n", MODULE_NAME, str);
}

/* --------------------------------------------------------------------------------------------- */
static void mpd_connection_changed_handler(MpdObj *mi, int connect, void *userdata)
{
    if (connect)
    {
        report(RPT_DEBUG, "Connected\n");
    }
    else
    {
        s_error = true;
        report(RPT_ERR, "Disconnected\n");
    }
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
    host = key_file_get_string_default(g_key_file, MODULE_NAME, "server", "localhost");
    password = key_file_get_string_default(g_key_file, MODULE_NAME, "password", "");
    port = key_file_get_integer_default(g_key_file, MODULE_NAME, "port", 6600);

    /* create the object */
    s_mpd = mpd_new(host, port, password);
    g_free(host);
    g_free(password);

    /* create the current song */
    s_current_song = mpd_song_new("", "");

    /* connect signal handlers */
    mpd_signal_connect_error(s_mpd, mpd_error_handler, NULL);
    mpd_signal_connect_status_changed(s_mpd, mpd_state_changed_handler, NULL);
    mpd_signal_connect_connection_changed(s_mpd, mpd_connection_changed_handler, NULL);
    
    /* connect */
    mpd_connect(s_mpd);
    if (s_error)
    {
        return false;
    }

    /* set timeout */
    mpd_set_connection_timeout(s_mpd, 
            key_file_get_integer_default(g_key_file, MODULE_NAME, "timeout", 10));
    if (s_error)
    {
        mpd_disconnect(s_mpd);
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

    /* add standby menu */
    service_thread_command("menu_add_item \"\" mpd_standby ring \"Standby\" "
            "-strings \"0\t15\t30\t45\t60\t75\t90\t115\t130\t145\t160\"\n");

    /* set the title */
    service_thread_command("widget_set %s title {%s}\n", MODULE_NAME, 
            key_file_get_string_default(g_key_file, MODULE_NAME, "name", "Music Player"));


    return true;
}

/* --------------------------------------------------------------------------------------------- */
void *mpd_run(void *cookie)
{
    time_t      next_check, current;
    gboolean    result;

    result = g_key_file_has_group(g_key_file, MODULE_NAME);
    if (!result)
    {
        report(RPT_INFO, "mpc disabled");
        conf_dec_count();
        goto song_free;
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
        current = time(NULL);

        g_usleep(1000000);
        mpd_status_queue_update(s_mpd);
        mpd_status_check(s_mpd);
        mpd_update_status_time();

        /* check playlists ? */
        if (current > next_check)
        {
            mpd_update_playlist_menu();
            next_check = time(NULL) + 60;
        }
        if (current > s_stop_time)
        {
            mpd_player_stop(s_mpd);
            s_stop_time = UINT_MAX;
            service_thread_command("menu_set_item \"\" mpd_standby -value 0\n");
        }
    }

out:
    CALL_IF_VALID(s_mpd, mpd_free);
    CALL_IF_VALID(s_current_list, mpd_free_playlist);
    service_thread_unregister_client(MODULE_NAME);
    mpd_song_delete(s_current_song);
song_free:
    
    return NULL;
}

/* vim: set ts=4 sw=4 et: */
