/*
 * SimplePost - A Simple HTTP Server
 *
 * Copyright (C) 2016 Karl Lenz.  All rights reserved.
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

#ifndef _SIMPLESTR_H_
#define _SIMPLESTR_H_

#include <sys/types.h>

size_t simplestr_count_to_str(char* buf, size_t size, unsigned int count);

size_t simplestr_get_uri(
	char* buf,
	size_t size,
	const char* file,
	const char* uri);

size_t simplestr_get_url(
	char* buf,
	size_t size,
	const char* file,
	const char* address,
	unsigned short port,
	const char* uri);

size_t simplestr_get_serving_str(
	char* buf,
	size_t size,
	const char* file,
	const char* address,
	unsigned short port,
	const char* uri,
	unsigned int count);

#endif // _SIMPLESTR_H_
