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

#ifndef _SIMPLEPOST_H_
#define _SIMPLEPOST_H_

#include <sys/types.h>
#include <stdbool.h>

/// Highest port number
#define SP_HTTP_PORT_MAX     65535

/// Maximum number of pending connections before clients start getting refused
#define SP_HTTP_BACKLOG      16

/// Milliseconds to sleep between shutdown checks while blocking
#define SP_HTTP_SLEEP        100

/// Maximum number of files which may be served simultaneously
#define SP_HTTP_FILES_MAX    50

/*!
 * \brief SimplePost files type
 */
typedef struct simplepost_file
{
	/// Name and path of the file on the filesystem
	char* file;

	/// Uniform Resource Locator assigned to the file
	char* url;

	/// Number of times the file may be downloaded
	unsigned int count;


	/// Next file in the doubly-linked list
	struct simplepost_file* next;

	/// Previous file in the doubly-linked list
	struct simplepost_file* prev;
} * simplepost_file_t;

/*!
 * \brief SimplePost master type
 */
typedef struct simplepost* simplepost_t;

/*!
 * \page using_simplepost Using the SimplePost webserver
 *
 * SimplePost is a lightweight, multi-threaded HTTP server. It is designed to
 * be embedded into other applications. If you have a reasonably modern C
 * compiler and POSIX support, you should have no problem building it. All of
 * the public simplepost_*() methods defined below are completely thread-safe.
 *
 * SimplePost tries to accommodate a reasonable amount of flexibility while
 * remaining fast and easy to use. In the simplest case, all you need to do is
 * initialize a simplepost instance, bind that instance to a port, and add a
 * file to serve. Sample code for this most basic case is below.
 *
 * \code
 * int simple_example()
 * {
 *     simplepost_t spp = simplepost_init();
 *     if(spp == NULL) goto init_error;
 *
 *     unsigned short port = simplepost_bind(spp, NULL, 0);
 *     if(port == 0) goto generic_error;
 *
 *     char* url;
 *     if(simplepost_serve_file(spp, &url, "/usr/bin/simplepost", NULL, 5) == 0) goto generic_error;
 *
 *     while(simplepost_is_alive(spp))
 *     {
 *         printf("Serving: %s\n", url);
 *         sleep(5);
 *     }
 *
 *     simplepost_free(spp);
 *     return 1;
 *
 * init_error:
 *     fprintf(stderr, "Failed to initialize SimplePost\n");
 *     return 0;
 *
 * generic_error:
 *     simplepost_free(spp);
 *     return 0;
 * }
 * \endcode
 */

simplepost_t simplepost_init();
void simplepost_free(simplepost_t spp);

unsigned short simplepost_bind(simplepost_t spp, const char* address, unsigned short port);
bool simplepost_unbind(simplepost_t spp);
void simplepost_block(const simplepost_t spp);
void simplepost_block_files(const simplepost_t spp);
bool simplepost_is_alive(const simplepost_t spp);

size_t simplepost_serve_file(simplepost_t spp, char** url, const char* file, const char* uri, unsigned int count);
short simplepost_purge_file(simplepost_t spp, const char* uri);

simplepost_file_t simplepost_file_init();
void simplepost_file_free(simplepost_file_t sfp);

size_t simplepost_get_address(const simplepost_t spp, char** address);
unsigned short simplepost_get_port(const simplepost_t spp);
size_t simplepost_get_files(simplepost_t spp, simplepost_file_t* files);

#endif // _SIMPLEPOST_H_
