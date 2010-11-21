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
#include <stdlib.h>
#include <stdarg.h>

#include "screen.h"
#include "main.h"
#include "servicethread.h"

/* ------------------------------------------------------------------------- */
static int screen_calculate_lines(struct screen *screen)
{
    int height;
    
    height = screen->lcd->display_size.height;
    if (!screen->lcd->no_title)
        height--;

    return height;
}

/* ------------------------------------------------------------------------- */
void screen_create(struct screen    *screen,
                   struct lcd_stuff *lcd_stuff,
                   const char       *name)
{
    int i, height;

    screen->lcd = lcd_stuff;
    screen->module_name = name;

    /* add a screen */
    service_thread_command(screen->lcd->service_thread, "screen_add %s \n",
                           screen->module_name);

    /* add the title */
    if (!screen->lcd->no_title) {
        service_thread_command(screen->lcd->service_thread,
                               "widget_add %s title title\n",
                               screen->module_name);
    }

    height = screen_calculate_lines(screen);
    for (i = 0; i < height; i++) {
        service_thread_command(screen->lcd->service_thread,
                               "widget_add %s line%d string\n",
                               screen->module_name, i);
    }
}

/* ------------------------------------------------------------------------- */
void screen_set_title(struct screen *screen, const char *title)
{
    if (screen->lcd->no_title)
        return;

    service_thread_command(screen->lcd->service_thread,
                           "widget_set %s title {%s}\n",
                           screen->module_name, title);
}

/* ------------------------------------------------------------------------- */
void screen_set_title_format(struct screen  *screen,
                             const char     *title_format, ...)
{
    va_list ap;
    char *title;

    va_start(ap, title_format);
    title = g_strdup_vprintf(title_format, ap);
    va_end(ap);

    screen_set_title(screen, title);
}

/* ------------------------------------------------------------------------- */
void screen_set_name(struct screen *screen, const char *name)
{
    service_thread_command(screen->lcd->service_thread,
                           "screen_set %s -name %s\n",
                           screen->module_name, name);
}

/* ------------------------------------------------------------------------- */
void screen_show_text(struct screen *screen, int line, const char *text)
{
    int title_line;

    if (line >= screen_calculate_lines(screen))
        return;
    if (!text)
        return;

    title_line = screen->lcd->no_title ? 0 : 1;

    service_thread_command(screen->lcd->service_thread,
                           "widget_set %s line%d 1 %d {%s}\n",
                           screen->module_name, line, line+1+title_line,
                           text);
}

/* ------------------------------------------------------------------------- */
void screen_clear(struct screen *screen)
{
    int height, line;

    height = screen_calculate_lines(screen);
    for (line = 0; line < height; line++)
        screen_show_text(screen, line, "");
}

/* ------------------------------------------------------------------------- */
void screen_destroy(struct screen *screen)
{
    service_thread_command(screen->lcd->service_thread,
                           "screen_del %s\n",
                           screen->module_name);
}

/* vim: set ts=4 sw=4 et: */
