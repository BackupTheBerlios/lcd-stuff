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

#include <shared/report.h>
#include <shared/configfile.h>
#include <shared/sockets.h>
#include <shared/str.h>
#include <shared/LL.h>

#include <libetpan/libetpan.h>

#include "weather.h"
#include "main.h"
#include "constants.h"
#include "global.h"
#include "helpfunctions.h"
#include "servicethread.h"
#include "weatherlib.h" 

/* ---------------------- constants ------------------------------------------------------------ */
#define MODULE_NAME           "weather"

/* ------------------------variables ----------------------------------------------------------- */
static int          s_interval;
static char         s_city[MAX_CITYCODE_LEN];
struct client       weather_client = 
                    {
                        .name            = MODULE_NAME,
                        .key_callback    = NULL,
                        .listen_callback = NULL,
                        .ignore_callback = NULL
                    }; 

/* --------------------------------------------------------------------------------------------- */
static void update_screen(const char *line1, const char *line2, const char *line3)
{
    if (line1)
    {
        service_thread_command("widget_set %s line1 1 2 {%s}\n", MODULE_NAME, line1);
    }

    if (line2)
    {
        service_thread_command("widget_set %s line2 1 3 {%s}\n", MODULE_NAME, line2);
    }

    if (line3)
    {
        service_thread_command("widget_set %s line3 1 4 {%s}\n", MODULE_NAME, line3);
    }
}

/* --------------------------------------------------------------------------------------------- */
static void weather_update(void)
{
    char *line1, *line2, *line3;
    struct weather_data data;

    if (retrieve_weather_data(s_city, &data) == 0)
    {
        line1 = g_strdup_printf("%s", data.weather);
        line2 = g_strdup_printf("%d (%d) °C  %d hPa", 
                                data.temp_c, data.temp_fl_c, (int)(data.pressure_hPa + 0.5));
        line3 = g_strdup_printf("%d %%  %d km/h %s", 
                                data.humid, data.wind_speed, data.wind_dir);
        update_screen(line1, line2, line3);
        g_free(line1);
        g_free(line2);
        g_free(line3);
    }
}

/* --------------------------------------------------------------------------------------------- */
static bool weather_init(void)
{
    /* register client */
    service_thread_register_client(&weather_client);

    /* add a screen */
    service_thread_command("screen_add " MODULE_NAME "\n");
    service_thread_command("screen_set %s -name %s\n", MODULE_NAME,
                          config_get_string(MODULE_NAME, "name", 0, "Mail"));

    /* add the title */
    service_thread_command("widget_add " MODULE_NAME " title title\n");
    service_thread_command("widget_set %s title %s\n", 
                          MODULE_NAME, config_get_string(MODULE_NAME, "name", 0, "Mail"));

    /* add three lines */
    service_thread_command("widget_add " MODULE_NAME " line1 string\n");
    service_thread_command("widget_add " MODULE_NAME " line2 string\n");
    service_thread_command("widget_add " MODULE_NAME " line3 string\n");

    /* get config items */
    s_interval = config_get_int(MODULE_NAME, "interval", 0, 300);
    strncpy(s_city, config_get_string(MODULE_NAME, "citycode", 0, ""), MAX_CITYCODE_LEN);
    s_city[MAX_CITYCODE_LEN-1] = 0;

    return true;
}

/* --------------------------------------------------------------------------------------------- */
void *weather_run(void *cookie)
{
    time_t  next_check;
    int     result;

    result = config_has_section(MODULE_NAME);
    if (!result)
    {
        report(RPT_INFO, "mail disabled");
        return NULL;
    }

    if (!weather_init())
    {
        return NULL;
    }

    /* check mails instantly */
    next_check = time(NULL);

    /* dispatcher */
    while (!g_exit)
    {
        g_usleep(100000);

        /* check emails? */
        if (time(NULL) > next_check)
        {
            weather_update();
            next_check = time(NULL) + s_interval;
        }
    }

    service_thread_unregister_client(MODULE_NAME);

    return NULL;
}

/* vim: set ts=4 sw=4 et: */