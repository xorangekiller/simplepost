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

#include "simplepipe.h"
#include "impact.h"
#include "config.h"

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <regex.h>

/*****************************************************************************
 *                               Pipe Support                                *
 *****************************************************************************/

/*
Send data to the pipe.

Remarks:
    This function doesn't fail by return value. It's possible that this will
    cause it to hang, but that is a problem for the caller. If the pipe is not
    opened for reading on the other end SIGPIPE will be sent to this program.
    http://man7.org/linux/man-pages/man7/fifo.7.html

Arguments:
    descriptor [in]     File descriptor of the named pipe
    command [in]        Command to send to over the pipe
    data [in]           Data to send to the receiver
                        The terminating character WILL NOT be sent!
*/
static void __pipe_send( int descriptor, const char * command, const char * data )
{
    char buffer[30]; // Number of characters to be written
    
    if( command )
    {
        sprintf( buffer, "%lu", strlen( command ) );
        write( descriptor, buffer, strlen( buffer ) + 1 );
        sleep( SP_PIPE_DELAY );
        
        write( descriptor, command, strlen( command ) );
        sleep( SP_PIPE_DELAY );
    }
    
    if( data )
    {
        sprintf( buffer, "%lu", strlen( data ) );
        write( descriptor, buffer, strlen( buffer ) + 1 );
        sleep( SP_PIPE_DELAY );
        
        // According to POSIX.1-2001, the results of attempting to write() a
        // zero-length string to anything other than a regular file are undefined.
        // Since the pipe command set handled by this program is well-defined, we don't
        // need to worry about the contingency of sending a zero-length command above;
        // if it occurs, it is clearly a bug. On the other hand, the data accompanying
        // each command could be almost anything. To avoid calling the equivalent of
        // write( descriptor, "", 0 ); we will write() the length "0" (above), but no
        // accompanying data.
        if( !(buffer[0] == '0' && buffer[1] == '\0') )
        {
            write( descriptor, data, strlen( data ) );
            sleep( SP_PIPE_DELAY );
        }
    }
}

/*
Send a command to the specified pipe and read the response.

Arguments:
    descriptor [in] File descriptor of the pipe
    command [in]    Command to send over the pipe
                    If this parameter is NULL, no command will be sent.
    data [out]      NULL-terminated string read from the pipe
                    If *data != NULL, you are responsible for freeing it.

Return Value:
    The number of bytes written to the read buffer will be returned. If an
    error occurred (or the operation timed out), zero will be returned instead.
*/
static size_t __pipe_recv( int descriptor, const char * command, char ** data )
{
    char buffer[30]; // Number of characters to be read from the buffer
    size_t length; // Number of characters received 
    char b; // Last byte read from the pipe
    *data = NULL; // Failsafe
    
    if( command ) __pipe_send( descriptor, command, NULL );
    
    for( size_t i = 0; read( descriptor, (void *) &b, 1 ) == 1; i++ )
    {
        if( i == sizeof( buffer ) )
        {
            impact_printf_error( "%s: Pipe protocol error: invalid string size\n", SP_MAIN_DESCRIPTION );
            return 0;
        }
        
        buffer[i] = b;
        if( b == '\0' ) break;
    }
    
    if( sscanf( buffer, "%lu", &length ) == EOF )
    {
        impact_printf_error( "%s: Pipe protocol error: %s is not a number\n", SP_MAIN_DESCRIPTION, buffer );
        return 0;
    }
    
    *data = (char *) malloc( sizeof( char ) * (length + 1) );
    if( *data == NULL )
    {
        if( command ) impact_printf_error( "%s: Read buffer required for %s\n", SP_MAIN_DESCRIPTION, command );
        else impact_printf_error( "%s: Read buffer required\n", SP_MAIN_DESCRIPTION );
        return 0;
    }
    
    // The data received may be a zero-length string. The following loop handles
    // this condition implicitly, but it is not to be overlooked. Since no
    // terminating character is sent for data (unlike for its length), a
    // zero-length data string effectively means that we should not attempt to
    // read anything from the pipe.
    for( size_t i = 0; i < length; i++ )
    {
        if( read( descriptor, (void *) &b, 1 ) == 1 )
        {
            *data[i] = b;
        }
        else
        {
            impact_printf_error( "%s: Read terminated before we received %ul bytes\n", SP_MAIN_DESCRIPTION, length );
            free( *data );
            *data = NULL;
            return 0;
        }
    }
    *data[length] = '\0';
    
    return length;
}

