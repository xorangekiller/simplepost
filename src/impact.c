/*
 * SimplePost - A Simple HTTP Server
 *
 * Copyright (C) 2012-2016 Karl Lenz.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have recieved a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include "impact.h"

#include <stdio.h>
#include <stdarg.h>

/*!
 * \brief Level of verbosity for log messages
 *
 * Verbosity Level  | Description
 * ---------------- | -----------
 * -1               | Discard all messages instead of printing them.
 *  0               | Print only critical error messages.
 *  1               | Print only initialization and critical messages.
 *  2-INT_MAX       | Print progressively more detailed messages.
 *
 * \note Although the theoretical verbosity level limit is INT_MAX,
 * practically it caps out at the level of the highest impact() statement in
 * the program.
 */
int impact_level = DEFAULT_IMPACT_LEVEL;

/*!
 * \brief Print a message to stderr based on its verbosity level.
 *
 * \note This function determines whether to print a message based on its
 * verbosity level. If the message is printed, it is always printed to the
 * standard error stream, never to the standard output stream. If you *really*
 * need to print a message to stdout, it should probably be printed all the
 * time, regardless of the verbosity level, so use printf() instead.
 *
 * \param[in] level  Verbosity level of the message
 * \param[in] format printf-style format string
 *
 * \retval  -1 An output error occurred.
 * \retval   0 Nothing was printed. Either the format string evaluated to a
 *             zero-length string, or the impact_level was less than the
 *             message level.
 * \retval >=1 The number of characters printed
 */
int impact(int level, const char* format, ...)
{
	if(impact_level < 0 || impact_level < level) return 0;

	int ret;      // printf() return value
	va_list args; // Arguments passed to this function

	va_start(args, format);
	ret = vfprintf(stderr, format, args);
	va_end(args);

	return ret;
}
