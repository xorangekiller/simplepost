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

#include "simplecmd.h"
#include "impact.h"
#include "config.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <dirent.h>
#include <regex.h>

/*
Command header strings
*/
#define SP_COMMAND_HEADER_NAMESPACE         "SimplePost::Command"
#define SP_COMMAND_HEADER_PROTOCOL_ERROR    "Local Protocol Error"

/*****************************************************************************
 *                              Socket Support                               *
 *****************************************************************************/

/*
Send a command to the client.

Remarks:
    This function doesn't fail by return value. It's possible that this will
    cause it to hang, but that is a problem for the caller. If the socket is not
    opened for reading on the other end, SIGPIPE will be sent to this program.
    http://man7.org/linux/man-pages/man2/socket.2.html

Arguments:
    sock [in]       Socket descriptor
    command [in]    Command to send to the client
                    If this parameter is NULL, no command will be sent.
    data [in]       Data to send to the client
                    The terminating character WILL NOT be sent!
                    If this parameter is NULL, no data will be sent.
*/
static void __sock_send( int sock, const char * command, const char * data )
{
    char buffer[30]; // Number of characters to be written
    
    if( command )
    {
        sprintf( buffer, "%lu", strlen( command ) );
        write( sock, buffer, strlen( buffer ) + 1 );
        write( sock, command, strlen( command ) );
    }
    
    if( data )
    {
        sprintf( buffer, "%lu", strlen( data ) );
        write( sock, buffer, strlen( buffer ) + 1 );
        
        // According to POSIX.1-2001, the results of attempting to write() a
        // zero-length string to anything other than a regular file are undefined.
        // Since the command set handled by this program is well-defined, we don't
        // need to worry about the contingency of sending a zero-length command above;
        // if it occurs, it is clearly a bug. On the other hand, the data accompanying
        // each command could be almost anything. To avoid calling the equivalent of
        // write( descriptor, "", 0 ); we will write() the length "0" (above), but no
        // accompanying data.
        if( !(buffer[0] == '0' && buffer[1] == '\0') ) write( sock, data, strlen( data ) );
    }
}

/*
Send a command to the client and read the response.

Arguments:
    sock [in]       Socket descriptor
    command [in]    Command to send to the client
                    If this parameter is NULL, no command will be sent.
    data [out]      NULL-terminated string from the client
                    If *data != NULL, you are responsible for freeing it.

Return Value:
    The number of bytes written to the read buffer will be returned. If an
    error occurred (or the operation timed out), zero will be returned instead.
*/
static size_t __sock_recv( int sock, const char * command, char ** data )
{
    char buffer[30]; // Number of characters to be read from the buffer
    size_t length; // Number of characters received
    char b; // Last byte read from the client
    *data = NULL; // Failsafe
    
    if( command ) __sock_send( sock, command, NULL );
    
    for( size_t i = 0; read( sock, (void *) &b, 1 ) == 1; i++ )
    {
        if( i == sizeof( buffer ) )
        {
            impact_printf_error( "%s: %s: String size cannot be longer than %lu bytes\n", SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR, sizeof( buffer ) );
            return 0;
        }
        
        buffer[i] = b;
        if( b == '\0' ) break;
    }
    
    if( sscanf( buffer, "%lu", &length ) == EOF )
    {
        impact_printf_error( "%s: %s: %s is not a valid string size\n", SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR, buffer );
        return 0;
    }
    
    *data = (char *) malloc( sizeof( char ) * (length + 1) );
    if( *data == NULL )
    {
        impact_printf_debug( "%s: %s: Failed to allocate memory for command data buffer\n", SP_COMMAND_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC );
        
        if( command ) impact_printf_error( "%s: Data buffer required for command %s\n", SP_COMMAND_HEADER_NAMESPACE, command );
        else impact_printf_error( "%s: Buffer required to receive data\n", SP_COMMAND_HEADER_NAMESPACE );
        
        return 0;
    }
    
    // The data received may be a zero-length string. The following loop handles
    // this condition implicitly, but it is not to be overlooked. Since no
    // terminating character is sent for data (unlike for its length), a
    // zero-length data string effectively means that we should not attempt to
    // read anything from the socket.
    for( size_t i = 0; i < length; i++ )
    {
        if( read( sock, (void *) &b, 1 ) == 1 )
        {
            (*data)[i] = b;
        }
        else
        {
            impact_printf_error( "%s: Read terminated before we received %zu bytes\n", SP_COMMAND_HEADER_NAMESPACE, length );
            
            free( *data );
            *data = NULL;
            
            return 0;
        }
    }
    (*data)[length] = '\0';
    
    return length;
}

