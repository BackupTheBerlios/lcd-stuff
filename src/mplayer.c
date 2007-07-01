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

#define MAX_ARGS  20

/* ---------------------- forward declarations ------------------------------ */
static void mplayer_key_handler(const char *str);
static void mplayer_menu_handler(const char *event, const char *id, const char *arg);

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
};

/* ------------------------variables ---------------------------------------- */
static int s_channel_number = 0;
static struct program s_current_mplayer;
static struct channel *s_channels;

static struct client mpd_client = {
    .name            = MODULE_NAME,
    .key_callback    = mplayer_key_handler,
    .listen_callback = NULL,
    .ignore_callback = NULL,
    .menu_callback   = mplayer_menu_handler
};


/* -------------------------------------------------------------------------- */
static void mplayer_key_handler(const char *str)
{
    if (s_current_mplayer.pid <= 0)
        return;

    if (g_ascii_strcasecmp(str, "Up") == 0)
        s_current_mplayer.pause_play_request = true;
    else
        s_current_mplayer.stop_request = true;
}

/* -------------------------------------------------------------------------- */
static void mplayer_check_child(void)
{
    pid_t ret;
    int   status;

    ret = waitpid(s_current_mplayer.pid, &status, WNOHANG);
    if (ret != s_current_mplayer.pid || 
            !(WIFEXITED(status) || WIFSIGNALED(status))) {
        return;
    }

    /* process has exited */
    s_current_mplayer.pid = 0;

    /* clear all */
    service_thread_command("widget_set %s line1 1 2 {}\n", MODULE_NAME);
    service_thread_command("widget_set %s line2 1 3 {}\n", MODULE_NAME);
    service_thread_command("widget_set %s line3 1 4 {}\n", MODULE_NAME);
    service_thread_command("screen_set " MODULE_NAME " -priority hidden\n");
}

/* -------------------------------------------------------------------------- */
static void mplayer_wait_for_playback(void)
{
    char buffer[BUFSIZ];

    while (fgets(buffer, BUFSIZ, s_current_mplayer.output_stream)) {
        if (strncmp(buffer, "Starting playback...", 
                    strlen("Starting playback...")) == 0)
            break;
    }
}

/* -------------------------------------------------------------------------- */
static void mplayer_update_metainfo(void)
{
    char buffer[BUFSIZ];
    char *artist = NULL, *title = NULL;
    int ret;

    /*
     * mplayer_kill() was issued but the child has not been handled
     * by waitpid()
     */
    if (!s_current_mplayer.input_stream || !s_current_mplayer.output_stream)
        return;

    /*
     * artist
     */

    ret = fprintf(s_current_mplayer.input_stream, "get_meta_artist\n");
    if (ret < 0)
        return;

    while (fgets(buffer, BUFSIZ, s_current_mplayer.output_stream)) {
        if (strncmp(buffer, "ANS_META_ARTIST=", strlen("ANS_META_ARTIST=")) == 0) {
            artist = g_strdup(buffer + strlen("ANS_META_ARTIST=") + 1);
            artist[strlen(artist)-2] = 0;
            break;
        }
    }

    /*
     * title
     */

    fprintf(s_current_mplayer.input_stream, "get_meta_title\n");
    if (ret < 0)
        return;

    while (fgets(buffer, BUFSIZ, s_current_mplayer.output_stream)) {
        if (strncmp(buffer, "ANS_META_TITLE=", strlen("ANS_META_TITLE=")) == 0) {
            title = g_strdup(buffer + strlen("ANS_META_TITLE=") + 1);
            title[strlen(title)-2] = 0;
            break;
        }
    }

    service_thread_command("widget_set %s line1 1 2 {%s}\n", MODULE_NAME,
            artist ? artist : "");
    service_thread_command("widget_set %s line2 1 3 {%s}\n", MODULE_NAME,
            title ? title : "");

    if (artist)
        g_free(artist);
    if (title)
        g_free(title);
}

/* -------------------------------------------------------------------------- */
void mplayer_play_pause(void)
{
    fprintf(s_current_mplayer.input_stream, "p\n");
    s_current_mplayer.paused = !s_current_mplayer.paused;
}

/* -------------------------------------------------------------------------- */
static void mplayer_kill(void)
{
    if (s_current_mplayer.input_stream) {
        fclose(s_current_mplayer.input_stream);
        s_current_mplayer.input_stream = NULL;
    }
    if (s_current_mplayer.output_stream) {
        fclose(s_current_mplayer.output_stream);
        s_current_mplayer.output_stream = NULL;
    }

    kill(s_current_mplayer.pid, SIGTERM);
}

