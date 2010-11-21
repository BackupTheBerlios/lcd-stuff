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
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <limits.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <glib.h>

#include <shared/report.h>
#include <shared/str.h>
#include <shared/sockets.h>

#include "constants.h"
#include "keyfile.h"
#include "util.h"
#include "servicethread.h"
#include "global.h"
#include "mplayer.h"
#include "main.h"
#include "config.h"
#if HAVE_LCDSTUFF_MAIL
#  include "mail.h"
#endif
#if HAVE_LCDSTUFF_WEATHER
#  include "weather.h"
#endif
#if HAVE_LCDSTUFF_MPD
#  include "mpd.h"
#endif
#if HAVE_LCDSTUFF_RSS
#  include "rss.h"
#endif

/* ========================= global variables =============================== */
volatile bool  g_exit                  = false;
GQuark         g_lcdstuff_quark;

/* ========================= thread functions =============================== */
static GThreadFunc s_thread_funcs[] = {
    mplayer_run,        /* no library dependencies */
#if HAVE_LCDSTUFF_RSS
    rss_run,
#endif
#if HAVE_LCDSTUFF_MAIL
    mail_run,
#endif
#if HAVE_LCDSTUFF_WEATHER
    weather_run,
#endif
#if HAVE_LCDSTUFF_MPD
    mpd_run,
#endif
};
#define THREAD_NUMBER (sizeof(s_thread_funcs)/sizeof(GThreadFunc))

/* ========================= static variables =============================== */
static char s_config_file[PATH_MAX] = DEFAULT_CONFIG_FILE;
static int s_report_level           = RPT_ERR;
static int s_report_dest            = RPT_DEST_STDERR;
static int s_foreground_mode        = -1;
static int s_refcount_conf          = THREAD_NUMBER + 1; /* + service thread */
static char g_help_text[] =
     "lcd-stuff - Mail, RSS on a display\n"
     "Copyright (c) 2006-2009 Bernhard Walle <bernhard@bwalle.de>\n"
     "\n"
     "This file is released under the GNU General Public License. Refer to the\n"
     "COPYING file distributed with this package.\n"
     "\n"
     "Options:\n"
     "  -c <file>\tSpecify configuration file ["DEFAULT_CONFIG_FILE"]\n"
     "  -a <address>\tDNS name or IP address of the LCDd server [localhost]\n"
     "  -p <port>\tport of the LCDd server [13666]\n"
     "  -f <0|1>\tRun in foreground (1) or background (0, default)\n"
     "  -r <level>\tSet reporting level (0-5) [2: errors and warnings]\n"
     "  -s <0|1>\tReport to syslog (1) or stderr (0, default)\n"
     "  -h\t\tShow this help\n"
     "\n"
     "Compiled-in features:\n  "
#if HAVE_LCDSTUFF_MAIL
     "mail "
#endif
#if HAVE_LCDSTUFF_MPD
     "mpd "
#endif
#if HAVE_LCDSTUFF_WEATHER
     "weather "
#endif
#if HAVE_LCDSTUFF_RSS
     "rss "
#endif
     "\n"
     ;


/* -------------------------------------------------------------------------- */
void sig_handler(int signal)
{
    g_exit = true;
}

/* -------------------------------------------------------------------------- */
int parse_command_line(struct lcd_stuff *lcd, int argc, char *argv[])
{
    int c;
    int temp_int;
    char *p;
    int error = 0;

    /* No error output from getopt */
	opterr = 0;

	while(( c = getopt( argc, argv, "c:a:p:s:r:f:h" )) > 0) {
        switch (c) {
            case 'c':
                strncpy(s_config_file, optarg, PATH_MAX);
                break;
            case 'a':
                strncpy(lcd->lcdproc_server, optarg, _POSIX_HOST_NAME_MAX);
                break;
            case 'p':
                temp_int = strtol(optarg, &p, 0 );
                if (*optarg != 0 && *p == 0) {
                    lcd->lcdproc_port = temp_int;
                } else {
                    report(RPT_ERR, "Could not interpret value for -%c", c );
                    error = -1;
                }
                break;
            case 'h':
                fprintf(stderr, "%s", g_help_text);
                exit(0);
            case 'r':
                temp_int = strtol(optarg, &p, 0);
                if (*optarg != 0 && *p == 0) {
                    s_report_level = temp_int;
                } else {
                    report(RPT_ERR, "Could not interpret value for -%c", c);
                    error = -1;
                }
                break;

            case 'f':
                temp_int = strtol(optarg, &p, 0);
                if (*optarg != 0 && *p == 0) {
                    s_foreground_mode = temp_int;
                } else {
                    report(RPT_ERR, "Could not interpret value for -%c", c);
                    error = -1;
                }
                break;


            case 's':
                temp_int = strtol(optarg, &p, 0);
                if (*optarg != 0 && *p == 0) {
                    s_report_dest = (temp_int?RPT_DEST_SYSLOG:RPT_DEST_STDERR);
                } else {
                    report(RPT_ERR, "Could not interpret value for -%c", c );
                    error = -1;
                }
                break;
            case '?':
                report(RPT_ERR, "Unknown option: %c", optopt);
                error = -1;
                break;
        }
    }
    return error;
}

