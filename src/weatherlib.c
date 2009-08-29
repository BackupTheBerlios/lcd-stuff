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
#include "weatherlib.h"

#include <glib.h>
#include <nxml.h>

#include <shared/report.h>

#define PARTNER_ID  "1135709469"
#define LICENSE_KEY "ad4915c997bebd9c"
#define WEATHER_URL ("http://xoap.weather.com/weather/local/%s?unit=%c&cc=*&par=" PARTNER_ID "&key=" LICENSE_KEY)

/* -------------------------------------------------------------------------- */
int retrieve_weather_data(const char            *code,
                          struct weather_data   *data,
                          enum unit             unit)
{
     nxml_t *nxml;
     nxml_data_t *element, *cc;
     char *url;
     char *string;
     nxml_error_t err;

     if (!data)
         return -EINVAL;

     url = g_strdup_printf(WEATHER_URL, code, unit == UNIT_IMPERIAL ? 'i' : 'm');

     nxml_new(&nxml);
     if (nxml_parse_url(nxml, url) != NXML_OK)
         return -1;
     nxml_root_element(nxml, &element);

     if (nxml_find_element(nxml, element, "cc", &cc) == NXML_OK && cc) {
         /* temperatur */
         if (nxml_find_element(nxml, cc, "tmp", &element) == NXML_OK && element) {
             string = nxmle_get_string(element, &err);
             if (err == NXML_OK) {
                 data->temp_c = atoi(string);
                 free(string);
             }
         }

         /* temperature feels like */
         if (nxml_find_element(nxml, cc, "flik", &element) == NXML_OK && element) {
             string = nxmle_get_string(element, &err);
             if (err == NXML_OK) {
                 data->temp_fl_c = atoi(string);
                 free(string);
             }
         }

         /* humidity */
         if (nxml_find_element(nxml, cc, "hmid", &element) == NXML_OK && element) {
             string = nxmle_get_string(element, &err);
             if (err == NXML_OK) {
                 data->humid = atoi(string);
                 free(string);
             }
         }

         /* weather */
         if (nxml_find_element(nxml, cc, "t", &element) == NXML_OK && element) {
             string = nxmle_get_string(element, &err);
             if (err == NXML_OK) {
                 strncpy(data->weather, string, MAX_WEATHER_LEN);
                 data->weather[MAX_WEATHER_LEN-1] = 0;
                 free(string);
             }
         }

         /* pressure */
         if (nxml_find_element(nxml, cc, "bar", &element) == NXML_OK && element &&
                nxml_find_element(nxml, element, "r", &element) == NXML_OK && element) {
             string = nxmle_get_string(element, &err);
             if (err == NXML_OK) {
                 data->pressure_hPa = atof(string);
                 free(string);
             }
         }

         /* wind speed */
         if (nxml_find_element(nxml, cc, "wind", &element) == NXML_OK && element &&
                nxml_find_element(nxml, element, "s", &element) == NXML_OK && element) {
             string = nxmle_get_string(element, &err);
             if (err == NXML_OK) {
                 data->wind_speed = atoi(string);
                 free(string);
             }
         }

         /* wind direction */
         if (nxml_find_element(nxml, cc, "wind", &element) == NXML_OK && element &&
                nxml_find_element(nxml, element, "t", &element) == NXML_OK && element) {
             string = nxmle_get_string(element, &err);
             if (err == NXML_OK) {
                 strncpy(data->wind_dir, string, MAX_WIND_LEN);
                 data->wind_dir[MAX_WIND_LEN-1] = 0;
                 free(string);
             }
         }
     } else {
         report(RPT_ERR, "URL (%s) does not contain valid weather data", url);
         return -1;
     }

     nxml_free(nxml);
     g_free(url);

     return 0;
}

static char *units[UNIT_MAXLEN][TYPE_MAXLEN] = {
    { "C", "hPa", "km/h", "%" },
    { "F", "in", "mph", "%" }
};

/* -------------------------------------------------------------------------- */
char *get_unit_for_type(enum unit unit, enum weather_type type)
{
    return units[unit][type];
}

/* vim: set sw=4 ts=4 et: */
