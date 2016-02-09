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

#ifndef _SIMPLEARG_H_
#define _SIMPLEARG_H_

#include <sys/types.h>

/*!
 * \brief Files to be served by this program
 */
typedef struct simplefile
{
	/// Name and path of the file to be served
	char* file;

	/// Number of times the file may be downloaded
	unsigned int count;


	/// Next file in the linked list
	struct simplefile* next;

	/// Previous file in the linked list
	struct simplefile* prev;
} * simplefile_t;

/*!
 * \brief Arguments parsed by this program
 */
typedef struct simplearg
{
	/// IP address of the HTTP server
	char* address;

	/// Port the server will be bound to
	unsigned short port;

	/// PID of the instance of this program to act on
	pid_t pid;


	/// Only act on this program instance
	unsigned short new     :1;

	/// Don't print anything to stdout
	unsigned short quiet   :1;

	/// Print our help information
	unsigned short help    :1;

	/// Print our version information
	unsigned short version :1;

	/// An error occurred; abort!
	unsigned short error   :1;

	unsigned short :11;


	/// List of files to serve
	simplefile_t files;
} * simplearg_t;

simplearg_t simplearg_init();
void simplearg_free(simplearg_t sap);
void simplearg_parse(simplearg_t sap, int argc, char* argv[]);

#endif // _SIMPLEARG_H_
