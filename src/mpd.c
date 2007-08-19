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

#include <libmpd/libmpd.h>

#include "mpd.h"
#include "main.h"
#include "constants.h"
#include "global.h"
#include "servicethread.h"
#include "keyfile.h"

/* ---------------------- forward declarations ------------------------------ */
static void mpd_key_handler(const char *str);
static void mpd_menu_handler(const char *event, const char *id, const char *arg);

/* ---------------------- constants ----------------------------------------- */
#define MODULE_NAME           "mpd"
#define RETRY_INTERVAL        5

/* ---------------------- types --------------------------------------------- */
struct song {
    char            *title;
    char            *artist;
};

/* ------------------------variables ---------------------------------------- */
static MpdObj *s_mpd;
static int s_error = 0;
static int s_current_state  = 0;
static bool s_song_displayed = false;
static struct song *s_current_song;
static guint s_stop_time = UINT_MAX;
static GPtrArray *s_current_list = NULL;
static struct connection *s_connection;
static int s_timeout;

static struct client mpd_client = {
    .name            = MODULE_NAME,
    .key_callback    = mpd_key_handler,
    .listen_callback = NULL,
    .ignore_callback = NULL,
    .menu_callback   = mpd_menu_handler
};


/* -------------------------------------------------------------------------- */
static struct song *mpd_song_new(const char *title, const char *artist)
{
    struct song     *song;

    song = g_malloc(sizeof(struct song));
    if (!song) {
        return NULL;
    }

    song->title = g_strdup(title);
    song->artist = g_strdup(artist);

    return song;
}

/* -------------------------------------------------------------------------- */
static void mpd_song_delete(struct song *song)
{
    if (song) {
        g_free(song->title);
        g_free(song->artist);
        g_free(song);
    }
}

/* -------------------------------------------------------------------------- */
static void mpd_free_playlist(GPtrArray *playlist)
{
    int i;

    for (i = 0; i < playlist->len; i++) {
        g_free(playlist->pdata[i]);
    }

    g_ptr_array_free(playlist, true);
}

/* -------------------------------------------------------------------------- */
static GPtrArray *mpd_get_playlists(void)
{
    MpdData     *data;
    GPtrArray   *array;

    array = g_ptr_array_new();

    for (data = mpd_database_get_directory(s_mpd, "/"); 
            data != NULL; data = mpd_data_get_next(data)) {
        switch (data->type) {
            case MPD_DATA_TYPE_PLAYLIST:
                g_ptr_array_add(array, g_path_get_basename(data->playlist));
                g_free(data->playlist);
                data->playlist = NULL;
                break;

            case MPD_DATA_TYPE_DIRECTORY:
            case MPD_INFO_ENTITY_TYPE_SONG:
                g_free(data->directory);
                data->directory = NULL;
                break;

            default:
                break;
        }
    }

    return array;
}

/* -------------------------------------------------------------------------- */
static bool mpd_playlists_equals(GPtrArray *a, GPtrArray *b)
{
    int i;

    if (a->len != b->len) {
        return false;
    }

    for (i = 0; i < a->len; i++) {
        if (g_ascii_strcasecmp((char *)a->pdata[i], (char *)b->pdata[i]) != 0) {
            return false;
        }
    }

    return true;
}

/* -------------------------------------------------------------------------- */
static void mpd_update_playlist_menu(void)
{
    bool        add = false;
    int         i;
    GPtrArray   *old, *new;
    
    old = s_current_list;
    new = mpd_get_playlists();

    /* if no old playlist, simply add the whole playlist */
    if (!old) {
        add = true;
    } else if (!mpd_playlists_equals(old, new)) {
        service_thread_command("menu_del_item \"\" mpd_pl\n");
        add = true;
    }

    if (add) {
        service_thread_command("menu_add_item \"\" mpd_pl menu {Playlists}\n");
        service_thread_command("menu_add_item mpd_pl mpd_pl_0 action {%s}\n",
                "== Clear ==");

        for (i = 0; i < new->len; i++) {
            service_thread_command("menu_add_item mpd_pl mpd_pl_%d action {%s}\n",
                    i + 1, (char *)new->pdata[i]);
        }
    }

    s_current_list = new;
    if (old)
        mpd_free_playlist(old);
}

/* -------------------------------------------------------------------------- */
static bool mpd_song_compare(const struct song *a, const struct song *b)
{
    return a && b && (g_ascii_strcasecmp(a->title, b->title) == 0) &&
           (g_ascii_strcasecmp(a->artist, b->artist) == 0);
}

