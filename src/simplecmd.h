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

#ifndef _SIMPLECMD_H_
#define _SIMPLECMD_H_

#include "simplepost.h"

/*****************************************************************************
 *                            SimpleCommand List                             *
 *****************************************************************************/

/*!
 * \brief SimplePost command instances type
 */
typedef struct simplecmd_list
{
	/// Absolute file name of the local socket
	char* sock_name;

	/// PID of the SimplePost instance listening on the socket
	pid_t inst_pid;


	/// Next instance in the linked list
	struct simplecmd_list* next;

	/// Previous instance in the linked list
	struct simplecmd_list* prev;
} * simplecmd_list_t;

simplecmd_list_t simplecmd_list_init();
void simplecmd_list_free(simplecmd_list_t sclp);

size_t simplecmd_list_inst(simplecmd_list_t* sclp);
pid_t simplecmd_find_inst(const char* address, unsigned short port, pid_t pid);

/*****************************************************************************
 *                           SimpleCommand Server                            *
 *****************************************************************************/

/*!
 * \brief SimplePost command server type
 */
typedef struct simplecmd* simplecmd_t;

simplecmd_t simplecmd_init();
void simplecmd_free(simplecmd_t scp);

short simplecmd_activate(simplecmd_t scp, simplepost_t spp);
short simplecmd_deactivate(simplecmd_t scp);
short simplecmd_is_alive(simplecmd_t scp);

/*****************************************************************************
 *                           SimpleCommand Client                            *
 *****************************************************************************/

size_t simplecmd_get_address(pid_t server_pid, char** address);
unsigned short simplecmd_get_port(pid_t server_pid);
size_t simplecmd_get_version(pid_t server_pid, char** version);
short simplecmd_set_file(pid_t server_pid, const char* file, unsigned int count);

#endif // _SIMPLECMD_H_
