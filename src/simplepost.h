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

#ifndef _SIMPLEPOST_H_
#define _SIMPLEPOST_H_

#include <sys/types.h>

/*
SimplePost HTTP configuration
*/
#define SP_HTTP_PORT_MAX     65535      // Highest port number
#define SP_HTTP_BACKLOG      5          // Maximum number of pending connections before clients start getting refused
#define SP_HTTP_SLEEP        100        // Milliseconds to sleep between shutdown checks while blocking
#define SP_HTTP_FILES_MAX    50         // Maximum number of files which may be served simultaneously
#define SP_HTTP_VERSION     "HTTP/1.0"  // HTTP version implemented by the server

/*
SimplePost error codes
*/
#define SP_ERROR_NONE               0   // No exception; oops!
#define SP_ERROR_INITIALIZED        1   // Server is already initialized
#define SP_ERROR_SOCKET             2   // Socket could not be created
#define SP_ERROR_BIND               3   // Server failed to bind to socket
#define SP_ERROR_ADDRESS            4   // Invalid source address specified
#define SP_ERROR_PORTALLOC          5   // Port could not be allocated
#define SP_ERROR_LISTEN             6   // Cannot listen on socket
#define SP_ERROR_ACCEPT             7   // Cannot accept connections on socket
#define SP_ERROR_FILE_DOESNT_EXIST  8   // Specified file does not exist
#define SP_ERROR_TOO_MANY_FILES     9   // Cannot add more than the maximum file count
#define SP_ERROR_FILE_INSERT_FAILED 10  // Cannot insert file
#define SP_ERROR_RECVREQUEST        11  // Buffer is too small to receive the request
#define SP_ERROR_NO_METHOD          12  // Request does not specify a HTTP method
#define SP_ERROR_INVALID_METHOD     13  // HTTP method of the request is invalid
#define SP_ERROR_NO_URI             14  // Request does not specify a HTTP URI
#define SP_ERROR_INVALID_URI        15  // HTTP URI of the request is invalid
#define SP_ERROR_URI_ALREADY_TAKEN  16  // HTTP URI is already in use
#define SP_ERROR_NO_VERSION         17  // Request does not specify a HTTP version
#define SP_ERROR_RESOURCE_NOT_FOUND 18  // Requested resource is not available
#define SP_ERROR_INVALID_VERSION    19  // HTTP version of the request is not supported
#define SP_ERROR_UNINITIALIZED      20  // Server is not running
#define SP_ERROR_UNIDENTIFIED       21  // Custom exception message was set

#define SP_ERROR_MIN    SP_ERROR_INITIALIZED    // Lowest SimplePost error code
#define SP_ERROR_MAX    SP_ERROR_UNIDENTIFIED   // Highest SimplePost error code

/*
SimplePost files type
*/
typedef struct simplepost_file
{
    char * file;    // Name and path of the file on the filesystem
    char * url;     // Uniform Resource Locator assigned to the file
    
    struct simplepost_file * next;  // Next file in the doubly-linked list
    struct simplepost_file * prev;  // Previous file in the doubly-linked list
} * simplepost_file_t;

/*
SimplePost master type
*/
typedef struct simplepost * simplepost_t;

/*
SimplePost is a lightweight, multi-threaded HTTP server. It is designed to be
embedded into other applications. If you have a reasonably modern C compiler
and POSIX support, you should have no problem building it. All of the public
simplepost_*() methods defined below are completely thread-safe.

SimplePost tries to accommodate a reasonable amount of flexibility while
remaining fast and easy to use. In the simplest case, all you need to do is
initialize a simplepost instance, bind that instance to a port, and add a file
to serve. Sample code for this most basic case is below.

int simple_example()
{
    simplepost_t spp = simplepost_init();
    if( spp == NULL ) goto init_error;
    
    unsigned short port = simplepost_bind( spp, NULL, 0 );
    if( port == 0 ) goto generic_error;
    
    char * url;
    if( simplepost_serve_file( spp, &url, "/usr/bin/simplepost", NULL, 5 ) == 0 ) goto generic_error;
    
    while( simplepost_is_alive( spp ) )
    {
        printf( "Serving: %s\n", url );
        sleep( 5 );
    }
    
    simplepost_free( spp );
    return 1;
    
    init_error:
    fprintf( stderr, "Failed to initialize SimplePost\n" );
    return 0;
    
    generic_error:
    char * error_msg;
    simplepost_get_last_error( ssp, &error_msg );
    fprintf( stderr, "%s\n", error_msg );
    free( error_msg );
    
    simplepost_free( spp );
    return 0;
}
*/

simplepost_t simplepost_init();
void simplepost_free( simplepost_t spp );

unsigned short simplepost_bind( simplepost_t spp, const char * address, unsigned short port );
short simplepost_unbind( simplepost_t spp );
void simplepost_block( simplepost_t spp );
short simplepost_is_alive( simplepost_t spp );

size_t simplepost_serve_file( simplepost_t spp, char ** url, const char * file,  const char * uri, unsigned int count );
short simplepost_purge_file( simplepost_t spp, const char * uri );

simplepost_file_t simplepost_file_init();
void simplepost_file_free( simplepost_file_t sfp );

size_t simplepost_get_address( simplepost_t spp, char ** address );
unsigned short simplepost_get_port( simplepost_t spp );
size_t simplepost_get_files( simplepost_t spp, simplepost_file_t * files );
unsigned int simplepost_get_last_error( simplepost_t spp, char ** error_msg );

#endif // _SIMPLEPOST_H_