/*****************************************************************************
 *                            SimpleCommand List                             *
 *****************************************************************************/

/*
Initialize a SimplePost Command List instance.

Return Value:
    An initialized SimplePost Command List instance will be returned unless an
    error occurred, in which case NULL will be returned instead.
*/
simplecmd_list_t simplecmd_list_init()
{
    simplecmd_list_t sclp = (simplecmd_list_t) malloc( sizeof( struct simplecmd_list ) );
    if( sclp == NULL ) return NULL;
    
    sclp->sock_name = NULL;
    sclp->instance_pid = 0;
    
    sclp->next = NULL;
    sclp->prev = NULL;
    
    return sclp;
}

/*
Free the given SimplePost Command List instance.

Arguments:
    sclp [in]   Instance to act on
*/
void simplecmd_list_free( simplecmd_list_t sclp )
{
    while( sclp )
    {
        simplecmd_list_t p = sclp;
        sclp = sclp->next;
        if( p->sock_name ) free( p->sock_name );
        free( p );
    }
}

/*
Get a list of all SimplePost instances on the system with open sockets,
excluding this one.

Arguments:
    sclp [out]  List of all open SimplePost sockets (excluding ours)
                If *sclp != NULL, you are responsible for freeing it.

Return Value:
    The number of instances in the list will be returned. If zero is returned,
    either a memory allocation error occurred, or, more likely, there are no
    other instances of SimplePost currently active.
*/
size_t simplecmd_list_instances( simplecmd_list_t * sclp )
{
    DIR * dp; // Directory handle
    struct dirent * ep; // Entity in the directory
    char suspect[512]; // File name and path of the current entity
    struct stat suspect_status; // Status of the current entity
    
    char sock_name[512]; // Name of our socket
    regex_t regex; // Compiled SimplePost socket matching regular expression
    
    simplecmd_list_t tail; // Last element in the list
    size_t count = 0; // Number of items in the list
    tail = *sclp = NULL; // Failsafe
    
    dp = opendir( "/tmp/" );
    if( dp == NULL )
    {
        impact_printf_error( "%s: Failed to open the temporary directory\n", SP_COMMAND_HEADER_NAMESPACE );
        return 0;
    }
    
    sprintf( sock_name, "^%s_sock_[0-9]+$", SP_MAIN_SHORT_NAME );
    if( regcomp( &regex, sock_name, REG_EXTENDED | REG_NOSUB | REG_NEWLINE ) )
    {
        impact_printf_error( "%s: Failed to compile the socket matching regular expression\n", SP_COMMAND_HEADER_NAMESPACE );
        closedir( dp );
        return 0;
    }
    
    sprintf( sock_name, "%s_sock_%d", SP_MAIN_SHORT_NAME, getpid() );
    
    while( (ep = readdir( dp )) )
    {
        strncpy( suspect, "/tmp/", sizeof( suspect )/sizeof( suspect[0] ) );
        strncat( suspect, ep->d_name, sizeof( suspect )/sizeof( suspect[0] ) - strlen( suspect ) );
        
        if( stat( suspect, &suspect_status ) == 0 && S_ISSOCK( suspect_status.st_mode ) )
        {
            int regex_ret = regexec( &regex, ep->d_name, 0, NULL, 0 );
            if( regex_ret == 0 && strcmp( sock_name, ep->d_name ) != 0 )
            {
                if( tail )
                {
                    tail->next = simplecmd_list_init();
                    if( tail->next == NULL )
                    {
                        impact_printf_debug( "%s: %s: Failed to add a new element to the element list\n", SP_COMMAND_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC );
                        
                        simplecmd_list_free( *sclp );
                        *sclp = NULL;
                        count = 0;
                        
                        break;
                    }
                    tail->next->prev = tail;
                    tail = tail->next;
                }
                else
                {
                    tail = *sclp = simplecmd_list_init();
                    if( tail == NULL )
                    {
                        impact_printf_error( "%s:%d: Failed to initialize the list\n", __FILE__, __LINE__ );
                        break;
                    }
                }
                count++;
                
                tail->sock_name = (char *) malloc( sizeof( char ) * (strlen( suspect ) + 1) );
                if( tail->sock_name == NULL )
                {
                    impact_printf_debug( "%s: %s: Failed to allocate memory for socket name\n", SP_COMMAND_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC );
                    
                    simplecmd_list_free( *sclp );
                    *sclp = NULL;
                    count = 0;
                    
                    break;
                }
                strcpy( tail->sock_name, suspect );
                
                const char * pid_ptr = suspect;
                while( isdigit( *pid_ptr ) == 0 ) pid_ptr++;
                sscanf( pid_ptr, "%d", &tail->instance_pid );
                
                impact_printf_debug( "%s: Found %s:%d socket %s\n", SP_COMMAND_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, tail->instance_pid, tail->sock_name );
            }
            else if( regex_ret != REG_NOMATCH )
            {
                char buffer[1024]; // regerror() error message
                
                regerror( regex_ret, &regex, buffer, sizeof( buffer )/sizeof( buffer[0] ) );
                impact_printf_error( "%s: %s\n", SP_COMMAND_HEADER_NAMESPACE, buffer );
                
                if( *sclp )
                {
                    simplecmd_list_free( *sclp );
                    *sclp = NULL;
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

/*****************************************************************************
 *                       SimpleCommand Server Private                        *
 *****************************************************************************/

/*
SimplePost command handler structure
*/
struct simplecmd_handler
{
    const char * request; // Request sent by the client
    short (*handler) (simplecmd_t, int); // Function to process the command
};

/*
Prototypes of the command handlers
*/
static short __command_send_address( simplecmd_t scp, int sock );
static short __command_send_port( simplecmd_t scp, int sock );
static short __command_send_version( simplecmd_t scp, int sock );
static short __command_recv_file( simplecmd_t scp, int sock );

/*
SimplePost commands to handle
*/
static struct simplecmd_handler __command_handlers[] =
{
    {"GetAddress", &__command_send_address},
    {"GetPort", &__command_send_port},
    {"GetVersion", &__command_send_version},
    {"SetFile", &__command_recv_file}
};

/*
Indexes of the commands in __command_handlers[]
*/
#define SP_COMMAND_GET_ADDRESS  0
#define SP_COMMAND_GET_PORT     1
#define SP_COMMAND_GET_VERSION  2
#define SP_COMMAND_SET_FILE     3

#define SP_COMMAND_MIN          0
#define SP_COMMAND_MAX          3

/*
SimplePost container for processing client requests
*/
struct simplecmd_request
{
    struct simplecmd * scp; // SimplePost command instance to act on
    int client_sock;        // Socket the client connected on
};

/*
SimplePost command server status structure
*/
struct simplecmd
{
    /* Initialization */
    int sock;                       // Socket for local commands
    char * sock_name;               // Absolute file name of the socket
    pthread_t accept_thread;        // Handle of the primary thread
    
    /* Clients */
    short accpeting_clients;        // Are we accepting client connections?
    size_t client_count;            // Number of clients currently being served
    simplepost_t spp;               // SimplePost handle
};

/*
Send the primary address our web server is bound to to the client.

Remarks:
    If the web server is not running, a zero-length string will be sent. See
    the related comments in __sock_send() and __sock_recv() to get a better
    understanding of how this contingency is handled.

Arguments:
    scp [in]    Instance to act on
    sock [in]   Client socket

Return Value:
    0   Failed to respond to the request.
    1   The requested information was sent successfully.
*/
static short __command_send_address( simplecmd_t scp, int sock )
{
    char * address; // Address of the web server
    size_t length; // Length of the address string
    
    length = simplepost_get_address( scp->spp, &address );
    if( address == NULL || length == 0 ) return 0;
    
    __sock_send( sock, NULL, address ? address : "" );
    free( address );
    
    return 1;
}

/*
Send the port our web server is listening on to the client.

Remarks:
    If the web server is not running, zero will be sent as the port number.

Arguments:
    scp [in]    Instance to act on
    sock [in]   Client socket

Return Value:
    0   Failed to respond to the request.
    1   The requested information was sent successfully.
*/
static short __command_send_port( simplecmd_t scp, int sock )
{
    char buffer[30]; // Port as a string
    unsigned short port; // Port the web server is listening on
    
    port = simplepost_get_port( scp->spp );
    if( port == 0 ) return 0;
    
    if( sprintf( buffer, "%u", port ) <= 0 ) return 0;
    __sock_send( sock, NULL, buffer );
    
    return 1;
}

/*
Send the current program version to the client.

Arguments:
    scp [in]    Instance to act on
    sock [in]   Client socket

Return Value:
    0   Failed to respond to the request.
    1   The requested information was sent successfully.
*/
static short __command_send_version( simplecmd_t scp, int sock )
{
    __sock_send( sock, NULL, SP_MAIN_VERSION );
    return 1;
}

/*
Receive a file and count from the client and add it our web server.

Arguments:
    scp [in]    Instance to act on
    sock [in]   Client socket

Return Value:
    0   Failed to respond to the request.
    1   The requested information was sent successfully.
*/
static short __command_recv_file( simplecmd_t scp, int sock )
{
    char * url; // URL of the file being served
    char * file; // Name and path of the file to serve
    char * buffer; // Count as a string
    unsigned int count; // Number of times the file should be served
    
    if( __sock_recv( sock, NULL, &file ) == 0 ) return 0;
    
    if( __sock_recv( sock, NULL, &buffer ) == 0 )
    {
        free( file );
        return 0;
    }
    
    if( sscanf( buffer, "%u", &count ) == EOF )
    {
        impact_printf_error( "%s: %s: %s is not a port number\n", SP_COMMAND_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC, buffer );
        
        free( buffer );
        free( file );
        
        return 0;
    }
    
    if( simplepost_serve_file( scp->spp, &url, file, NULL, count ) == 0 ) return 0;
    if( url ) free( url );
    
    free( buffer );
    free( file );
    
    return 1;
}

/*
Process a request accepted by the server.

Arguments:
    p [in]  SimplePost command + client socket wrapper

Return Value:
    NULL is always returned (for pthreads compatibility).
*/
static void * __process_request( void * p )
{
    struct simplecmd_request * scrp = (struct simplecmd_request *) p; // Properly cast SimplePost command request handle
    simplecmd_t scp = scrp->scp; // SimplePost command instance to act on
    int sock = scrp->client_sock; // Socket the client connected on
    
    free( scrp );
    scrp = p = NULL;
    
    char * command; // Command to process
    size_t length; // Length of the command string
    short response; // Command handler return value
    
    scp->client_count++;
    
    length = __sock_recv( sock, NULL, &command );
    if( command == NULL || length == 0 ) goto error;
    response = -1;
    
    for( unsigned int i = SP_COMMAND_MIN; i <= SP_COMMAND_MAX; i++ )
    {
        if( strcmp( command, __command_handlers[i].request ) == 0 )
        {
            impact_printf_debug( "%s: Request 0x%lx: Responding to %s command\n", SP_COMMAND_HEADER_NAMESPACE, pthread_self(), __command_handlers[i].request );
            response = (*__command_handlers[i].handler)( scp, sock );
            break;
        }
    }
    
    #ifdef DEBUG
    switch( response )
    {
        case -1:
            impact_printf_debug( "%s: Request 0x%lx: %s is not a supported command\n", SP_COMMAND_HEADER_NAMESPACE, pthread_self(), command );
            break;
        case 0:
            impact_printf_debug( "%s: Request 0x%lx: Failed to process %s command\n", SP_COMMAND_HEADER_NAMESPACE, pthread_self(), command );
            break;
        case 1:
            impact_printf_debug( "%s: Request 0x%lx: Successfully processed %s command\n", SP_COMMAND_HEADER_NAMESPACE, pthread_self(), command );
            break;
        default:
            impact_printf_debug( "%s: Request 0x%lx: %s returned with an invalid state: %d\n", SP_COMMAND_HEADER_NAMESPACE, pthread_self(), command, response );
            break;
    }
    #endif // DEBUG
    
    error:
    impact_printf_debug( "%s: Request 0x%lx: Closing client %d\n", SP_COMMAND_HEADER_NAMESPACE, pthread_self(), sock );
    close( sock );
    
    if( command ) free( command );
    scp->client_count--;
    
    return NULL;
}

/*
Start accepting requests from clients.

Arguments:
    p [in]  Instance to act on

Return Value:
    NULL is always returned (for pthreads compatibility).
*/
static void * __accept_requests( void * p )
{
    simplecmd_t scp = (simplecmd_t) p; // Properly cast SimplePost command handle
    
    int client_sock; // Socket the client connected on
    struct sockaddr_in client_name; // Name of the client
    socklen_t client_name_len; // Length of the client name structure
    pthread_t client_thread; // Handle of the client thread
    
    struct timespec timeout; // Maximum time to wait for a connection before checking the shutdown sentinel
    fd_set fds; // File descriptor set (for pselect() socket monitoring)
    
    scp->client_count = 0;
    scp->accpeting_clients = 1;
    
    while( scp->accpeting_clients )
    {
        FD_ZERO( &fds );
        FD_SET( scp->sock, &fds );
        
        timeout.tv_sec = 2;
        timeout.tv_nsec = 0;
        
        switch( pselect( scp->sock + 1, &fds, NULL, NULL, &timeout, NULL ) )
        {
            case -1:
                impact_printf_error( "%s: Cannot accept connections on socket %d\n", SP_COMMAND_HEADER_NAMESPACE, scp->sock );
                scp->accpeting_clients = 0;
            case 0:
                continue;
        }
        
        client_name_len = sizeof( client_name );
        client_sock = accept( scp->sock, (struct sockaddr *) &client_name, &client_name_len );
        if( client_sock == -1 ) continue;
        
        struct simplecmd_request * scrp = (struct simplecmd_request *) malloc( sizeof( struct simplecmd_request ) );
        if( scrp == NULL )
        {
            impact_printf_error( "%s: %s: Failed to allocate memory for a new command request thread\n", SP_COMMAND_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC );
            scp->accpeting_clients = 0;
            continue;
        }
        scrp->scp = scp;
        scrp->client_sock = client_sock;
        
        if( pthread_create( &client_thread, NULL, &__process_request, (void *) scrp ) == 0 )
        {
            impact_printf_debug( "%s: Launched request processing thread 0x%lx for client %d\n", SP_COMMAND_HEADER_NAMESPACE, client_thread, client_sock );
            pthread_detach( client_thread );
        }
        else
        {
            impact_printf_debug( "%s: Failed to launch request processing thread for client %d\n", SP_COMMAND_HEADER_NAMESPACE, client_sock );
            close( scrp->client_sock );
            free( scrp );
        }
    }
    
    impact_printf_debug( "%s: Waiting for %lu clients to finish processing ...\n", SP_COMMAND_HEADER_NAMESPACE, scp->client_count );
    while( scp->client_count ) usleep( 1000 );
    
    impact_printf_debug( "%s: Closing socket %d\n", SP_COMMAND_HEADER_NAMESPACE, scp->sock );
    close( scp->sock );
    scp->sock = -1;
    remove( scp->sock_name );
    
    return NULL;
}

/*****************************************************************************
 *                       SimpleCommand Server Public                         *
 *****************************************************************************/

/*
Initialize a SimplePost command instance.

Return Value:
    An initialized SimplePost command instance will be returned unless an error
    occurred, in which case NULL will be returned instead.
*/
simplecmd_t simplecmd_init()
{
    simplecmd_t scp = (simplecmd_t) malloc( sizeof( struct simplecmd ) );
    if( scp == NULL ) return NULL;
    
    scp->sock = -1;
    scp->sock_name = NULL;
    scp->accept_thread = -1;
    
    scp->accpeting_clients = 0;
    scp->client_count = 0;
    scp->spp = NULL;
    
    return scp;
}

/*
Free the given SimplePost command instance.

Arguments:
    scp [in]    Instance to act on
*/
void simplecmd_free( simplecmd_t scp )
{
    if( scp->accpeting_clients ) simplecmd_deactivate( scp );
    
    if( scp->sock != -1 )
    {
        impact_printf_debug( "%s:%d: BUG! simplecmd_deactivate() should have (directly or indirectly) closed the socket already!\n", __FILE__, __LINE__ );
        close( scp->sock );
    }
    
    if( scp->sock_name )
    {
        remove( scp->sock_name );
        free( scp->sock_name );
    }
}

/*
Start accepting client commands.

Arguments:
    scp [in]    Instance to act on
    spp [in]    SimplePost instance to back our client requests

Return Value:
    0   The command server is already running or could not be started.
    1   The command server was successfully started.
*/
short simplecmd_activate( simplecmd_t scp, simplepost_t spp )
{
    if( scp->sock != -1 )
    {
        impact_printf_error( "%s: Server is already activated\n", SP_COMMAND_HEADER_NAMESPACE );
        return 0;
    }
    
    if( spp == NULL )
    {
        impact_printf_error( "%s: Cannot activate command server without a %s instance\n", SP_COMMAND_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION );
        return 0;
    }
    scp->spp = spp;
    
    if( scp->sock_name == NULL )
    {
        char buffer[2048]; // Buffer for the the socket name string
        
        sprintf( buffer, "/tmp/%s_sock_%d", SP_MAIN_SHORT_NAME, getpid() );
        
        scp->sock_name = (char *) malloc( sizeof( char ) * (strlen( buffer ) + 1) );
        if( scp->sock_name == NULL )
        {
            impact_printf_error( "%s: %s: Failed to allocate memory for the socket name\n", SP_COMMAND_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC );
            return 0;
        }
        strcpy( scp->sock_name, buffer );
    }
    
    scp->sock = socket( AF_UNIX, SOCK_STREAM, 0 );
    if( scp->sock == -1 )
    {
        impact_printf_error( "%s: Socket could not be created\n", SP_COMMAND_HEADER_NAMESPACE );
        return 0;
    }
    
    struct sockaddr_un sock_addr; // Address to assign to the socket
    memset( &sock_addr, 0, sizeof( struct sockaddr_un ) );
    sock_addr.sun_family = AF_UNIX;
    strncpy( sock_addr.sun_path, scp->sock_name, sizeof( sock_addr.sun_path ) - 1 );
    
    if( bind( scp->sock, (struct sockaddr *) &sock_addr, sizeof( struct sockaddr_un ) ) == -1 )
    {
        impact_printf_error( "%s: Failed to bind %s to socket %d\n", SP_COMMAND_HEADER_NAMESPACE, scp->sock_name, scp->sock );
        goto error;
    }
    
    if( listen( scp->sock, 30 ) == -1 )
    {
        impact_printf_error( "%s: Cannot listen on socket %d\n", SP_COMMAND_HEADER_NAMESPACE, scp->sock );
        goto error;
    }
    
    if( pthread_create( &scp->accept_thread, NULL, &__accept_requests, (void *) scp ) != 0 )
    {
        impact_printf_error( "%s: Failed to create listen thread for %s\n", SP_COMMAND_HEADER_NAMESPACE, scp->sock_name );
        goto error;
    }
    
    impact_printf_standard( "%s: Now accepting commands on %s\n", SP_COMMAND_HEADER_NAMESPACE, scp->sock_name );
    
    return 1;
    
    error:
    close( scp->sock );
    scp->sock = -1;
    
    remove( scp->sock_name );
    free( scp->sock_name );
    scp->sock_name = NULL;
    
    return 0;
}

/*
Stop accepting client commands.

Arguments:
    scp [in]    Instance to act on

Return Value:
    0   The command server is not running or could not be shut down.
    1   The command server has been successfully killed.
*/
short simplecmd_deactivate( simplecmd_t scp )
{
    if( scp->sock == -1 )
    {
        impact_printf_error( "%s: Server is not active\n", SP_COMMAND_HEADER_NAMESPACE );
        return 0;
    }
    
    #ifdef DEBUG
    pthread_t accept_thread = scp->accept_thread;
    #endif // DEBUG
    
    impact_printf_standard( "%s: Shutting down ...\n", SP_COMMAND_HEADER_NAMESPACE );
    
    scp->accpeting_clients = 0;
    pthread_join( scp->accept_thread, NULL );
    
    impact_printf_debug( "%s: 0x%lx cleanup complete\n", SP_COMMAND_HEADER_NAMESPACE, accept_thread );
    
    return 1;
}

/*
Are we listening for client connections?

Arguments:
    scp [in]    Instance to act on

Return Value:
    0   The server is not running.
    1   The server is alive!
*/
short simplecmd_is_alive( simplecmd_t scp )
{
    return (scp->sock == -1) ? 0 : 1;
}

/*****************************************************************************
 *                       SimpleCommand Client Private                        *
 *****************************************************************************/

/*
Open a socket to the server with the specified process identifier.

Remarks:
    This functions prints error messages so its consumers don't have to.

Arguments:
    server_pid [in] Process identifier of the server

Return Value:
    If the specified PID belongs to a compatible SimplePost server with an open
    local socket, an open socket descriptor will be returned. If POSIX open()
    encounters an error, -1 will be returned (as per POSIX.1-2008). If this
    function encounters an error, -2 will be returned.
*/
static int __open_sock_by_pid( pid_t server_pid )
{
    int sock; // Socket descritor
    struct sockaddr_un sock_addr; // Address to assign to the socket
    char sock_name[512]; // Name of the socket
    struct stat sock_status; // Status of the socket
    
    sprintf( sock_name, "/tmp/%s_sock_%d", SP_MAIN_SHORT_NAME, server_pid );
    if( stat( sock_name, &sock_status ) == -1 )
    {
        impact_printf_error( "%s: Socket %s does not exist\n", SP_COMMAND_HEADER_NAMESPACE, sock_name );
        return -2;
    }
    if( S_ISSOCK( sock_status.st_mode ) == 0 )
    {
        impact_printf_error( "%s: %s is not a socket\n", SP_COMMAND_HEADER_NAMESPACE, sock_name );
        return -2;
    }
    
    sock = socket( AF_UNIX, SOCK_STREAM, 0 );
    if( sock == -1 )
    {
        impact_printf_error( "%s: Failed to open socket %s\n", SP_COMMAND_HEADER_NAMESPACE, sock_name );
        return -1;
    }
    
    memset( &sock_addr, 0, sizeof( struct sockaddr_un ) );
    sock_addr.sun_family = AF_UNIX;
    strncpy( sock_addr.sun_path, sock_name, sizeof( sock_addr.sun_path ) - 1 );
    
    if( connect( sock, (struct sockaddr *) &sock_addr, sizeof( sock_addr ) ) == -1 )
    {
        impact_printf_error( "%s: Failed to connect to socket %s\n", SP_COMMAND_HEADER_NAMESPACE, sock_name );
        close( sock );
        return -1;
    }
    
    return sock;
}

/*****************************************************************************
 *                       SimpleCommand Client Public                         *
 *****************************************************************************/

/*
Get the address the specified server is bound to.

Arguments:
    server_pid [in] Process identifier of the server to query
    address [out]   Address of the server
                    
                    The storage for this string will be dynamically allocated.
                    You are responsible for freeing it (unless it is NULL, in
                    which case an error occurred).

Return Value:
    The number of characters written to the address (excluding the NULL-
    terminating character) will be returned.
*/
size_t simplecmd_get_address( pid_t server_pid, char ** address )
{
    int sock; // Socket descritor
    size_t length = 0; // Length of the address
    *address = NULL; // Failsafe
    
    sock = __open_sock_by_pid( server_pid );
    if( sock < 0 ) return 0;
    
    length = __sock_recv( sock, __command_handlers[SP_COMMAND_GET_ADDRESS].request, address );
    
    close( sock );
    
    return length;
}

/*
Get the port the specified server is listening on.

Arguments:
    server_pid [in] Process identifier of the server to query

Return Value:
    The server's port number will be returned.
*/
unsigned short simplecmd_get_port( pid_t server_pid )
{
    int sock; // Socket descritor
    unsigned short port; // Port the specified server is listening on
    char * buffer; // Port as a string (directly from the server)
    
    sock = __open_sock_by_pid( server_pid );
    if( sock < 0 ) return 0;
    
    __sock_recv( sock, __command_handlers[SP_COMMAND_GET_PORT].request, &buffer );
    if( buffer )
    {
        if( sscanf( buffer, "%hu", &port ) == EOF )
        {
            impact_printf_error( "%s: %s: %s is not a port number\n", SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR, buffer );
            port = 0;
        }
        free( buffer );
    }
    else
    {
        port = 0;
    }
    
    close( sock );
    
    return port;
}

/*
Get the version of the specified server.

Arguments:
    server_pid [in] Process identifier of the server to query
    version [out]   Version of the server
                    
                    The storage for this string will be dynamically allocated.
                    You are responsible for freeing it (unless it is NULL, in
                    which case an error occurred).

Return Value:
    The number of characters written to the version string (excluding the NULL-
    terminating character) will be returned.
*/
size_t simplecmd_get_version( pid_t server_pid, char ** version )
{
    int sock; // Socket descritor
    size_t length = 0; // Length of the version string
    *version = NULL; // Failsafe
    
    sock = __open_sock_by_pid( server_pid );
    if( sock < 0 ) return 0;
    
    length = __sock_recv( sock, __command_handlers[SP_COMMAND_GET_VERSION].request, version );
    
    close( sock );
    
    return length;
}

/*
Add a file to the specified server.

Arguments:
    server_pid [in] Process identifier of the server to act on
    file [in]       Name and path of the file to serve
    count [in]      Number of times the file should be served

Return Value:
    0   Something went wrong. The file was not added to the server.
    1   The file was successfully added to the server.
*/
short simplecmd_set_file( pid_t server_pid, const char * file, unsigned int count )
{
    int sock; // Socket descritor
    char buffer[512]; // Count as a string
    
    sock = __open_sock_by_pid( server_pid );
    if( sock < 0 ) return 0;
    
    __sock_send( sock, __command_handlers[SP_COMMAND_SET_FILE].request, file );
    sprintf( buffer, "%u", count );
    __sock_send( sock, NULL, buffer );
    
    close( sock );
    
    return 1;
}
