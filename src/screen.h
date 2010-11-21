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
#ifndef SCREEN_H
#define SCREEN_H

#include <stdbool.h>

/**
 * @file screen.h
 * @brief Functions for a default screen that consists of text lines only.
 */

/**
 * @brief Screen structure
 *
 * This struture should normally be embedded in the client structure (e.g.
 * lcd_stuff_rss) of the module. Therefore it's necessary to know the size of
 * the structure in advance (which means that it has to be declared in the
 * header).  However, one should not access structure members from outside.
 */
struct screen {
    struct lcd_stuff    *lcd;
    const char          *module_name;
};

/**
 * @brief Creates a new screen.
 *
 * This function initializes all members in @p screen and also creates the
 * necessary components in the display by communicating with lcdproc.
 *
 * @param[in] screen the screen structure that must be passed to all functions
 *            in the file and which represents the opaque object. The members
 *            of @p screen doesn't have to be initialized, but the memory must
 *            be allocated.
 * @param[in] lcd the lcd_stuff object
 * @param[in] name the name of the screen
 */
void screen_create(struct screen    *screen,
                   struct lcd_stuff *lcd,
                   const char       *name);

/**
 * @brief Sets the specified title on the current screen.
 *
 * If the global setting no_title has been set, this function does nothing.
 *
 * @param[in] screen the screen object
 * @param[in] title the title that should be shown on top of the screen.
 *            If @p title is NULL then the function does nothing.

 * @see screen_set_title_format()
 */
void screen_set_title(struct screen *screen, const char *title);

/**
  * @brief Sets the specified title on the current screen
  *
  * This function does the same as screen_set_title() but takes a printf()-style
  * format string and executes g_strdup_printf() on that format string and its
  * arguments.
  *
  * @param[in] screen the screen object
  * @param[in] title_format the printf()-style format specifier
  *            If @p title_format is NULL then the function does nothing.
  */
void screen_set_title_format(struct screen *screen, const char *title_format, ...);

/**
  * @brief Sets the name of a screen
  *
  * @param[in] screen the screen object
  * @param[in] name the name of the screen that should be set
  */
void screen_set_name(struct screen *screen, const char *name);

/**
 * @brief Shows the specified text on the screen
 *
 * @param[in] screen the screen object
 * @param[in] line the number of the line starting from 0. If the display
 *            is too small it is legal to call this function but it doesn't
 *            show any text. However, this doesn't result in a return value
 *            0 @c false. Only real errors result in a return value of @c
 *            false.
 * @param[in] text the text that should be shown. If @p text is NULL then
 *            the function does nothing.
 */
void screen_show_text(struct screen *screen, int line, const char *text);

/**
 * @brief Deletes the text on any line except the title
 *
 * @param[in] screen the screen object
 */
void screen_clear(struct screen *screen);

/**
 * @brief Destroys a screen
 *
 * Destroys the screen that has been created with screen_create(). This
 * function doesn't free any memory. However, it deletes the screen by
 * communicating with lcdproc.
 *
 * @param[in] screen the screen to free.
 */
void screen_destroy(struct screen *screen);

#endif /* SCREEN_H */

/* vim: set ts=4 sw=4 et: */
