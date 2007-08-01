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
#ifndef WEATHERLIB_H
#define WEATHERLIB_H

#define MAX_WEATHER_LEN       1024
#define MAX_WIND_LEN          32
#define MAX_CITYCODE_LEN      10
#define UNIT_MAX              5


/**
 * Structure which gets filled by the function following.
 */
struct weather_data {
    int         temp_c;
    int         temp_fl_c;
    int         humid;
    double      pressure_hPa;
    char        weather[MAX_WEATHER_LEN];
    char        wind_dir[MAX_WIND_LEN];
    int         wind_speed;
};

/**
 * Enumeration type for the unit.
 */
enum unit {
    UNIT_METRIC,
    UNIT_IMPERIAL,
    UNIT_MAXLEN
};

/**
 * Type of weather data.
 */
enum type {
    TYPE_TEMPERATURE,
    TYPE_PRESSURE,
    TYPE_WINDSPEED,
    TYPE_HUMIDITY,
    TYPE_MAXLEN
};

/**
 * Retrieves the weather data
 *
 * @param code the city code
 * @param data gets filled with the current data
 */
int retrieve_weather_data(const char            *code,
                          struct weather_data   *data, 
                          enum unit             unit);

/**
 * Returns a static (!) char pointer to the unit for a specific type.
 */ 
char *get_unit_for_type(enum unit unit, enum type type);

#endif /* WEATHERLIB_H */

/* vim: set ts=4 sw=4 et: */
