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

#include "simplestr.h"

#include <string.h>
#include <stdio.h>

/*!
 * \brief Convert the COUNT to a string.
 *
 * \param[out] buf  Buffer to receive the COUNT string
 * \param[in] size  Size (in bytes) of buf
 * \param[in] count COUNT to convert
 *
 * \return the number of characters written to the buffer, excluding the NULL-
 * terminating character. If there was an error, zero will be returned instead.
 */
size_t simplestr_count_to_str(char* buf, size_t size, unsigned int count)
{
	if(buf == NULL) return 0;

	switch(count)
	{
		case 0:
			if(size <= 12) return 0;
			strcpy(buf, "indefinitely");
			return 12;

		case 1:
			if(size <= 12) return 0;
			strcpy(buf, "exactly once");
			return 12;

		default:
			{
				int ret = snprintf(buf, (int) size, "%u times", count);
				if(ret <= 0 || ((size_t) ret) >= size) return 0;
				return (size_t) ret;
			}
	}

	return 0;
}

/*!
 * \brief Construct a URI from the given FILE or URI.
 *
 * \param[out] buf Buffer to receive the URI string
 * \param[in] size Size (in bytes) of buf
 * \param[in] file Optional FILE to convert
 * \param[in] uri  Optional URI to convert
 *
 * \note Only the file or uri parameters need be given, but not both. If they
 * are both NULL, this function will return zero. If they are both given, the
 * URI will be preferred.
 *
 * \return the number of characters written to the buffer, excluding the NULL-
 * terminating character. If there was an error, zero will be returned instead.
 */
size_t simplestr_get_uri(
	char* buf,
	size_t size,
	const char* file,
	const char* uri)
{
	if(buf == NULL) return 0;

	if(uri)
	{
		size_t len = strlen(uri);
		if(uri[0] != '/' || size <= len) return 0;

		strcpy(buf, uri);
		return len;
	}
	else if(file)
	{
		for(const char* s = file; *s != '\0'; ++s)
		{
			if(*s == '/') file = (s + 1);
		}

		size_t len = strlen(file) + 1;
		if(len == 0 || size <= len) return 0;

		buf[0] = '/';
		strcpy(buf + 1, file);
		return len;
	}

	return 0;
}

/*!
 * \brief Construct a URL from the given FILE, ADDRESS, PORT, and URI.
 *
 * \param[out] buf    Buffer to receive the URI string
 * \param[in] size    Size (in bytes) of buf
 * \param[in] file    Optional FILE to convert
 * \param[in] address ADDRESS to convert
 * \param[in] port    PORT to convert
 * \param[in] uri     Optional URI to convert
 *
 * \note Only the file or uri parameters need be given, but not both. If they
 * are both NULL, this function will return zero. If they are both given, the
 * URI will be preferred.
 *
 * \return the number of characters written to the buffer, excluding the NULL-
 * terminating character. If there was an error, zero will be returned instead.
 */
size_t simplestr_get_url(
	char* buf,
	size_t size,
	const char* file,
	const char* address,
	unsigned short port,
	const char* uri)
{
	if(buf == NULL || address == NULL || port == 0) return 0;

	int snp_ret;    // snprintf() return code
	size_t uri_ret; // simplestr_get_uri() return code
	size_t len = 0; // Length of the string written to the buffer

	if(port == 80)
	{
		snp_ret = snprintf(buf, (int) size, "http://%s", address);
		if(snp_ret <= 0 || ((size_t) snp_ret) >= size) return 0;
		len += (size_t) snp_ret;
	}
	else
	{
		snp_ret = snprintf(buf, (int) size, "http://%s:%hu", address, port);
		if(snp_ret <= 0 || ((size_t) snp_ret) >= size) return 0;
		len += (size_t) snp_ret;
	}

	uri_ret = simplestr_get_uri(buf + len, size - len, file, uri);
	if(uri_ret == 0) return 0;
	len += uri_ret;

	return len;
}

/*!
 * \brief Construct a pretty string from the given FILE, ADDRESS, PORT, and
 * URI which may be printed to the console.
 *
 * This function creates a nicely formatted string to print the given
 * parameters to the console. This string is not really useful for parsing or
 * doing anything automated, just nice output. It will be in the format of,
 * "Serving FILE on URL COUNT times".
 *
 * \param[out] buf    Buffer to receive the URI string
 * \param[in] size    Size (in bytes) of buf
 * \param[in] file    FILE to convert
 * \param[in] address ADDRESS to convert
 * \param[in] port    PORT to convert
 * \param[in] uri     Optional URI to convert
 * \param[in] count   COUNT to convert
 *
 * \return the number of characters written to the buffer, excluding the NULL-
 * terminating character. If there was an error, zero will be returned instead.
 */
size_t simplestr_get_serving_str(
	char* buf,
	size_t size,
	const char* file,
	const char* address,
	unsigned short port,
	const char* uri,
	unsigned int count)
{
	if(buf == NULL || file == NULL || address == NULL || port == 0) return 0;

	size_t ret;     // simplestr_get_url() or simplestr_count_to_str() return code
	size_t len = 0; // Length of the string written to the buffer

	len += 8;
	if(size <= len) return 0;
	strcpy(buf, "Serving ");

	len += strlen(file);
	if(size <= len) return 0;
	strcat(buf, file);

	len += 4;
	if(size <= len) return 0;
	strcat(buf, " on ");

	ret = simplestr_get_url(buf + len, size - len, file, address, port, uri);
	if(ret == 0) return 0;
	len += ret;

	++len;
	if(size <= len) return 0;
	strcat(buf, " ");

	ret = simplestr_count_to_str(buf + len, size - len, count);
	if(ret == 0) return 0;
	len += ret;

	return len;
}