/*****************************************************************************
 *                             SimplePipe List                               *
 *****************************************************************************/

/*
Initialize a SimplePost Pipe List instance.

Return Value:
    An initialized SimplePost Pipe List instance will be returned unless an
    error occurred, in which case NULL will be returned instead.
*/
simplepipe_list_t simplepipe_list_init()
{
    simplepipe_list_t splp = (simplepipe_list_t) malloc( sizeof( struct simplepipe_list ) );
    if( splp == NULL ) return NULL;
    
    splp->pipe_name = NULL;
    splp->pipe_pid = 0;
    
    splp->next = NULL;
    splp->prev = NULL;
    
    return splp;
}

/*
Free the given SimplePost Pipe List instance.

Arguments:
    splp [in]   Instance to act on
*/
void simplepipe_list_free( simplepipe_list_t splp )
{
    while( splp )
    {
        simplepipe_list_t p = splp;
        splp = splp->next;
        if( p->pipe_name ) free( p->pipe_name );
        free( p );
    }
}

/*****************************************************************************
 *                       SimplePipe Command Handlers                         *
 *****************************************************************************/

/*
Pipe command type
*/
struct pipecommand
{
    const char * request; // Request sent by the client
    short (*handler) (simplepipe_t); // Function to process the command
};

/*
Prototypes of pipe command handlers
*/
static short __command_send_address( simplepipe_t spp );
static short __command_send_port( simplepipe_t spp );
static short __command_send_pid( simplepipe_t spp );
static short __command_send_version( simplepipe_t spp );
static short __command_recv_file( simplepipe_t spp );

/*
Pipe commands to handle
*/
struct pipecommand pipe_commands[] =
{
    {"GetAddress", &__command_send_address},
    {"GetPort", &__command_send_port},
    {"GetPID", &__command_send_pid},
    {"GetVersion", &__command_send_version},
    {"SetFile", &__command_recv_file},
    {NULL, NULL}
};

/*
Send the primary address our web server is bound to to the client.

Remarks:
    If the web server is not running, a zero-length string will be sent. See
    the related comments in __pipe_send() and __pipe_recv() to get a better
    understanding of how this contingency is handled.

Arguments:
    spp [in]    Instance to act on

Return Value:
    0   Failed to respond to the request.
    1   The requested information was sent successfully.
*/
static short __command_send_address( simplepipe_t spp )
{
    return 0;
}

/*
Send the port our web server is listening on to the client.

Remarks:
    If the web server is not running, zero will be sent as the port number.

Arguments:
    spp [in]    Instance to act on

Return Value:
    0   Failed to respond to the request.
    1   The requested information was sent successfully.
*/
static short __command_send_port( simplepipe_t spp )
{
    return 0;
}

/*
Send our process identifier to the client.

Arguments:
    spp [in]    Instance to act on

Return Value:
    0   Failed to respond to the request.
    1   The requested information was sent successfully.
*/
static short __command_send_pid( simplepipe_t spp )
{
    char buffer[30]; // Our stringified PID
    
    sprintf( buffer, "%d", getpid() );
    __pipe_send( spp->pipe_descriptor, NULL, buffer );
    
    return 1;
}

/*
Send the current program version to the client.

Arguments:
    spp [in]    Instance to act on

Return Value:
    0   Failed to respond to the request.
    1   The requested information was sent successfully.
*/
static short __command_send_version( simplepipe_t spp )
{
    __pipe_send( spp->pipe_descriptor, NULL, SP_MAIN_VERSION );
    return 1;
}

/*
Receive a file and count from the client and add it our web server.

Arguments:
    spp [in]    Instance to act on

Return Value:
    0   Failed to respond to the request.
    1   The requested information was sent successfully.
*/
static short __command_recv_file( simplepipe_t spp )
{
    return 0;
}

/*****************************************************************************
 *                            SimplePipe Private                             *
 *****************************************************************************/