/* -------------------------------------------------------------------------- */
static struct song *mpd_get_current_song(void)
{
    mpd_Song        *current;
    gchar           **strings;
    struct song     *ret;

    current = mpd_playlist_get_current_song(s_mpd);
    if (!current || s_current_state != MPD_PLAYER_PLAY || !current) {
        ret = mpd_song_new("", "");
    }

    if (!current->title) {
        ret = mpd_song_new("(unknown)", "");
    } else {
        strings = g_strsplit(current->title, " - ", 2);
        if (g_strv_length(strings) == 2) {
            ret = mpd_song_new(strings[0], strings[1]);
        } else {
            ret = mpd_song_new(current->artist ? current->artist : "",
                    current->title ? current->title : "");
        }
        g_strfreev(strings);
    }

    return ret;
}

/* -------------------------------------------------------------------------- */
static void mpd_key_handler(const char *str)
{
    if (g_ascii_strcasecmp(str, "Up") == 0) {
        /* if playing, next song */
        if (s_current_state == MPD_PLAYER_PLAY) {
            mpd_player_next(s_mpd);
        } else {
            mpd_player_stop(s_mpd);
        }
    } else {
        if (s_current_state == MPD_PLAYER_PLAY) {
            mpd_player_pause(s_mpd);
        } else {
            mpd_player_play(s_mpd);
        }
    }
}

/* -------------------------------------------------------------------------- */
static void mpd_menu_handler(const char *event, const char *id, const char *arg)
{
    char **ids;

    if (strlen(id) == 0) {
        return;
    }

    ids = g_strsplit(id, "_", 2);

    if ((g_ascii_strcasecmp(ids[0], "pl") == 0) && (ids[1] != NULL)) {
        int no = atoi(ids[1]) - 1;

        if (no == -1) {
            mpd_playlist_clear(s_mpd);
            mpd_playlist_queue_commit(s_mpd);
        } else if (s_current_list && (no < s_current_list->len)) {
            char *list;
            list = g_strconcat( s_current_list->pdata[no], NULL);
            mpd_playlist_queue_load(s_mpd, list);
            mpd_playlist_queue_commit(s_mpd);

            if (s_current_state != MPD_PLAYER_PLAY) {
                mpd_player_play(s_mpd);
            }
            
            g_free(list);
        }
    } else if ((g_ascii_strcasecmp(ids[0], "standby") == 0)) {
        int min = atoi(arg) * 15;
        if (min == 0) {
            s_stop_time = INT_MAX;
        } else {
            s_stop_time = time(NULL) + 60 * min;
        }
    }

    g_strfreev(ids);
}

/* -------------------------------------------------------------------------- */
static void mpd_error_handler(MpdObj *mpd, int id, char *msg, void *ptr)
{
    report(RPT_ERR, "MPD Error: %s", msg);
}

