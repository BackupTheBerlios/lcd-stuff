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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <shared/report.h>

#include "main.h"
#include "constants.h"
#include "global.h"
#include "servicethread.h"
#include "keyfile.h"
#include "util.h"
#include "screen.h"

#define MAX_ARGS  20

/* ---------------------- constants ----------------------------------------- */
#define MODULE_NAME           "mplayer"

/* ---------------------- types --------------------------------------------- */
struct channel {
    char            *name;
    char            *url;
    bool            playlist;
};

struct program {
    int             pid;
    int             input_fd;
    int             output_fd;
    int             error_fd;
    FILE            *input_stream;
    FILE            *output_stream;
    bool            stop_request;
    bool            pause_play_request;
    bool            paused;
    int             channel_number;
};

struct lcd_stuff_mplayer {
    struct lcd_stuff    *lcd;
    int                 channel_number;
    struct program      current;
    struct channel      *channels;
    struct screen       screen;
};

/* -------------------------------------------------------------------------- */
static void mplayer_key_handler(const char *str, void *cookie)
{
    struct lcd_stuff_mplayer *mplayer = (struct lcd_stuff_mplayer *)cookie;

    if (mplayer->current.pid <= 0)
        return;

    if (g_ascii_strcasecmp(str, "Up") == 0)
        mplayer->current.pause_play_request = true;
    else
        mplayer->current.stop_request = true;
}

/* -------------------------------------------------------------------------- */
static void mplayer_check_child(struct lcd_stuff_mplayer *mplayer)
{
    pid_t ret;
    int   status;

    ret = waitpid(mplayer->current.pid, &status, WNOHANG);
    if (ret != mplayer->current.pid ||
            !(WIFEXITED(status) || WIFSIGNALED(status))) {
        return;
    }

    /* process has exited */
    mplayer->current.pid = 0;

    /* clear all */
    screen_clear(&mplayer->screen);
    screen_set_priority(&mplayer->screen, "hidden");
}

/* -------------------------------------------------------------------------- */
static void mplayer_wait_for_playback(struct lcd_stuff_mplayer *mplayer)
{
    char buffer[BUFSIZ];

    while (fgets(buffer, BUFSIZ, mplayer->current.output_stream)) {
        if (starts_with(buffer, "Starting playback..."))
            break;
    }
}

/* -------------------------------------------------------------------------- */
static void mplayer_update_metainfo(struct lcd_stuff_mplayer *mplayer)
{
    char buffer[BUFSIZ];
    char *artist = NULL, *title = NULL;
    int ret;

    /*
     * mplayer_kill() was issued but the child has not been handled
     * by waitpid()
     */
    if (!mplayer->current.input_stream || !mplayer->current.output_stream)
        return;

    /*
     * artist
     */

    ret = fprintf(mplayer->current.input_stream, "get_meta_artist\n");
    if (ret >= 0) {
        while (fgets(buffer, BUFSIZ, mplayer->current.output_stream)) {
            if (starts_with(buffer, "ANS_META_ARTIST=")) {
                artist = g_strdup(buffer + strlen("ANS_META_ARTIST=") + 1);
                artist[strlen(artist)-2] = 0;
                break;
            }
        }
    }

    /*
     * title
     */

    fprintf(mplayer->current.input_stream, "get_meta_title\n");
    if (ret >= 0) {
        while (fgets(buffer, BUFSIZ, mplayer->current.output_stream)) {
            if (starts_with(buffer, "ANS_META_TITLE=")) {
                title = g_strdup(buffer + strlen("ANS_META_TITLE=") + 1);
                title[strlen(title)-2] = 0;
                break;
            }
        }
    }

    if (mplayer->current.channel_number >= 0 && mplayer->current.channel_number < mplayer->channel_number)
        screen_show_text(&mplayer->screen, 0, mplayer->channels[mplayer->current.channel_number].name);
    screen_show_text(&mplayer->screen, 1, artist ? artist : "");
    screen_show_text(&mplayer->screen, 2, title ? title : "");

    g_free(artist);
    g_free(title);
}

/* -------------------------------------------------------------------------- */
void mplayer_play_pause(struct lcd_stuff_mplayer *mplayer)
{
    fprintf(mplayer->current.input_stream, "p\n");
    mplayer->current.paused = !mplayer->current.paused;
}

