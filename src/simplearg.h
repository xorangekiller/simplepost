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


/// No options are defined (default)
#define SA_OPT_NONE  0x00

/// Only act on this instance of this program
#define SA_OPT_NEW   0x01

/// Don't print anything to stdout
#define SA_OPT_QUIET 0x02

/// An error occurred. Abort!
#define SA_OPT_ERROR 0x04


/// No actions are defined (default)
#define SA_ACT_NONE       0x00

/// List all accessible instances of this program
#define SA_ACT_LIST_INST  0x01

/// List all files being served by the targeted instance of this program
#define SA_ACT_LIST_FILES 0x02

/// Stop serving all files from the targeted instance of this program
#define SA_ACT_DELETE     0x04

/// Shut down the HTTP server on the targeted instance of this program
#define SA_ACT_SHUTDOWN   0x08

/// Print this program's help information
#define SA_ACT_HELP       0x10

/// Print this program's version information
#define SA_ACT_VERSION    0x20


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


	/// Extra options controlling various aspects of operation
	unsigned int options;

	/// Actions which may be performed by this program
	unsigned int actions;


	/// List of files to serve
	simplefile_t files;
} * simplearg_t;

simplearg_t simplearg_init();
void simplearg_free(simplearg_t sap);
void simplearg_parse(simplearg_t sap, int argc, char* argv[]);

#endif // _SIMPLEARG_H_
