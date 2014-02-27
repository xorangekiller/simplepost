/*
SimplePost - A Simple HTTP Server

Copyright (C) 2012-2014 Karl Lenz.  All rights reserved.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have recieved a copy of the GNU General Public
License along with this program; if not, write to the
Free Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA 021110-1307, USA.
*/

#ifndef _SIMPLEPIPE_H_
#define _SIMPLEPIPE_H_

#include <sys/types.h>
#include <pthread.h>

/*
Pipe configuration details
*/
#define SP_PIPE_DELAY       1   // Number of seconds to wait between pipe commands
#define SP_PIPE_TIMEOUT     7   // Number of seconds before timing out while waiting for a response

/*
List of SimplePost Pipe instances
*/
typedef struct simplepipe_list
{
    char * pipe_name; // Absolute file name of the pipe
    pid_t pipe_pid; // PID of the SimplePost instance listening on the pipe
    
    struct simplepipe_list * next; // Next pipe in the linked list
    struct simplepipe_list * prev; // Previous pipe in the linked list
} * simplepipe_list_t;

simplepipe_list_t simplepipe_list_init();
void simplepipe_list_free( simplepipe_list_t splp );

/*
SimplePost Pipe for communicating with this instance
*/
typedef struct simplepipe
{
    char * pipe_name; // Absolute file name of the pipe
    int pipe_descriptor; // File descriptor associated with the pipe
    
    pthread_t accept_thread; // Handle of the thread processing pipe commands
    unsigned short accept; // Are we accepting connections over a pipe?
} * simplepipe_t;

simplepipe_t simplepipe_init();
void simplepipe_free( simplepipe_t spp );

size_t simplepipe_get_name( simplepipe_t spp, char ** pipe_name );
size_t simplepipe_enumerate_instances( simplepipe_t spp, simplepipe_list_t * splp );

#endif // _SIMPLEPIPE_H_
