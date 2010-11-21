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

#include <shared/report.h>
#include <shared/sockets.h>
#include <shared/str.h>

#include "weather.h"
#include "main.h"
#include "constants.h"
#include "global.h"
#include "servicethread.h"
#include "weatherlib.h"
#include "keyfile.h"
#include "screen.h"

/* ---------------------- constants ----------------------------------------- */
#define MODULE_NAME           "weather"

/* ---------------------- types --------------------------------------------- */
struct lcd_stuff_weather {
    struct lcd_stuff    *lcd;
    int                 interval;
    char                city[MAX_CITYCODE_LEN];
    enum unit           unit;
    struct screen       screen;
};

/* -------------------------------------------------------------------------- */
static void weather_update(struct lcd_stuff_weather *weather)
{
    char *line1 = NULL, *line2 = NULL, *line3 = NULL;
    struct weather_data data;

    if (retrieve_weather_data(weather->city, &data, weather->unit) == 0) {
        line1 = g_strdup_printf("%s", data.weather);
        if (weather->lcd->display_size.height >= 3) {
            line2 = g_strdup_printf("%d%s (%d%s)  %.1f%s",
                                    data.temp_c, get_unit_for_type(weather->unit, TYPE_TEMPERATURE),
                                    data.temp_fl_c, get_unit_for_type(weather->unit, TYPE_TEMPERATURE),
                                    data.pressure_hPa, get_unit_for_type(weather->unit, TYPE_PRESSURE));
            line3 = g_strdup_printf("%d%s  %d%s %s",
                                    data.humid, get_unit_for_type(weather->unit, TYPE_HUMIDITY),
                                    data.wind_speed, get_unit_for_type(weather->unit, TYPE_WINDSPEED),
                                    data.wind_dir);
        } else {
            line2 = g_strdup_printf("%d%s %d%s  %d%s %s",
                                    data.temp_c, get_unit_for_type(weather->unit, TYPE_TEMPERATURE),
                                    data.humid, get_unit_for_type(weather->unit, TYPE_HUMIDITY),
                                    data.wind_speed, get_unit_for_type(weather->unit, TYPE_WINDSPEED),
                                    data.wind_dir);
        }
        screen_show_text(&weather->screen, 0, line1);
        screen_show_text(&weather->screen, 1, line2);
        screen_show_text(&weather->screen, 2, line3);
        g_free(line1);
        g_free(line2);
        g_free(line3);
    }
}

/* -------------------------------------------------------------------------- */
static struct client weather_client = {
    .name            = MODULE_NAME
};

/* -------------------------------------------------------------------------- */
static bool weather_init(struct lcd_stuff_weather *weather)
{
    char *tmp;

    /* register client */
    service_thread_register_client(weather->lcd->service_thread,
                                   &weather_client, weather);

    /* add a screen */
    screen_create(&weather->screen, weather->lcd, MODULE_NAME);

    tmp = key_file_get_string_default(MODULE_NAME, "name", "Mail");
    screen_set_name(&weather->screen, tmp);
    g_free(tmp);

    /* add the title */
    tmp = key_file_get_string_default_l1(MODULE_NAME, "name", "Weather");
    screen_set_title(&weather->screen, tmp);
    g_free(tmp);

    /* get config items */
    weather->interval = key_file_get_integer_default(MODULE_NAME, "interval", 3600);
    tmp = key_file_get_string_default(MODULE_NAME, "citycode", "");
    strncpy(weather->city, tmp, MAX_CITYCODE_LEN);
    g_free(tmp);
    weather->city[MAX_CITYCODE_LEN-1] = 0;

    /* unit -- metric vs. imperial */
    tmp = key_file_get_string_default(MODULE_NAME, "units", "metric");
    if (strcmp(tmp, "imperial") == 0)
        weather->unit = UNIT_IMPERIAL;
    g_free(tmp);

    return true;
}

/* -------------------------------------------------------------------------- */
void *weather_run(void *cookie)
{
    time_t next_check;
    int result;
    struct lcd_stuff_weather weather;

    /* default values */
    weather.lcd = (struct lcd_stuff *)cookie;
    weather.interval = 0;
    weather.city[0] = '\0';
    weather.unit = UNIT_METRIC;

    result = key_file_has_group(MODULE_NAME);
    if (!result) {
        report(RPT_INFO, "weather disabled");
        conf_dec_count();
        return NULL;
    }

    if (!weather_init(&weather)) {
        return NULL;
    }
    conf_dec_count();

    /* check mails instantly */
    next_check = time(NULL);

    /* dispatcher */
    while (!g_exit) {
        g_usleep(100000);

        /* check emails? */
        if (time(NULL) > next_check) {
            weather_update(&weather);
            next_check = time(NULL) + weather.interval;
        }
    }

    service_thread_unregister_client(weather.lcd->service_thread, MODULE_NAME);
    screen_destroy(&weather.screen);

    return NULL;
}

/* vim: set ts=4 sw=4 et: */
