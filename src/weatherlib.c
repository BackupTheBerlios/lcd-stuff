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
#include "weatherlib.h"

#include <glib.h>
#include <nxml.h>

#define WEATHER_URL "http://xoap.weather.com/weather/local/%s?prod=xoap&par=1003832479&key=bb12936706a2d601&cc=*&dayf=0&unit=m"

/* --------------------------------------------------------------------------------------------- */
int retrieve_weather_data(const char *code, struct weather_data *data)
{
     nxml_t *nxml;
     nxml_data_t *element, *cc;
     char *url;
     char *string;
     nxml_error_t err;
     
     url = g_strdup_printf(WEATHER_URL, code);

     nxml_new(&nxml);
     if (nxml_parse_url(nxml, url) != NXML_OK)
     {
         return -1;
     }
     nxml_root_element(nxml, &element);

     if (nxml_find_element(nxml, element, "cc", &cc) == NXML_OK && cc)
     {
         /* temperatur */
         if (nxml_find_element(nxml, cc, "tmp", &element) == NXML_OK && element)
         {
             string = nxmle_get_string(element, &err);
             if (err == NXML_OK)
             {
                 data->temp_c = atoi(string);
                 free(string);
             }
         }

         /* temperature feels like */
         if (nxml_find_element(nxml, cc, "flik", &element) == NXML_OK && element)
         {
             string = nxmle_get_string(element, &err);
             if (err == NXML_OK)
             {
                 data->temp_fl_c = atoi(string);
                 free(string);
             }
         }

         /* humidity */
         if (nxml_find_element(nxml, cc, "hmid", &element) == NXML_OK && element)
         {
             string = nxmle_get_string(element, &err);
             if (err == NXML_OK)
             {
                 data->humid = atoi(string);
                 free(string);
             }
         }

         /* weather */
         if (nxml_find_element(nxml, cc, "t", &element) == NXML_OK && element)
         {
             string = nxmle_get_string(element, &err);
             if (err == NXML_OK)
             {
                 strncpy(data->weather, string, MAX_WEATHER_LEN);
                 data->weather[MAX_WEATHER_LEN-1] = 0;
                 free(string);
             }
         }

         /* pressure */
         if (nxml_find_element(nxml, cc, "bar", &element) == NXML_OK && element &&
                nxml_find_element(nxml, element, "r", &element) == NXML_OK && element)
         {
             string = nxmle_get_string(element, &err);
             if (err == NXML_OK)
             {
                 data->pressure_hPa = atof(string);
                 free(string);
             }
         }

         /* wind speed */
         if (nxml_find_element(nxml, cc, "wind", &element) == NXML_OK && element &&
                nxml_find_element(nxml, element, "s", &element) == NXML_OK && element)
         {
             string = nxmle_get_string(element, &err);
             if (err == NXML_OK)
             {
                 data->wind_speed = atoi(string);
                 free(string);
             }
         }

         /* wind direction */
         if (nxml_find_element(nxml, cc, "wind", &element) == NXML_OK && element &&
                nxml_find_element(nxml, element, "t", &element) == NXML_OK && element)
         {
             string = nxmle_get_string(element, &err);
             if (err == NXML_OK)
             {
                 strncpy(data->wind_dir, string, MAX_WIND_LEN);
                 data->wind_dir[MAX_WIND_LEN-1] = 0;
                 free(string);
             }
         }
     }

     
     
     nxml_free(nxml);
     g_free(url);

     return 0;
}