/*
Start accepting commands from another instance of this program.

Remarks:
    A new named pipe will be created using a predictable naming scheme.

Arguments:
    p [in]      SimplePost Pipe instance to act on

Return Value:
    NULL will always be returned.
*/
static void * __accept( void * p )
{
    simplepipe_t spp = (simplepipe_t) p; // Instance to act on
    
    char * buffer = NULL; // Data read from the pipe
    struct stat file_status; // Pipe status
    
    if( spp->pipe_name == NULL )
    {
        char * buffer = (char *) malloc( sizeof( char ) * 512 );
        if( buffer )
        {
            sprintf( buffer, "/tmp/simplepost_pipe_%d", getpid() );
            spp->pipe_name = (char *) malloc( sizeof( char ) * (strlen( buffer ) + 1) );
        }
        if( spp->pipe_name == NULL )
        {
            impact_printf_error( "%s: %s cannot be buffered\n", SP_MAIN_DESCRIPTION, buffer );
            free( buffer );
            return NULL;
        }
        strcpy( spp->pipe_name, buffer );
        free( buffer );
        buffer = NULL;
    }
    
    if( stat( spp->pipe_name, &file_status ) == 0 )
    {
        impact_printf_error( "%s: Pipe %s already exists\n", SP_MAIN_DESCRIPTION, spp->pipe_name );
        return NULL;
    }
    
    if( mkfifo( buffer, O_RDWR | O_CREAT | S_IRWXU | S_IRWXG | S_IRWXO ) != 0 )
    {
        impact_printf_error( "%s: Failed to create pipe %s\n", SP_MAIN_DESCRIPTION, spp->pipe_name );
        return NULL;
    }
    
    spp->pipe_descriptor = open( spp->pipe_name, O_RDWR | O_APPEND );
    if( spp->pipe_descriptor == -1 )
    {
        impact_printf_error( "%s: Failed to open pipe %s\n", SP_MAIN_DESCRIPTION, spp->pipe_name );
        remove( spp->pipe_name );
        return NULL;
    }
    
    spp->accept = 1;
    while( spp->accept )
    {
        if( __pipe_recv( spp->pipe_descriptor, NULL, &buffer ) > 0 )
        {
            size_t i;
            for( i = 0; pipe_commands[i].request; i++ )
            {
                if( strcmp( pipe_commands[i].request, buffer ) == 0 )
                {
                    impact_printf_debug( "%s: Thread 0x%lx responding to %s ...\n", SP_MAIN_DESCRIPTION, pthread_self(), pipe_commands[i].request );
                    if( (*(pipe_commands[i].handler))( spp ) == 0 ) impact_printf_error( "%s: Command failed: %s\n", SP_MAIN_DESCRIPTION, pipe_commands[i].request );
                    break;
                }
            }
            
            if( pipe_commands[i].request == NULL )
            {
                impact_printf_error( "%s: Invalid command: %s\n", SP_MAIN_DESCRIPTION, buffer );
            }
        }
        
        if( buffer ) free( buffer );
    }
    
    impact_printf_debug( "%s: Exiting pipe handler thread 0x%lx ...\n", SP_MAIN_DESCRIPTION, pthread_self() );
    
    close( spp->pipe_descriptor );
    spp->pipe_descriptor = -1;
    sleep( SP_PIPE_DELAY );
    remove( spp->pipe_name );
    
    if( buffer ) free( buffer );
    spp->accept_thread = -1;
    
    return NULL;
}

/*
Terminate the pipe thread if it is running.

Arguments:
    spp [in]    Instance to act on
*/
static void __kill_pipe( simplepipe_t spp )
{
    if( spp->accept_thread == -1 ) return;
    spp->accept = 0;
    pthread_join( spp->accept_thread, NULL );
}

/*****************************************************************************
 *                            SimplePipe Public                              *
 *****************************************************************************/

/*
Initialize a SimplePost Pipe instance.

Remarks:
    The pipe will be initialized and accepting connections if this function
    succeeds (contrary to most *_init() functions, which merely initialize a
    data structure).

Return Value:
    An initialized SimplePost Pipe instance will be returned unless an error
    occurred, in which case NULL will be returned instead.
*/
simplepipe_t simplepipe_init()
{
    simplepipe_t spp = (simplepipe_t) malloc( sizeof( struct simplepipe ) );
    if( spp == NULL ) return NULL;
    
    spp->pipe_name = NULL;
    spp->pipe_descriptor = -1;
    
    if( pthread_create( &spp->accept_thread, NULL, &__accept, (void *) spp ) != 0 )
    {
        if( spp->pipe_name ) free( spp->pipe_name );
        free( spp );
        return NULL;
    }
    
    return spp;
}

/*
Free the given SimplePost Pipe instance.

Arguments:
    spp [in]    Instance to act on
*/
void simplepipe_free( simplepipe_t spp )
{
    __kill_pipe( spp );
    if( spp->pipe_name ) free( spp->pipe_name );
    free( spp );
}

