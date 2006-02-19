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
#ifndef WEATHERLIB_H
#define WEATHERLIB_H

#define MAX_WEATHER_LEN       1024
#define MAX_WIND_LEN          32
#define MAX_CITYCODE_LEN      10


/**
 * Structure which gets filled by the function following.
 */
struct weather_data
{
    int         temp_c;
    int         temp_fl_c;
    int         humid;
    double      pressure_hPa;
    char        weather[MAX_WEATHER_LEN];
    char        wind_dir[MAX_WIND_LEN];
    int         wind_speed;
};

/**
 * Retrieves the weather data
 *
 * @param code the city code
 * @param data gets filled with the current data
 */
int retrieve_weather_data(const char *code, struct weather_data *data);

#endif /* WEATHERLIB_H */
