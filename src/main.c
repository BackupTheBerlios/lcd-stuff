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
#include <pthread.h>
#include <errno.h>

#include <shared/report.h>
#include <shared/configfile.h>

#include "constants.h"
#include "mail.h"

/* ========================= global variables =================================================== */
char            g_lcdproc_server[1024]         = DEFAULT_SERVER;
int             g_lcdproc_port                 = DEFAULT_PORT;
bool            g_exit                         = false;
pthread_mutex_t g_mutex                        = PTHREAD_MUTEX_INITIALIZER;

static char s_config_file[PATH_MAX] = DEFAULT_CONFIG_FILE;
static int s_report_level           = RPT_ERR;
static int s_report_dest            = RPT_DEST_STDERR;
static int s_foreground_mode        = -1;
char g_help_text[] =
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

/* ========================= thread functions =================================================== */
#define THREAD_NUMBER 1
typedef void * (*start_routine)(void *);
static start_routine s_thread_funcs[] = {
    mail_run
};
static pthread_t   s_threads[THREAD_NUMBER];


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
                strncpy(g_lcdproc_server, optarg, 1024);
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
int main(int argc, char *argv[])
{
    int err;
    int i;

	set_reporting(PRG_NAME, RPT_ERR, RPT_DEST_STDERR);

    err = parse_command_line(argc, argv);
    if (err != 0)
    {
        return -1;
    }

	set_reporting(PRG_NAME, s_report_level, s_report_dest);

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

    err = config_read_file(s_config_file);
    switch (err)
    {
        case -1:
            report(RPT_ERR, "Parsing error in configuration file");
            return 1;
        case -2:
            report(RPT_ERR, "File could not be opened");
            return 1;
        case -16:
            report(RPT_ERR, "Memory allocation error");
            return 1;
    }
        
    if (!s_foreground_mode)
    {
        if (daemon(1, 1) != 0)
        {
            report(RPT_ERR, "Error: daemonize failed\n");
        }
    }

    /* start all threads */
    for (i = 0; i < THREAD_NUMBER; i++)
    {
        if (pthread_create(&s_threads[i], NULL, s_thread_funcs[i], NULL) != 0)
        {
            report(RPT_ERR, "Error: Creating thread failed, errno = %d\n", errno);
            return 1;
        }
    }

    /* wait until the threads have been exited */
    for (i = 0; i < THREAD_NUMBER; i++)
    {
        pthread_join(s_threads[i], NULL);
    }

    return 0;
}

/* vim: set ts=4 sw=4 et: */