/*
Get the name of our pipe.

Arguments:
    spp [in]        Instance to act on
    pipe_name [out] Name of our pipe
                    If *pipe_name != NULL, you are responsible for freeing it.

Return Value:
    The number of characters written to pipe_name (excluding the NULL-
    terminating character) will be returned. If this number is zero, a memory
    allocation error occurred, or, more likely, no pipe has been initialized.
*/
size_t simplepipe_get_name( simplepipe_t spp, char ** pipe_name )
{
    size_t length = 0; // Number of characters written to pipe_name
    *pipe_name = NULL; // Failsafe
    
    if( spp->pipe_name == NULL ) return 0;
    
    length = strlen( spp->pipe_name );
    *pipe_name = (char *) malloc( sizeof( char ) * (length + 1) );
    if( *pipe_name == NULL ) return 0;
    
    strcpy( *pipe_name, spp->pipe_name );
    
    return length;
}

/*
Get a list of all SimplePost instances on the system with open pipes, excluding
this one.

Arguments:
    spp [in]    Instance to act on
    splp [out]  List of all open SimplePost pipes (excluding ours) on the system
                If *splp != NULL, you are responsible for freeing it.

Return Value:
    The number instances in the list will be returned. If zero is returned,
    either a memory allocation error occurred, or, more likely, there are no
    other instances of SimplePost currently active.
*/
size_t simplepipe_enumerate_instances( simplepipe_t spp, simplepipe_list_t * splp )
{
    DIR * dp; // Directory handle
    struct dirent * ep; // Entity in the directory
    struct stat file_status; // Status of the current entity
    
    regex_t regex; // Compiled SimplePost pipe name matching regular expression
    
    simplepipe_list_t tail; // Last element in the list
    size_t count = 0; // Number of items in the list
    tail = *splp = NULL; // Failsafe
    
    dp = opendir( "/tmp/" );
    if( dp == NULL )
    {
        impact_printf_error( "%s: Failed to open the temporary directory\n", SP_MAIN_DESCRIPTION );
        return 0;
    }
    
    if( regcomp( &regex, "^simplepost_pipe_[0-9]+$", REG_EXTENDED | REG_NOSUB | REG_NEWLINE ) )
    {
        impact_printf_error( "%s: Failed to compile the pipe name matching regular expression\n", SP_MAIN_DESCRIPTION );
        closedir( dp );
        return 0;
    }
    
    while( (ep = readdir( dp )) )
    {
        if( stat( ep->d_name, &file_status ) == 0 && S_ISFIFO( file_status.st_mode ) )
        {
            int regex_ret = regexec( &regex, ep->d_name, 0, NULL, 0 );
            if( regex_ret == 0 && spp->pipe_name && strcmp( spp->pipe_name, ep->d_name ) == 0 )
            {
                if( tail )
                {
                    tail->next = simplepipe_list_init();
                    if( tail->next == NULL )
                    {
                        impact_printf_error( "%s:%d: Failed to add a new element to the element list\n", __FILE__, __LINE__ );
                        simplepipe_list_free( *splp );
                        *splp = NULL;
                        count = 0;
                        break;
                    }
                    tail->next->prev = tail;
                    tail = tail->next;
                }
                else
                {
                    tail = *splp = simplepipe_list_init();
                    if( tail == NULL )
                    {
                        impact_printf_error( "%s:%d: Failed to initialize the list\n", __FILE__, __LINE__ );
                        break;
                    }
                }
                count++;
                
                tail->pipe_name = (char *) malloc( sizeof( char ) * (strlen( ep->d_name ) + 1) );
                if( tail->pipe_name == NULL )
                {
                    impact_printf_debug( "%s:%d: Failed to allocate memory for pipe name\n", __FILE__, __LINE__ );
                    simplepipe_list_free( *splp );
                    *splp = NULL;
                    count = 0;
                    break;
                }
                strcpy( tail->pipe_name, ep->d_name );
                
                const char * pid_ptr = tail->pipe_name;
                while( isdigit( pid_ptr ) == 0 ) pid_ptr++;
                scanf( pid_ptr, "%d", &tail->pipe_pid );
            }
            else if( regex_ret != REG_NOMATCH )
            {
                char buffer[1024]; // regerror() error message
                
                regerror( regex_ret, &regex, buffer, sizeof( buffer )/sizeof( buffer[0] ) );
                impact_printf_error( "%s: %s\n", SP_MAIN_DESCRIPTION, buffer );
                
                if( *splp )
                {
                    simplepipe_list_free( *splp );
                    *splp = NULL;
                    count = 0;
                }
                
                break;
            }
        }
    }
    
    regfree( &regex );
    closedir( dp );
    
    return count;
}
