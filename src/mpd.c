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
#include "screen.h"

/* ---------------------- constants ----------------------------------------- */
#define MODULE_NAME           "mpd"
#define RETRY_INTERVAL        5

/* ---------------------- types --------------------------------------------- */
struct song {
    char            *title;
    char            *artist;
};

/* ------------------------variables ---------------------------------------- */
struct lcd_stuff_mpd {
    struct lcd_stuff    *lcd;
    MpdObj              *mpd;
    int                 error;
    int                 current_state;
    bool                song_displayed;
    struct song         *current_song;
    guint               stop_time;
    GPtrArray           *current_list;
    struct connection   *connection;
    struct screen       screen;
    int                 timeout;
};

/* -------------------------------------------------------------------------- */
static void mpd_song_set_title(struct song *song, const char *title)
{
    song->title = g_convert_with_fallback(title, -1,
            "iso-8859-1", "utf-8", "?", NULL, NULL, NULL);
    if (!song->title)
        song->title = g_strdup("(Unknown)");
}

/* -------------------------------------------------------------------------- */
static void mpd_song_set_artist(struct song *song, const char *artist)
{
    song->artist = g_convert_with_fallback(artist, -1,
            "iso-8859-1", "utf-8", "?", NULL, NULL, NULL);
    if (!song->artist)
        song->artist = g_strdup("(Unknown)");
}

/* -------------------------------------------------------------------------- */
static struct song *mpd_song_new(const char *title, const char *artist)
{
    struct song *song;

    song = g_malloc(sizeof(struct song));
    if (!song)
        return NULL;

