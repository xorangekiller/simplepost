/*
 * SimplePost - A Simple HTTP Server
 *
 * Copyright (C) 2012-2015 Karl Lenz.  All rights reserved.
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

#ifndef _IMPACT_H_
#define _IMPACT_H_

#include <stdbool.h>

extern bool impact_quiet;

int impact_printf_standard(const char* format, ...) __attribute__ ((format (printf, 1, 2)));
int impact_printf_error(const char* format, ...) __attribute__ ((format (printf, 1, 2)));

#ifdef DEBUG
int impact_printf_debug(const char* format, ...) __attribute__ ((format (printf, 1, 2)));
#else // !DEBUG
#define impact_printf_debug(...)
#endif // DEBUG

#endif // _IMPACT_H_
