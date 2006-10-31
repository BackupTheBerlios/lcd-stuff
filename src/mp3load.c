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

#include "rss.h"
#include "main.h"
#include "constants.h"
#include "global.h"
#include "servicethread.h"
#include "keyfile.h"
#include "util.h"

/* ---------------------- forward declarations ------------------------------------------------- */
static void mp3load_key_handler(const char *str);
static void mp3load_menu_handler(const char *event, const char *id, const char *arg);

/* ---------------------- constants ------------------------------------------------------------ */
#define MODULE_NAME           "mp3load"

/* ---------------------- types ---------------------------------------------------------------- */

/* ------------------------variables ----------------------------------------------------------- */
static volatile int     s_button_pressed = BT_None;
static GMutex           *s_mutex = NULL;
static GCond            *s_condition = NULL;
static char             *s_source_directory = NULL;
static char             *s_target_directory = NULL;
static char             **s_extensions = NULL;
static unsigned int     s_extension_len = 0;
static char             *s_mount_command = NULL;
static char             *s_umount_command = NULL;
static char             *s_size = NULL;
static struct client    mpd_client = {
                           .name            = MODULE_NAME,
                           .key_callback    = mp3load_key_handler,
                           .listen_callback = NULL,
                           .ignore_callback = NULL,
                           .menu_callback   = mp3load_menu_handler
                        };