/* -------------------------------------------------------------------------- */
static void mpd_update_status_time(void)
{
    int             elapsed, total;
    char            *line3;
    struct song     *cur_song;

    if (s_current_state != MPD_PLAYER_PLAY) {
        return;
    }

    /* song ? */
    cur_song = mpd_get_current_song();
    if (!mpd_song_compare(cur_song, s_current_song) || !s_song_displayed) {
        mpd_song_delete(s_current_song);
        s_current_song = cur_song;

        service_thread_command("widget_set %s line1 1 2 {%s}\n", MODULE_NAME,
                cur_song->artist);
        service_thread_command("widget_set %s line2 1 3 {%s}\n", MODULE_NAME,
                cur_song->title);

        s_song_displayed = true;
    } else {
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

    service_thread_command("widget_set %s line3 1 4 {%10s}\n",
            MODULE_NAME, line3);
    g_free(line3);
}

/* -------------------------------------------------------------------------- */
static void mpd_state_changed_handler(MpdObj *mi, ChangedStatusType what, void *userdata)
{
    char *str;
    s_current_state = mpd_player_get_state(s_mpd);
    report(RPT_DEBUG, "State changed, %d\n", s_current_state);

    s_song_displayed = false;

    switch (s_current_state) {
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

/* -------------------------------------------------------------------------- */
static void mpd_connection_changed_handler(MpdObj *mi, int connect, void *userdata)
{
    if (connect) {
        report(RPT_DEBUG, "Connected");
    } else {
        s_error = true;
        report(RPT_ERR, "Disconnected");
    }
}

/* -------------------------------------------------------------------------- */
static bool mpd_init_connection(void)
{
    char    *server = NULL, *password = NULL;
    int     port;

    /* get config items */
    server = key_file_get_string_default(MODULE_NAME, "server", "localhost");
    password = key_file_get_string_default(MODULE_NAME, "password", "");
    port = key_file_get_integer_default(MODULE_NAME, "port", 6600);
    s_timeout = key_file_get_integer_default(MODULE_NAME, "timeout", 10);

    /* set the global connection */
    s_connection = connection_new(server, password, port);
    g_free(server);
    g_free(password);
    if (!s_connection)
        return false;
    
    return true;
}

/* -------------------------------------------------------------------------- */
static bool mpd_start_connection(void)
{
    int     err;

    s_error = false;

    /* create the current song */
    mpd_song_delete(s_current_song);
    s_current_song = mpd_song_new("", "");

    /* create the object */
    if (s_mpd) {
        mpd_free(s_mpd);
        s_mpd = NULL;
    }
    s_mpd = mpd_new(s_connection->host, s_connection->port, s_connection->password);

    /* connect signal handlers */
    mpd_signal_connect_error(s_mpd, mpd_error_handler, NULL);
    mpd_signal_connect_status_changed(s_mpd, mpd_state_changed_handler, NULL);
    mpd_signal_connect_connection_changed(s_mpd, mpd_connection_changed_handler, NULL);

    /* set timeout */
    mpd_set_connection_timeout(s_mpd, s_timeout);
    if (s_error) {
        mpd_disconnect(s_mpd);
        return false;
    }

    /* connect */
    err = mpd_connect(s_mpd);
    if (err != MPD_OK || s_error) {
        report(RPT_ERR, "Failed to connect: %d", err);
        return false;
    }

    return true;
}

/* -------------------------------------------------------------------------- */
static bool mpd_init(void)
{
    char     *string;

    /* register client */
    service_thread_register_client(&mpd_client);

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
    string = key_file_get_string_default_l1(MODULE_NAME, "name", "Music Player");
    service_thread_command("widget_set %s title {%s}\n", MODULE_NAME, string);
    g_free(string);

    return true;
}

/* -------------------------------------------------------------------------- */
static void mpd_deinit(void)
{
    service_thread_command("screen_del " MODULE_NAME "\n");
}

/* -------------------------------------------------------------------------- */
void *mpd_run(void *cookie)
{
    time_t      next_update, current;
    gboolean    result;
    int         retry_count = RETRY_INTERVAL;

    result = key_file_has_group(MODULE_NAME);
    if (!result) {
        report(RPT_INFO, "mpd disabled");
        conf_dec_count();
        return NULL;
    }

    if (!mpd_init())
        goto out;

    if (!mpd_init_connection())
        goto out_screen;
    if (!mpd_start_connection())
        goto out_screen;

    /* do first update instantly */
    next_update = time(NULL);

    conf_dec_count();

    /* dispatcher */
    while (!g_exit) {

        /* if we are in error state, try to retrieve a connection first */
        if (s_error) {
            if (retry_count-- <= 0) { /* each minute */
                if (mpd_start_connection()) {
                    s_error = false;
                } else {
                    retry_count = RETRY_INTERVAL;
                }
            }

            if (s_error) {
                g_usleep(1000000);
                continue;
            }
        }

        current = time(NULL);

        g_usleep(1000000);
        mpd_status_queue_update(s_mpd);
        mpd_status_check(s_mpd);
        mpd_update_status_time();

        /* check playlists ? */
        if (current > next_update) {
            mpd_update_playlist_menu();
            next_update = time(NULL) + 60;
        } if (current > s_stop_time) {
            mpd_player_stop(s_mpd);
            s_stop_time = UINT_MAX;
            service_thread_command("menu_set_item \"\" mpd_standby -value 0\n");
        }
    }

out_screen:
    mpd_deinit();

out:
    if (s_mpd)
        mpd_free(s_mpd);
    if (s_current_list)
        mpd_free_playlist(s_current_list);
    service_thread_unregister_client(MODULE_NAME);
    mpd_song_delete(s_current_song);
    connection_delete(s_connection);
    
    return NULL;
}

/* vim: set ts=4 sw=4 et: */
