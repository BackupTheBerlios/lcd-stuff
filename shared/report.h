/*
 * report.h
 * This file is part of LCDd, the lcdproc server.
 *
 * This file is released under the GNU General Public License. Refer to the
 * COPYING file distributed with this package.
 *
 * Copyright (c) 1999, William Ferrell, Scott Scriven
 *		 2001, Joris Robijn
 *
 */

#ifndef REPORT_H
#define REPORT_H

/* DEBUGGING / REPORTING
 *
 * To enable the debug() function on all of the software, just type:
 * ./configure --enable-debug
 * and recompile with 'make'
 *
 * To enable the debug() function only in specific files:
 * 1) Configure without enabling debug (that is without --enable-debug)
 * 2) Edit the source file that you want to debug and put the following
 *    line at the top, before the #include "report.h" line:
 *    #define DEBUG
 * 3) Then recompile with 'make'
 * This way, the global DEBUG macro is off but is locally enabled in
 * certains parts of the software.
 *
 * The reporting levels have the following meaning:
 *
 *   0 RPT_CRIT		Critical conditions: the program stops right after
 *			this. Only use this if the program is actually exited
 *			from the current function.
 *   1 RPT_ERR		Error conditions: serious problem, program continues.
 * 			Use this just before you return -1 from a function.
 *   2 RPT_WARNING	Warning conditions: Something that the user should
 *			fix, but the program can continue without a real
 *			problem.
 *			Ex: Protocol errors from a client.
 *   3 RPT_NOTICE	Major event in the program.
 *			Ex: (un)loading of driver, client (dis)connect.
 *   4 RPT_INFO		Minor event in the program: the activation of a
 *			setting, details of a loaded driver, a key
 *			reservation, a keypress, a screen switch.
 *   5 RPT_DEBUG	Insignificant event.
 *			Ex: What function has been called, what subpart of a
 *			function is being executed, what was received and sent
 *			over the socket, etc.
 *
 * Levels 4 (maybe) and 5 (certainly) should be reported using the debug
 * function.
 * The code that this function generates will not be in the executable when
 * compiled without debugging. This way memory and CPU cycles are saved.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdarg.h>
#include <stdio.h>


/* Reporting levels */
#define RPT_CRIT 0
#define RPT_ERR 1
#define RPT_WARNING 2
#define RPT_NOTICE 3
#define RPT_INFO 4
#define RPT_DEBUG 5
/* Don't just modify these numbers, they're related to syslog. */

/* Reporting destinations */
#define RPT_DEST_STDERR 0
#define RPT_DEST_SYSLOG 1
#define RPT_DEST_STORE 2

int set_reporting( char *application_name, int new_level, int new_dest );
/* Sets reporting level and message destination. */

void report( const int level, const char *format, .../*args*/ );
/* Report the message to the selected destination if important enough */

/* Consider the debug function to be exactly the same as the report function.
 * The only difference is that it is only compiled in if DEBUG is defined.
 */

static inline void dont_report( const int level, const char *format, .../*args*/ )
{} /* The idea is that this gets optimized out */

#ifdef DEBUG
#  define debug report
#else
#  define debug dont_report
#endif /*DEBUG*/

#endif