/* --------------------------------------------------------------------------------------------- */
static void update_screen(const char *line1, const char *line2, const char *line3)
{
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

/* --------------------------------------------------------------------------------------------- */
#if 0
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
#endif
/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
static void mp3load_key_handler(const char *str)
{
    if (g_ascii_strcasecmp(str, "Up") == 0) {
        s_button_pressed = BT_Up;
    } else {
        s_button_pressed = BT_Down;
    }
}

/* --------------------------------------------------------------------------------------------- */
static unsigned long long bytes_to_copy(const char      *path, 
                                        const char      *size_desc, 
                                        GError          **gerr)
{
    GError              *gerr_result;
    double              result = 0;
    char                *unit = NULL;

    /* parse the number part of the string */
    result = g_strtod(size_desc, &unit);

    g_assert(unit != NULL);
    g_strstrip(unit);

    /* get the unit */
    if (strlen(unit) == 0) {
        /* unit is bytes, return */
        return result;
    }

    if (g_strcasecmp(unit, "m") == 0) {
        return (unsigned long long)(result * 1024ULL * 1024ULL);
    } else if (g_strcasecmp(unit, "k") == 0) {
        return (unsigned long long)(result * 1024ULL);
    } else if (g_strcasecmp(unit, "g") == 0) {
        return (unsigned long long)(result * 1024ULL * 1024ULL * 1024ULL);
    } else if (g_strcasecmp(unit, "%") == 0) {
        unsigned long long free_bytes;

        free_bytes = get_free_bytes(path, &gerr_result);
        if (free_bytes == 0) {
            g_propagate_error(gerr, gerr_result);
            return 0;
        }

        if (result > 100.0) {
            g_set_error(gerr, g_lcdstuff_quark, 0, "More than 100 %% are invalid");
            return 0;
        }

        return (unsigned long long)(result * free_bytes / 100.0);
    } else {
        g_set_error(gerr, g_lcdstuff_quark, 0, "Invalid unit: '%s'", unit);
        return 0;
    }
}

/* --------------------------------------------------------------------------------------------- */
static bool file_collect_function(const char    *filename, 
                                  void          *cookie, 
                                  GError        **gerr)
{
    GPtrArray   *array = (GPtrArray *)cookie;
    bool        extension_ok = false;
    int         i;

    for (i = 0; i < s_extension_len; i++) {
        if (g_str_has_suffix(filename, s_extensions[i])) {
            extension_ok = true;
            break;
        }
    }

    if (extension_ok) {
        g_ptr_array_add(array, g_strdup(filename));
    }

    return true;
}

/* --------------------------------------------------------------------------------------------- */
static void mp3load_fill_player(void)
{
    GError              *gerr_result = NULL;
    bool                ret;
    long long           bytes = 0;
    GPtrArray           *files = NULL;
    GRand               *randomizer = NULL;
    int                 count = 0;

    /* make screen visible */
    service_thread_command("screen_set " MODULE_NAME " -priority alert\n");

    /*
     * ask the user to press Up if he really wants to continue ------------------------------------
     */

    s_button_pressed = BT_None;
    update_screen("This command will", "destroy data. Cont.?", "[Up=Continue]");

    /* wait until a button was pressed */
    while (s_button_pressed == BT_None && !g_exit) {
        g_usleep(G_USEC_PER_SEC / 10);
    }

    if (g_exit || s_button_pressed == BT_Down) {
        goto end;
    }

    /*
     * mount -------------------------------------------------------------------------------------
     */
    if (s_mount_command && strlen(s_mount_command) > 0) {
        int err;

        err = system(s_mount_command);
        if (err != 0) {
            char buffer[1024];
            update_screen("Mounting the device", "failed, aborting", "");
            strerror_r(errno, buffer, 1024);
            report(RPT_ERR, "mount failed: %s",buffer);
            g_usleep(2 * G_USEC_PER_SEC);
            goto end;
        }
    }

    /*
     * up pressed, continue to fill the stick  ---------------------------------------------------
     */
    
    /* delete the files on the stick */
    ret = delete_directory_recursively(s_target_directory, &gerr_result);
    if (!ret) {
        update_screen("Error while", "deleting files,", "aborted");
        report(RPT_ERR, "%s", gerr_result->message);
        g_error_free(gerr_result);
        g_usleep(2 * G_USEC_PER_SEC);
        goto end_umount;
    }

    /* calculate the size */
    bytes = bytes_to_copy(s_target_directory, s_size, &gerr_result);
    if (bytes == 0) {
        update_screen("Error while", "calculating size,", "aborted");
        report(RPT_ERR, "%s", gerr_result->message);
        g_error_free(gerr_result);
        g_usleep(2 * G_USEC_PER_SEC);
        goto end_umount;
    }

    printf("bytes to copy: %llu\n", bytes);

    /* collect the files */
    update_screen("Collecting files", "Please be patient", "");
    files = g_ptr_array_new();
    if (!filewalk(s_source_directory, file_collect_function, files, FWF_NO_FLAGS, &gerr_result))
    {
        update_screen("Error while", "collecting files,", "aborted");
        report(RPT_ERR, "%s", gerr_result->message);
        g_error_free(gerr_result);
        g_usleep(2 * G_USEC_PER_SEC);
        goto end_umount;
    }

    /* now use the randomizer to find out which titles should be copied */
    randomizer = g_rand_new();

    while (bytes >= 0 && files->len > 1) {
        int          file_number;
        char         *file_name;
        char         buffer[20];
        long         bytes_copied;
        
        file_number = g_rand_int_range(randomizer, 0, files->len);
        file_name = g_ptr_array_index(files, file_number);

        snprintf(buffer, 20, "[%d]", count++);
        update_screen("Copying files ...", buffer, "");

        printf("Copy: %s to %s\n", file_name, s_target_directory);
        bytes_copied = copy_file(file_name, s_target_directory, &gerr_result);
        /*bytes_copied = 3 * 1024 * 1024;//copy_file(file_name, s_target_directory, &gerr_result);+|*/
        if (bytes_copied < 0) {
            update_screen("Error while", "copying file,", "aboring");
            report(RPT_ERR, "%s", gerr_result->message);
            g_error_free(gerr_result);
            g_usleep(2 * G_USEC_PER_SEC);
            goto end_umount;
        }

        bytes -= bytes_copied;

        g_free(file_name);
        g_ptr_array_remove_index_fast(files, file_number);
    }

end_umount:
    /*
     * unmount -------------------------------------------------------------------------------------
     */
    if (s_umount_command && strlen(s_umount_command) > 0) {
        int err;

        err = system(s_umount_command);
        if (err != 0) {
            char buffer[1024];
            update_screen("Unmounting the device", "failed, aborting", "");
            strerror_r(errno, buffer, 1024);
            report(RPT_ERR, "unmount failed: %s",buffer);
            g_usleep(2 * G_USEC_PER_SEC);
            goto end;
        }
    }

    update_screen("You can remove", "the device", "");
    g_usleep(2 * G_USEC_PER_SEC);

    /*
     * Cleanup -------------------------------------------------------------------------------------
     */

end:
    if (files) {
        int i;

        for (i = 0; i < files->len; i++) {
            g_free(g_ptr_array_index(files, i));
        }
        g_ptr_array_free(files, TRUE);
    }

    /* make screen invisible again */
    update_screen("", "", "");
    service_thread_command("screen_set " MODULE_NAME " -priority hidden\n");
}


/* --------------------------------------------------------------------------------------------- */
static void mp3load_menu_handler(const char *event, const char *id, const char *arg)
{
    /*char **ids;*/

    if (strlen(id) == 0) {
        return;
    }


    g_cond_broadcast(s_condition);

#if 0
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
#endif
}

/* --------------------------------------------------------------------------------------------- */
static bool mp3load_init(void)
{
    /* register client */
    service_thread_register_client(&mpd_client);

    /* get config items */
    s_source_directory = key_file_get_string(MODULE_NAME, "source_directory");
    s_target_directory = key_file_get_string(MODULE_NAME, "target_directory");
    s_extensions = key_file_get_string_list_default(MODULE_NAME, "extensions", 
            ".mp3", &s_extension_len);
    s_mount_command = key_file_get_string_default(MODULE_NAME, "mount_command", "");
    s_umount_command = key_file_get_string_default(MODULE_NAME, "umount_command", "");
    s_size = key_file_get_string_default(MODULE_NAME, "size", "90%");

    /* check if necessary config items are available */
    if (!s_source_directory || strlen(s_target_directory) <= 0 || 
            !s_target_directory || strlen(s_source_directory) <= 0) {
        report(RPT_ERR, MODULE_NAME ": `source_directory' and `target_directory' are "
                "necessary configuration variables");
        return false;
    }

    /* add a screen */
    service_thread_command("screen_add " MODULE_NAME "\n");

    /* set the priority of the screen to hidden */
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

    /* add menu item */
    service_thread_command("menu_add_item \"\" mp3load_fill action "
            "\"Fill MP3 player\" -next \"_quit_\"\n");

    /* set the title */
    service_thread_command("widget_set %s title {%s}\n", MODULE_NAME, 
            key_file_get_string_default(MODULE_NAME, "name", "MP3 Load"));

    return true;
}

/* --------------------------------------------------------------------------------------------- */
void *mp3load_run(void *cookie)
{
    gboolean    result;

    result = key_file_has_group(MODULE_NAME);
    if (!result) {
        report(RPT_INFO, "mp3load disabled");
        conf_dec_count();
        goto out_end;
    }

    /* initialise mutex and condition variable */
    s_condition = g_cond_new();
    s_mutex = g_mutex_new();

    if (!mp3load_init()) {
        goto out;
    }
    conf_dec_count();

    /*
     * dispatcher, wait until we can exit and execute the main function if triggered
     * from the menu 
     */
    while (!g_exit) {
        GTimeVal next_timeout;

        g_get_current_time(&next_timeout);
        g_time_val_add(&next_timeout, G_USEC_PER_SEC);

        g_mutex_lock(s_mutex);
        if (g_cond_timed_wait(s_condition, s_mutex, &next_timeout)) {
            /* condition was signalled, i.e. no timeout occurred */
            mp3load_fill_player();
        }
        g_mutex_unlock(s_mutex);

    }

out:
    service_thread_unregister_client(MODULE_NAME);
    CALL_IF_VALID(s_mutex, g_mutex_free);
    CALL_IF_VALID(s_condition, g_cond_free);
    CALL_IF_VALID(s_source_directory, g_free);
    CALL_IF_VALID(s_target_directory, g_free);
    CALL_IF_VALID(s_extensions, g_strfreev);
    CALL_IF_VALID(s_mount_command, g_free);
    CALL_IF_VALID(s_umount_command, g_free);
    CALL_IF_VALID(s_size, g_free);
out_end:
    return NULL;
}

/* vim: set ts=4 sw=4 et: */