/* -------------------------------------------------------------------------- */
static void mplayer_kill(struct lcd_stuff_mplayer *mplayer)
{
    if (mplayer->current.input_stream) {
        fclose(mplayer->current.input_stream);
        mplayer->current.input_stream = NULL;
    }
    if (mplayer->current.output_stream) {
        fclose(mplayer->current.output_stream);
        mplayer->current.output_stream = NULL;
    }

    kill(mplayer->current.pid, SIGTERM);
}

/* -------------------------------------------------------------------------- */
static void mplayer_start_program(struct lcd_stuff_mplayer *mplayer, int no)
{
    struct channel *channel;
    bool           ret;
    char           *args[MAX_ARGS];
    int            i = 0;
    int            pid;

    if (no >= mplayer->channel_number)
        return;

    /* check if mplayer is running -- if so, don't start a new one */
    if (mplayer->current.pid > 0)
        return;

    channel = &mplayer->channels[no];

    args[i++] = "mplayer";
    args[i++] = "-quiet";
    args[i++] = "-slave";

    if (channel->playlist)
        args[i++] = "-playlist";

    args[i++] = channel->url;
    args[i++] = NULL;

    ret = g_spawn_async_with_pipes(
            NULL,                           /* working_directory */
            args,                           /* arguments */
            NULL,                           /* environment */
            G_SPAWN_DO_NOT_REAP_CHILD |
            G_SPAWN_SEARCH_PATH,            /* flags */
            NULL,                           /* child_setup */
            NULL,                           /* user_data */
            &pid,                           /* child_pid */
            &mplayer->current.input_fd,     /* standard_input */
            &mplayer->current.output_fd,    /* standard_output */
            &mplayer->current.error_fd,     /* standard_error */
            NULL                            /* error */
    );
    if (!ret) {
        report(RPT_ERR, "g_spawn_async_with_pipes failed");
        return;
    }

    mplayer->current.input_stream = fdopen(mplayer->current.input_fd, "w");
    if (!mplayer->current.input_stream) {
        report(RPT_ERR, "fdopen input stream failed: %s", strerror(errno));
        mplayer_kill(mplayer);
        return;
    }
    setlinebuf(mplayer->current.input_stream);

    mplayer->current.output_stream = fdopen(mplayer->current.output_fd, "r");
    if (!mplayer->current.output_stream) {
        report(RPT_ERR, "fdopen output stream failed: %s", strerror(errno));
        fclose(mplayer->current.input_stream);
        mplayer_kill(mplayer);
        return;
    }
    setlinebuf(mplayer->current.output_stream);

    /* show screen */
    screen_set_priority(&mplayer->screen, "info");

    mplayer_wait_for_playback(mplayer);

    mplayer->current.pid = pid;
    mplayer->current.channel_number = no;
}

/* -------------------------------------------------------------------------- */
static void mplayer_menu_handler(const char *event, const char *id, const char *arg, void *cookie)
{
    char **ids;
    struct lcd_stuff_mplayer *mplayer = (struct lcd_stuff_mplayer *)cookie;

    if (strlen(id) == 0) {
        return;
    }

    ids = g_strsplit(id, "_", 2);

    if ((g_ascii_strcasecmp(ids[0], "ch") == 0) && (ids[1] != NULL)) {
        int no = atoi(ids[1]) - 1;

        mplayer_start_program(mplayer, no);
    }

    g_strfreev(ids);
}

/* -------------------------------------------------------------------------- */
static void mplayer_net_handler(char **args, int fd, void *cookie)
{
    int i;
    struct lcd_stuff_mplayer *mplayer = (struct lcd_stuff_mplayer *)cookie;

    if (!args[0])
        return;

    if (starts_with(args[0], "streams")) {
        char buffer[1024];
        ssize_t to_write;
        ssize_t written;

        for (i = 0; i < mplayer->channel_number; i++) {
            snprintf(buffer, 1024, "%s\n", mplayer->channels[i].name);

            to_write = strlen(buffer);
            written = write(fd, buffer, to_write);
            if (written != to_write)
                report(RPT_ERR, "write() failed: ", strerror(errno));
        }

        to_write = strlen("__END__");
        written = write(fd, "__END__", to_write);
        if (written != to_write) {
            report(RPT_ERR, "write() failed: ", strerror(errno));
        }
    } else if (starts_with(args[0], "play") && args[1]) {
        int no = atoi(args[1]);

        mplayer_start_program(mplayer, no);
    } else if (starts_with(args[0], "stop"))
        mplayer->current.stop_request = true;
    else if (starts_with(args[0], "pause_play"))
        mplayer->current.pause_play_request = true;
}