/* -------------------------------------------------------------------------- */
void conf_dec_count(void)
{
    if (g_atomic_int_dec_and_test(&s_refcount_conf)) {
        key_file_close();
    }
}

/* -------------------------------------------------------------------------- */
static int send_command(struct lcd_stuff *lcd, char *result, int size, char *command)
{
    char        buffer[BUFSIZ];
    int         err;
    int         num_bytes        = 0;

    err = sock_send_string(lcd->socket, command);
    if (err < 0) {
        report(RPT_ERR, "Could not send '%s': %d", buffer, err);
        return err;
    }

    if (result) {
        while (num_bytes == 0) {
            num_bytes = sock_recv_string(lcd->socket, result, size - 1);
            if (num_bytes < 0) {
                report(RPT_ERR, "Could not receive string: %s", strerror(errno));
                return err;
            }
        }
    }

    return num_bytes;
}

/* -------------------------------------------------------------------------- */
static bool communication_init(struct lcd_stuff *lcd)
{
	char     *argv[10];
	int      argc;
    char     buffer[BUFSIZ];

    /* create the connection that will be used in the service thread */
    lcd->socket = sock_connect(lcd->lcdproc_server, lcd->lcdproc_port);
    if (lcd->socket <= 0) {
        report(RPT_ERR, "Could not create socket: %s", strerror(errno));
        return false;
    }

    /* handshake */
    send_command(lcd, buffer, BUFSIZ, "hello\n");

    argc = get_args(argv, buffer, 10);
    if (argc < 8) {
        report(RPT_ERR, "rss: Error received: %s", buffer);
        return false;
    }
    lcd->display_size.width = min(atoi(argv[7]), MAX_LINE_LEN-1);
    lcd->display_size.height = min(atoi(argv[9]), MAX_DISPLAY_HEIGHT);

    /* client */
    send_command(lcd, NULL, 0, "client_set -name " PRG_NAME "\n");

    return true;
}

/* -------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    int     err;
    int     i;
    GThread *threads[THREAD_NUMBER];
    struct lcd_stuff lcd_stuff = {
        .lcdproc_server = DEFAULT_SERVER,
        .lcdproc_port   = DEFAULT_PORT,
        .socket         = 0,
        .display_size   = {0, 0},
        .no_title       = false
    };

    g_lcdstuff_quark = g_quark_from_static_string("lcd-stuff");
	set_reporting(PRG_NAME, RPT_ERR, RPT_DEST_STDERR);
    string_canon_init();

    /* check availability of threads */
    if (!g_thread_supported()) {
        g_thread_init(NULL);
    }

    /* parse command line */
    err = parse_command_line(&lcd_stuff, argc, argv);
    if (err != 0) {
        return -1;
    }
	set_reporting(PRG_NAME, s_report_level, s_report_dest);

    /* register signal handlers */
    if (signal(SIGTERM, sig_handler) == SIG_ERR) {
        report(RPT_ERR, "Registering signal handler failed");
        return 1;
    }
    if (signal(SIGINT, sig_handler) == SIG_ERR) {
        report(RPT_ERR, "Registering signal handler failed");
        return 1;
    }

    /* ignore SIGPIPE */
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        report(RPT_ERR, "Registering signal handler failed");
        return 1;
    }

    /* read configuration file */
    err = key_file_load_from_file(s_config_file);
    if (!err) {
        report(RPT_ERR, "Loading key file failed");
        return 1;
    }

    /* open socket */
    if (!communication_init(&lcd_stuff)) {
        report(RPT_ERR, "Error: communication init");
        return 1;
    }

    /* check if the display is supported */
    if (lcd_stuff.display_size.height != 2 && lcd_stuff.display_size.height != 4) {
        report(RPT_ERR, "Error: Only displays of height 2 or 4 are supported.");
        return 1;
    }

    /* don't waste a line for the title if we have only two of them */
    lcd_stuff.no_title = lcd_stuff.display_size.height == 2;

    /* daemonize */
    if (!s_foreground_mode) {
        if (daemon(1, 1) != 0) {
            report(RPT_ERR, "Error: daemonize failed");
            return 1;
        }
    }

    service_thread_init(&lcd_stuff.service_thread);

    /* create the threads */
    for (i = 0; i < THREAD_NUMBER; i++) {
        threads[i] = g_thread_create(s_thread_funcs[i], &lcd_stuff, true, NULL);
        if (!threads[i]) {
            report(RPT_ERR, "Thread creation (%d) failed", i);
        }
    }

    service_thread_run(&lcd_stuff);

    for (i = 0; i < THREAD_NUMBER; i++) {
        g_thread_join(threads[i]);
    }

    sock_close(lcd_stuff.socket);

    return 0;
}

/* vim: set ts=4 sw=4 et: */