/* -------------------------------------------------------------------------- */
static void mplayer_start_program(int no)
{
    struct channel *channel = &s_channels[no];
    bool           ret;
    char           *args[MAX_ARGS];
    int            i = 0;
    int            pid;

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
            &s_current_mplayer.input_fd,    /* standard_input */
            &s_current_mplayer.output_fd,   /* standard_output */
            &s_current_mplayer.error_fd,    /* standard_error */
            NULL                            /* error */
    );
    if (!ret) {
        report(RPT_ERR, "g_spawn_async_with_pipes failed");
        return;
    }

    s_current_mplayer.input_stream = fdopen(s_current_mplayer.input_fd, "w");
    if (!s_current_mplayer.input_stream) {
        report(RPT_ERR, "fdopen input stream failed: %s", strerror(errno));
        mplayer_kill();
        return;
    }
    setlinebuf(s_current_mplayer.input_stream);

    s_current_mplayer.output_stream = fdopen(s_current_mplayer.output_fd, "r");
    if (!s_current_mplayer.output_stream) {
        report(RPT_ERR, "fdopen output stream failed: %s", strerror(errno));
        fclose(s_current_mplayer.input_stream);
        mplayer_kill();
        return;
    }
    setlinebuf(s_current_mplayer.output_stream);

    /* show screen */
    service_thread_command("screen_set " MODULE_NAME " -priority info\n");

    mplayer_wait_for_playback();

    s_current_mplayer.pid = pid;
}

/* -------------------------------------------------------------------------- */
static void mplayer_menu_handler(const char *event, const char *id, const char *arg)
{
    char **ids;

    if (strlen(id) == 0) {
        return;
    }

    ids = g_strsplit(id, "_", 2);

    if ((g_ascii_strcasecmp(ids[0], "ch") == 0) && (ids[1] != NULL)) {
        int no = atoi(ids[1]) - 1;

        mplayer_start_program(no);
    }

    g_strfreev(ids);
}

/* -------------------------------------------------------------------------- */
static bool mplayer_init(void)
{
    char     *string;
    int      i, j;
    char     **channels;

    /* register client */
    service_thread_register_client(&mpd_client);

    /* add a invisible screen */
    service_thread_command("screen_add " MODULE_NAME "\n");
    service_thread_command("screen_set " MODULE_NAME " -priority hidden\n");

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
    string = key_file_get_string_default_l1(MODULE_NAME, "name", "Webradio");
    service_thread_command("widget_set %s title {%s}\n", MODULE_NAME, string);
    g_free(string);

    /* get number of channels in the file */
    channels = key_file_get_keys(MODULE_NAME, &s_channel_number);

    s_channels = g_new(struct channel, s_channel_number);
    if (!s_channels) {
        report(RPT_ERR, "g_new failed for s_channels");
        return false;
    }

    service_thread_command("menu_add_item \"\" mplayer_ch menu {Web-Channels}\n");

    for (i = 0, j = 0; channels[i]; i++, j++) {
        char *value;

        /* keys that are no channels */
        if (strcmp(channels[i], "name") == 0) {
            s_channel_number--;
            j--;
            continue;
        }

        s_channels[j].name = g_strdup(channels[i]);

        value = key_file_get_string(MODULE_NAME, s_channels[j].name);
        if (!value) {
            report(RPT_ERR, "Value %s missing", value);
            continue;
        }

        s_channels[j].playlist = g_str_has_prefix(value, "playlist");
        if (s_channels[j].playlist) {
            s_channels[j].url = g_strdup(value + strlen("playlist"));
            g_free(value);
        } else
            s_channels[j].url = value;

        g_strchug(s_channels[j].url);

        service_thread_command("menu_add_item mplayer_ch mplayer_ch_%d action {%s}\n",
                j+1, s_channels[j].name);
        service_thread_command("menu_set_item mplayer_ch mplayer_ch_%d "
                "-menu_result quit\n", j+1);
    }

    g_strfreev(channels);

    return true;
}

/* -------------------------------------------------------------------------- */
static void mplayer_deinit(void)
{
    service_thread_command("screen_del " MODULE_NAME "\n");
}

/* -------------------------------------------------------------------------- */
void *mplayer_run(void *cookie)
{
    gboolean    result;
    int         ret;

    result = key_file_has_group(MODULE_NAME);
    if (!result) {
        report(RPT_INFO, "mplayer disabled");
        conf_dec_count();
        return NULL;
    }

    ret = mplayer_init();
    conf_dec_count();

    if (!ret)
        goto out;

    /* dispatcher */
    while (!g_exit) {
        g_usleep(1000000);

        if (s_current_mplayer.pid <= 0)
            continue;

        if (s_current_mplayer.stop_request) {
            mplayer_kill();
            s_current_mplayer.stop_request = false;
        } else if (s_current_mplayer.pause_play_request) {
            mplayer_play_pause();
            s_current_mplayer.pause_play_request = false;
        } else if (!s_current_mplayer.paused)
            mplayer_update_metainfo();

        mplayer_check_child();
    }

    mplayer_deinit();

out:
    service_thread_unregister_client(MODULE_NAME);
    
    return NULL;
}

/* vim: set ts=4 sw=4 et: */
