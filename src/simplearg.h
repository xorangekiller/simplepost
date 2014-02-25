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

#ifndef _SIMPLEARG_H_
#define _SIMPLEARG_H_

#include <sys/types.h>

/*
Files to be served by this program
*/
typedef struct simplefile
{
    char * file; // Name and path of the file to be served
    unsigned int count; // Number of times the file may be downloaded
    
    struct simplefile * next; // Next file in the linked list
    struct simplefile * prev; // Previous file in the linked list
} * simplefile_t;

/*
Arguments parsed by this program
*/
typedef struct simplearg
{
    char * address;         // IP address of the HTTP server
    unsigned short port;    // Port the server will be bound to
    pid_t pid;              // PID of the instance of this program to act on
    
    unsigned short new      :1; // Only act on this program instance
    unsigned short quiet    :1; // Don't print anything to stdout
    unsigned short help     :1; // Print our help information
    unsigned short version  :1; // Print our version information
    unsigned short error    :1; // An error occurred; abort!
    unsigned short :11;
    
    simplefile_t files; // List of files to serve
} * simplearg_t;

simplearg_t simplearg_init();
void simplearg_free( simplearg_t sap );
void simplearg_parse( simplearg_t sap, int argc, char * argv[] );

#endif // _SIMPLEARG_H_
