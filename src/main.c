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

#include "rss.h"
#include "constants.h"
#include "mail.h"
#include "weather.h"
#include "servicethread.h"
#include "global.h"
#include "mpd.h" 
#include "keyfile.h"

/* ========================= global variables =================================================== */
char           *g_lcdproc_server       = DEFAULT_SERVER;
int            g_lcdproc_port          = DEFAULT_PORT;
volatile bool  g_exit                  = false;
int            g_socket                = 0;
int            g_display_width         = 0;

/* ========================= thread functions =================================================== */
#define THREAD_NUMBER 4
static GThreadFunc s_thread_funcs[] = {
    rss_run,
    mail_run,
    weather_run,
    mpd_run
};

/* ========================= static variables =================================================== */
static char s_config_file[PATH_MAX] = DEFAULT_CONFIG_FILE;
static int s_report_level           = RPT_ERR;
static int s_report_dest            = RPT_DEST_STDERR;
static int s_foreground_mode        = -1;
static int s_refcount_conf          = THREAD_NUMBER;
static char g_help_text[] =
     "lcd-stuff - Mail, RSS on a display\n"
     "Copyright (c) 2006 Bernhard Walle\n"
     "This file is released under the GNU General Public License. Refer to the\n"
     "COPYING file distributed with this package.\n"
     "Options:\n"
     "  -c <file>\tSpecify configuration file ["DEFAULT_CONFIG_FILE"]\n"
     "  -a <address>\tDNS name or IP address of the LCDd server [localhost]\n"
     "  -p <port>\tport of the LCDd server [13666]\n"
     "  -f <0|1>\tRun in foreground (1) or background (0, default)\n"
     "  -r <level>\tSet reporting level (0-5) [2: errors and warnings]\n"
     "  -s <0|1>\tReport to syslog (1) or stderr (0, default)\n"
     "  -h\t\tShow this help\n";



/* --------------------------------------------------------------------------------------------- */
void sig_handler(int signal)
{
    g_exit = true;
}

/* --------------------------------------------------------------------------------------------- */
int parse_command_line(int argc, char *argv[])
{
    int c;
    int temp_int;
    char *p;
    int error = 0;

    /* No error output from getopt */
	opterr = 0;

	while(( c = getopt( argc, argv, "c:a:p:s:r:f:h" )) > 0) 
    {
        switch (c)
        {
            case 'c':
                strncpy(s_config_file, optarg, PATH_MAX);
                break;
            case 'a':
                g_lcdproc_server = g_strdup(optarg);
                break;
            case 'p':
                temp_int = strtol(optarg, &p, 0 );
                if (*optarg != 0 && *p == 0)
                {
                    g_lcdproc_port = temp_int;
                }
                else 
                {
                    report(RPT_ERR, "Could not interpret value for -%c", c );
                    error = -1;
                }
                break;
            case 'h':
                fprintf(stderr, "%s", g_help_text);
                exit(0);
            case 'r':
                temp_int = strtol(optarg, &p, 0);
                if (*optarg != 0 && *p == 0)
                {
                    s_report_level = temp_int;
                }
                else 
                {
                    report(RPT_ERR, "Could not interpret value for -%c", c);
                    error = -1;
                }
                break;

            case 'f':
                temp_int = strtol(optarg, &p, 0);
                if (*optarg != 0 && *p == 0)
                {
                    s_foreground_mode = temp_int;
                }
                else
                {
                    report(RPT_ERR, "Could not interpret value for -%c", c);
                    error = -1;
                }
                break;


            case 's':
                temp_int = strtol(optarg, &p, 0);
                if (*optarg != 0 && *p == 0)
                {
                    s_report_dest = (temp_int?RPT_DEST_SYSLOG:RPT_DEST_STDERR);
                }
                else 
                {
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

/* --------------------------------------------------------------------------------------------- */
void conf_dec_count(void)
{
    if (g_atomic_int_dec_and_test(&s_refcount_conf))
    {
        key_file_close();
    }
}

/* --------------------------------------------------------------------------------------------- */
static int send_command(char *result, int size, char *command)
{
    char        buffer[BUFSIZ];
    int         err;
    int         num_bytes        = 0;
    
    err = sock_send_string(g_socket, command);
    if (err < 0)
    {
        report(RPT_ERR, "Could not send '%s': %d", buffer, err);
        return err;
    }
    
    if (result)
    {
        while (num_bytes == 0)
        {
            num_bytes = sock_recv_string(g_socket, result, size - 1);
            if (num_bytes < 0)
            {
                report(RPT_ERR, "Could not receive string: %s", strerror(errno));
                return err;
            }
        }
    }

    return num_bytes;
}

/* --------------------------------------------------------------------------------------------- */
static bool communication_init(void)
{
	char     *argv[10];
	int      argc;
    char     buffer[BUFSIZ];

    /* create the connection that will be used in the service thread */
    g_socket = sock_connect(g_lcdproc_server, g_lcdproc_port);
    if (g_socket <= 0)
    {
        report(RPT_ERR, "Could not create socket: %s", strerror(errno));
        return false;
    }
    
    /* handshake */
    send_command(buffer, BUFSIZ, "hello\n");
    
    argc = get_args(argv, buffer, 10);
    if (argc < 8)
    {
        report(RPT_ERR, "rss: Error received: %s", buffer);
        return false;
    }
    g_display_width = min(atoi(argv[7]), MAX_LINE_LEN-1);

    /* client */
    send_command(NULL, 0, "client_set -name " PRG_NAME "\n");

    return true;
}

/* --------------------------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    int     err;
    int     i;
    GThread *threads[THREAD_NUMBER];

	set_reporting(PRG_NAME, RPT_ERR, RPT_DEST_STDERR);

    /* check availability of threads */
    if (!g_thread_supported())
    {
        g_thread_init(NULL);
    }

    /* parse command line */
    err = parse_command_line(argc, argv);
    if (err != 0)
    {
        return -1;
    }
	set_reporting(PRG_NAME, s_report_level, s_report_dest);

    /* register signal handlers */
    if (signal(SIGTERM, sig_handler) == SIG_ERR)
    {
        report(RPT_ERR, "Registering signal handler failed");
        return 1;
    }
    if (signal(SIGINT, sig_handler) == SIG_ERR)
    {
        report(RPT_ERR, "Registering signal handler failed");
        return 1;
    }

    /* read configuration file */
    err = key_file_load_from_file(s_config_file);
    if (!err) 
    {
        report(RPT_ERR, "Loading key file failed");
        return 1;
    }
    
    /* open socket */
    if (!communication_init())
    {
        report(RPT_ERR, "Error: communication init");
        return 1;
    }
        
    /* daemonize */
    if (!s_foreground_mode)
    {
        if (daemon(1, 1) != 0)
        {
            report(RPT_ERR, "Error: daemonize failed");
            return 1;
        }
    }

    service_thread_init();

    /* create the threads */
    for (i = 0; i < THREAD_NUMBER; i++)
    {
        threads[i] = g_thread_create(s_thread_funcs[i], NULL, true, NULL);
        if (!threads[i])
        {
            report(RPT_ERR, "Thread creation (%d) failed", i);
        }
    }

    service_thread_run(NULL);

    for (i = 0; i < THREAD_NUMBER; i++)
    {
        g_thread_join(threads[i]);
    }

    sock_close(g_socket);

    return 0;
}

/* vim: set ts=4 sw=4 et: */