/* -------------------------------------------------------------------------- */
static const struct client mpd_client = {
    .name            = MODULE_NAME,
    .key_callback    = mplayer_key_handler,
    .listen_callback = NULL,
    .ignore_callback = NULL,
    .menu_callback   = mplayer_menu_handler,
    .net_callback    = mplayer_net_handler
};

/* -------------------------------------------------------------------------- */
static bool mplayer_init(struct lcd_stuff_mplayer *mplayer)
{
    char     *string;
    int      i, j;
    char     **channels;

    /* register client */
    service_thread_register_client(mplayer->lcd->service_thread, &mpd_client, mplayer);

    /* add a invisible screen */
    screen_create(&mplayer->screen, mplayer->lcd, MODULE_NAME);
    screen_set_priority(&mplayer->screen, "hidden");

    /* register keys */
    service_thread_command(mplayer->lcd->service_thread,
                           "client_add_key Up\n");
    service_thread_command(mplayer->lcd->service_thread,
                           "client_add_key Down\n");

    /* set the title */
    string = key_file_get_string_default_l1(MODULE_NAME, "name", "Webradio");
    screen_set_title(&mplayer->screen, string);
    g_free(string);

    /* get number of channels in the file */
    channels = key_file_get_keys(MODULE_NAME, (size_t *)&mplayer->channel_number);

    mplayer->channels = g_new(struct channel, mplayer->channel_number);
    if (!mplayer->channels) {
        report(RPT_ERR, "g_new failed for s_channels");
        return false;
    }

    service_thread_command(mplayer->lcd->service_thread,
                           "menu_add_item \"\" mplayer_ch menu {Web-Channels}\n");

    for (i = 0, j = 0; channels[i]; i++, j++) {
        char *value;

        /* keys that are no channels */
        if (strcmp(channels[i], "name") == 0) {
            mplayer->channel_number--;
            j--;
            continue;
        }

        mplayer->channels[j].name = g_strdup(channels[i]);

        value = key_file_get_string(MODULE_NAME, mplayer->channels[j].name);
        if (!value) {
            report(RPT_ERR, "Value %s missing", value);
            continue;
        }

        mplayer->channels[j].playlist = g_str_has_prefix(value, "playlist");
        if (mplayer->channels[j].playlist) {
            mplayer->channels[j].url = g_strdup(value + strlen("playlist"));
            g_free(value);
        } else
            mplayer->channels[j].url = value;

        g_strchug(mplayer->channels[j].url);

        service_thread_command(mplayer->lcd->service_thread,
                               "menu_add_item mplayer_ch mplayer_ch_%d action {%s}\n",
                               j+1, mplayer->channels[j].name);
        service_thread_command(mplayer->lcd->service_thread,
                               "menu_set_item mplayer_ch mplayer_ch_%d "
                               "-menu_result quit\n", j+1);
    }

    g_strfreev(channels);

    return true;
}

/* -------------------------------------------------------------------------- */
static void mplayer_deinit(struct lcd_stuff_mplayer *mplayer)
{
    screen_destroy(&mplayer->screen);
}

/* -------------------------------------------------------------------------- */
void *mplayer_run(void *cookie)
{
    gboolean    result;
    int         ret;
    struct lcd_stuff_mplayer mplayer;

    memset(&mplayer, 0, sizeof(struct lcd_stuff_mplayer));
    mplayer.lcd = (struct lcd_stuff *)cookie;

    result = key_file_has_group(MODULE_NAME);
    if (!result) {
        report(RPT_INFO, "mplayer disabled");
        conf_dec_count();
        return NULL;
    }

    ret = mplayer_init(&mplayer);
    conf_dec_count();

    if (!ret)
        goto out;

    /* dispatcher */
    while (!g_exit) {
        g_usleep(1000000);

        if (mplayer.current.pid <= 0)
            continue;

        if (mplayer.current.stop_request) {
            mplayer_kill(&mplayer);
            mplayer.current.stop_request = false;
        } else if (mplayer.current.pause_play_request) {
            mplayer_play_pause(&mplayer);
            mplayer.current.pause_play_request = false;
        } else if (!mplayer.current.paused)
            mplayer_update_metainfo(&mplayer);

        mplayer_check_child(&mplayer);
    }

    mplayer_deinit(&mplayer);

out:
    service_thread_unregister_client(mplayer.lcd->service_thread, MODULE_NAME);

    return NULL;
}

/* vim: set ts=4 sw=4 et: */