    mpd_song_set_title(song, title);
    mpd_song_set_artist(song, artist);

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
static GPtrArray *mpd_get_playlists(struct lcd_stuff_mpd *mpd)
{
    MpdData     *data;
    GPtrArray   *array;

    array = g_ptr_array_new();

    for (data = mpd_database_get_directory(mpd->mpd, "/");
            data != NULL; data = mpd_data_get_next(data)) {
        switch (data->type) {
            case MPD_DATA_TYPE_PLAYLIST:
                g_ptr_array_add(array, g_path_get_basename(data->playlist->path));
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
static void mpd_update_playlist_menu(struct lcd_stuff_mpd *mpd)
{
    bool        add = false;
    int         i;
    GPtrArray   *old_list, *new_list;

    old_list = mpd->current_list;
    new_list = mpd_get_playlists(mpd);

    /* if no old playlist, simply add the whole playlist */
    if (!old_list) {
        add = true;
    } else if (!mpd_playlists_equals(old_list, new_list)) {
        service_thread_command(mpd->lcd->service_thread, "menu_del_item \"\" mpd_pl\n");
        add = true;
    }

    if (add) {
        service_thread_command(mpd->lcd->service_thread,
                               "menu_add_item \"\" mpd_pl menu {Playlists}\n");
        service_thread_command(mpd->lcd->service_thread,
                               "menu_add_item mpd_pl mpd_pl_0 action {%s}\n",
                               "== Clear ==");

        for (i = 0; i < new_list->len; i++) {
            service_thread_command(mpd->lcd->service_thread,
                                   "menu_add_item mpd_pl mpd_pl_%d action {%s}\n",
                                   i + 1, (char *)new_list->pdata[i]);
        }
    }

    mpd->current_list = new_list;
    if (old_list)
        mpd_free_playlist(old_list);
}

/* -------------------------------------------------------------------------- */
static bool mpd_song_compare(const struct song *a, const struct song *b)
{
    return a && b && (g_ascii_strcasecmp(a->title, b->title) == 0) &&
           (g_ascii_strcasecmp(a->artist, b->artist) == 0);
}

/* -------------------------------------------------------------------------- */
static struct song *mpd_get_current_song(struct lcd_stuff_mpd *mpd)
{
    mpd_Song        *current;
    gchar           **strings;
    struct song     *ret;

    current = mpd_playlist_get_current_song(mpd->mpd);
    if (!current || mpd->current_state != MPD_PLAYER_PLAY) {
        ret = mpd_song_new("", "");
    } else if (!current->title) {
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
static void mpd_key_handler(const char *str, void *cookie)
{
    struct lcd_stuff_mpd *mpd = (struct lcd_stuff_mpd *)cookie;

    if (g_ascii_strcasecmp(str, "Up") == 0) {
        /* if playing, next song */
        if (mpd->current_state == MPD_PLAYER_PLAY) {
            mpd_player_next(mpd->mpd);
        } else {
            mpd_player_stop(mpd->mpd);
        }
    } else {
        if (mpd->current_state == MPD_PLAYER_PLAY) {
            mpd_player_pause(mpd->mpd);
        } else {
            mpd_player_play(mpd->mpd);
        }
    }
}

/* -------------------------------------------------------------------------- */
static void mpd_menu_handler(const char *event, const char *id, const char *arg, void *cookie)
{
    char **ids;
    struct lcd_stuff_mpd *mpd = (struct lcd_stuff_mpd *)cookie;

    if (strlen(id) == 0) {
        return;
    }

    ids = g_strsplit(id, "_", 2);

    if ((g_ascii_strcasecmp(ids[0], "pl") == 0) && (ids[1] != NULL)) {
        int no = atoi(ids[1]) - 1;

        if (no == -1) {
            mpd_playlist_clear(mpd->mpd);
            mpd_playlist_queue_commit(mpd->mpd);
        } else if (mpd->current_list && (no < mpd->current_list->len)) {
            char *list;
            list = g_strconcat(mpd->current_list->pdata[no], NULL);
            mpd_playlist_queue_load(mpd->mpd, list);
            mpd_playlist_queue_commit(mpd->mpd);

            if (mpd->current_state != MPD_PLAYER_PLAY) {
                mpd_player_play(mpd->mpd);
            }

            g_free(list);
        }
    } else if ((g_ascii_strcasecmp(ids[0], "standby") == 0)) {
        int min = atoi(arg) * 15;
        if (min == 0) {
            mpd->stop_time = INT_MAX;
        } else {
            mpd->stop_time = time(NULL) + 60 * min;
        }
    }

    g_strfreev(ids);
}

/* -------------------------------------------------------------------------- */
static int mpd_error_handler(MpdObj *mpd, int id, char *msg, void *ptr)
{
    report(RPT_ERR, "MPD Error: %s", msg);
    return FALSE; /* don't disconnect */
}

/* -------------------------------------------------------------------------- */
static void mpd_update_status_time(struct lcd_stuff_mpd *mpd)
{
    int             elapsed, total;
    char            *line3;
    struct song     *cur_song;

    if (mpd->current_state != MPD_PLAYER_PLAY)
        return;

    /* song ? */
    cur_song = mpd_get_current_song(mpd);
    if (!mpd_song_compare(cur_song, mpd->current_song) || !mpd->song_displayed) {
        mpd_song_delete(mpd->current_song);
        mpd->current_song = cur_song;

        screen_show_text(&mpd->screen, 0, cur_song->artist);
        screen_show_text(&mpd->screen, 1, cur_song->title);

        mpd->song_displayed = true;
    } else {
        mpd_song_delete(mpd->current_song);
        mpd->current_song = NULL;
    }

    elapsed = mpd_status_get_elapsed_song_time(mpd->mpd);
    total = mpd_status_get_total_song_time(mpd->mpd);

    line3 = g_strdup_printf("%d:%2.2d/%d:%2.2d     %s%s",
                            elapsed / 60, elapsed % 60,
                            total / 60,   total   % 60,
                            mpd_player_get_repeat(mpd->mpd) ? "R" : "_",
                            mpd_player_get_random(mpd->mpd) ? "S" : "_");
    screen_show_text(&mpd->screen, 2, line3);
    g_free(line3);
}

/* -------------------------------------------------------------------------- */
static void mpd_state_changed_handler(MpdObj *mi, ChangedStatusType what, void *userdata)
{
    struct lcd_stuff_mpd *mpd = (struct lcd_stuff_mpd *)userdata;
    char *str;
    mpd->current_state = mpd_player_get_state(mpd->mpd);
    report(RPT_DEBUG, "State changed, %d\n", mpd->current_state);

    mpd->song_displayed = false;

    switch (mpd->current_state) {
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

    screen_clear(&mpd->screen);
}

/* -------------------------------------------------------------------------- */
static void mpd_connection_changed_handler(MpdObj *mi, int connect, void *userdata)
{
    struct lcd_stuff_mpd *mpd = (struct lcd_stuff_mpd *)userdata;

    if (connect) {
        report(RPT_DEBUG, "Connected");
    } else {
        mpd->error = true;
        report(RPT_ERR, "Disconnected");
    }
}

/* -------------------------------------------------------------------------- */
static bool mpd_init_connection(struct lcd_stuff_mpd *mpd)
{
    char    *server = NULL, *password = NULL;
    int     port;

    /* get config items */
    server = key_file_get_string_default(MODULE_NAME, "server", "localhost");
    password = key_file_get_string_default(MODULE_NAME, "password", "");
    port = key_file_get_integer_default(MODULE_NAME, "port", 6600);
    mpd->timeout = key_file_get_integer_default(MODULE_NAME, "timeout", 10);

    /* set the global connection */
    mpd->connection = connection_new(server, password, port);
    g_free(server);
    g_free(password);
    if (!mpd->connection)
        return false;

    return true;
}

/* -------------------------------------------------------------------------- */
static bool mpd_start_connection(struct lcd_stuff_mpd *mpd)
{
    int     err;

    mpd->error = false;

    /* create the current song */
    mpd_song_delete(mpd->current_song);
    mpd->current_song = mpd_song_new("", "");

    /* create the object */
    if (mpd->mpd) {
        mpd_free(mpd->mpd);
        mpd->mpd = NULL;
    }
    mpd->mpd = mpd_new(mpd->connection->host, mpd->connection->port, mpd->connection->password);

    /* connect signal handlers */
    mpd_signal_connect_error(mpd->mpd, mpd_error_handler, mpd);
    mpd_signal_connect_status_changed(mpd->mpd, mpd_state_changed_handler, mpd);
    mpd_signal_connect_connection_changed(mpd->mpd, mpd_connection_changed_handler, mpd);

    /* set timeout */
    mpd_set_connection_timeout(mpd->mpd, mpd->timeout);
    if (mpd->error) {
        mpd_disconnect(mpd->mpd);
        return false;
    }

    /* connect */
    err = mpd_connect(mpd->mpd);
    if (err != MPD_OK || mpd->error) {
        report(RPT_ERR, "Failed to connect: %d", err);
        return false;
    }

    return true;
}

/* -------------------------------------------------------------------------- */
static struct client mpd_client = {
    .name            = MODULE_NAME,
    .key_callback    = mpd_key_handler,
    .menu_callback   = mpd_menu_handler
};

/* -------------------------------------------------------------------------- */
static bool mpd_init(struct lcd_stuff_mpd *mpd)
{
    char     *string;

    /* register client */
    service_thread_register_client(mpd->lcd->service_thread, &mpd_client, mpd);

    screen_create(&mpd->screen, mpd->lcd, MODULE_NAME);

    /* register keys */
    service_thread_command(mpd->lcd->service_thread,
                           "client_add_key Up\n");
    service_thread_command(mpd->lcd->service_thread,
                           "client_add_key Down\n");

    /* add standby menu */
    service_thread_command(mpd->lcd->service_thread,
                           "menu_add_item \"\" mpd_standby ring \"Standby\" "
                           "-strings \"0\t15\t30\t45\t60\t75\t90\t115\t130\t145\t160\"\n");

    /* set the title */
    string = key_file_get_string_default_l1(MODULE_NAME, "name", "Music Player");
    screen_set_title(&mpd->screen, string);
    g_free(string);

    return true;
}

/* -------------------------------------------------------------------------- */
static void mpd_deinit(struct lcd_stuff_mpd *mpd)
{
    screen_destroy(&mpd->screen);
}

/* -------------------------------------------------------------------------- */
void *mpd_run(void *cookie)
{
    time_t next_update, current;
    gboolean result;
    int retry_count = RETRY_INTERVAL;
    struct lcd_stuff_mpd mpd;

    /* default values */
    mpd.lcd = (struct lcd_stuff *)cookie;
    mpd.mpd = NULL;
    mpd.error = 0;
    mpd.current_state = 0;
    mpd.song_displayed = false;
    mpd.current_song = NULL;
    mpd.stop_time = UINT_MAX;
    mpd.current_list = NULL;
    mpd.connection = NULL;
    mpd.timeout = 0;

    result = key_file_has_group(MODULE_NAME);
    if (!result) {
        report(RPT_INFO, "mpd disabled");
        conf_dec_count();
        return NULL;
    }

    if (!mpd_init(&mpd))
        goto out;

    if (!mpd_init_connection(&mpd))
        goto out_screen;
    if (!mpd_start_connection(&mpd))
        goto out_screen;

    /* do first update instantly */
    next_update = time(NULL);

    conf_dec_count();

    /* dispatcher */
    while (!g_exit) {

        /* if we are in error state, try to retrieve a connection first */
        if (mpd.error) {
            if (retry_count-- <= 0) { /* each minute */
                if (mpd_start_connection(&mpd)) {
                    mpd.error = false;
                } else {
                    retry_count = RETRY_INTERVAL;
                }
            }

            if (mpd.error) {
                g_usleep(1000000);
                continue;
            }
        }

        current = time(NULL);

        g_usleep(1000000);
        mpd_status_queue_update(mpd.mpd);
        mpd_status_check(mpd.mpd);
        mpd_update_status_time(&mpd);

        /* check playlists ? */
        if (current > next_update) {
            mpd_update_playlist_menu(&mpd);
            next_update = time(NULL) + 60;
        } if (current > mpd.stop_time) {
            mpd_player_stop(mpd.mpd);
            mpd.stop_time = UINT_MAX;
            service_thread_command(mpd.lcd->service_thread,
                                   "menu_set_item \"\" mpd_standby -value 0\n");
        }
    }

out_screen:
    mpd_deinit(&mpd);

out:
    if (mpd.mpd)
        mpd_free(mpd.mpd);
    if (mpd.current_list)
        mpd_free_playlist(mpd.current_list);
    service_thread_unregister_client(mpd.lcd->service_thread, MODULE_NAME);
    mpd_song_delete(mpd.current_song);
    connection_delete(mpd.connection);

    return NULL;
}

/* vim: set ts=4 sw=4 et: */
