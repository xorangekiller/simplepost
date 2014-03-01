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

#include "simplepost.h"
#include "config.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <pthread.h>
#include <magic.h>

/*****************************************************************************
 *                              Error Handling                               *
 *****************************************************************************/

/*
SimplePost error containment structure
*/
struct simplepost_error
{
    unsigned int id; // Numeric error code
    char * msg; // Human readable message associated with the error code
};

/*
SimplePost error codes
*/
static const char * simplepost_error_table[] =
{
    "No error",                                     // SP_ERROR_NONE
    "Server is already initialized",                // SP_ERROR_INITIALIZED
    "Socket could not be created",                  // SP_ERROR_SOCKET
    "Server failed to bind to the socket",          // SP_ERROR_BIND
    "Invalid source address specified",             // SP_ERROR_ADDRESS
    "Port could not be allocated",                  // SP_ERROR_PORTALLOC
    "Cannot listen on socket",                      // SP_ERROR_LISTEN
    "Cannot accept connections on socket",          // SP_ERROR_ACCEPT
    "Specified file does not exist",                // SP_ERROR_FILE_DOESNT_EXIST
    "Cannot add more than the maximum file count",  // SP_ERROR_TOO_MANY_FILES
    "Cannot insert file",                           // SP_ERROR_FILE_INSERT_FAILED
    "Buffer is too small to receive the request",   // SP_ERROR_RECVREQUEST
    "Request does not specify a HTTP method",       // SP_ERROR_NO_METHOD
    "HTTP method of the request is invalid",        // SP_ERROR_INVALID_METHOD
    "Request does not specify a HTTP URI",          // SP_ERROR_NO_URI
    "HTTP URI of the request is invalid",           // SP_ERROR_INVALID_URI
    "HTTP URI is already in use",                   // SP_ERROR_URI_ALREADY_TAKEN
    "Request does not specify a HTTP version",      // SP_ERROR_NO_VERSION
    "Requested resource is not available",          // SP_ERROR_RESOURCE_NOT_FOUND
    "HTTP version of the request is not supported", // SP_ERROR_INVALID_VERSION
    "Server is not running",                        // SP_ERROR_UNINITIALIZED
    "Unknown error"                                 // SP_ERROR_UNIDENTIFIED
};

/*****************************************************************************
 *                              Socket Support                               *
 *****************************************************************************/

/*
Get a line of text from a socket.

Remarks:
    This function handles strings that end in LF, CR, or CRLF. Technically
    HTTP/1.1 (RFC 2616) mandates that all lines end in CRLF, but non-conferment
    clients sometimes terminate with just one or the other.

Arguments:
    sock [in]       Socket to retrieve the characters from
    buffer [out]    Buffer that the NULL-terminated string will be written to
                    If the end of the buffer is encountered before a newline,
                    the buffer will be terminated with a NULL character.
    size [in]       Length of the buffer, above

Return Value:
    The number of bytes written to the buffer, excluding the NULL character,
    will be returned.
*/
static size_t __recv_line( int sock, char * buffer, size_t size )
{
    char byte; // Last character read from the socket
    ssize_t received; // Number of characters received from a socket read operation
    size_t count = 0; // Number of bytes written to the buffer
    
    while( count < (size - 1) )
    {
        received = recv( sock, &byte, 1, 0 );
        if( received == 0 || received == -1 ) break;
        
        if( byte == '\n' || byte == '\0' ) break;
        if( byte == '\r' )
        {
            received = recv( sock, &byte, 1, MSG_PEEK );
            if( received == 0 || received == -1 ) break;
            if( byte != '\n' ) break;
        }
        
        buffer[count++] = byte;
    }
    buffer[count] = '\0';
    
    return count;
}

/*
Get a line of text from a socket.

Remarks:
    This function is identical to __recv_line(), except that it dynamically
    allocates the buffer instead of relying on a fixed size.

Arguments:
    sock [in]       Socket to retrieve the characters from
    buffer [out]    Pointer to the buffer for the NULL-terminated output string
                    The storage for this string will be dynamically allocated.
                    You are responsible for freeing it (unless it is NULL, in
                    which case an error occurred).

Return Value:
    The number of bytes written to the buffer, excluding the NULL character,
    will be returned. The actual size of the buffer may be slightly larger than
    this length, but not by much.
*/
static size_t __recv_line_dynamic( int sock, char ** buffer )
{
    size_t buffer_size; // Length of the buffer
    size_t buffer_length; // Length of the line
    const size_t buffer_base_size = 32; // Minimum buffer length
    
    buffer_size = buffer_base_size;
    *buffer = (char *) malloc( sizeof( char ) * buffer_size );
    if( *buffer == NULL ) return 0;
    
    buffer_length = __recv_line( sock, *buffer, buffer_size );
    while( buffer_length + 1 == buffer_size )
    {
        char * new_buffer = realloc( *buffer, sizeof( char ) * (buffer_size + buffer_base_size) );
        if( new_buffer == NULL )
        {
            free( *buffer );
            *buffer = NULL;
            return 0;
        }
        
        *buffer = new_buffer;
        buffer_length = __recv_line( sock, *buffer + (buffer_size - 1), buffer_base_size );
        buffer_size += buffer_base_size;
    }
    
    return buffer_length;
}

/*****************************************************************************
 *                               File Support                                *
 *****************************************************************************/

/*
SimplePost container of files being served
*/
struct simplepost_serve
{
    char * file;            // Name and path of the file on the filesystem
    char * uri;             // Uniform Resource Identifier assigned to the file
    unsigned int count;     // Number of times the file may be downloaded
    
    struct simplepost_serve * next; // Next file in the doubly-linked list
    struct simplepost_serve * prev; // Previous file in the doubly-linked list
};

/*
Initialize a blank SimpleServe instance.

Return Value:
    On success a pointer to the new instance will be returned.
    If we failed to allocate the requested memory, a NULL pointer will be returned.
*/
static struct simplepost_serve * __simplepost_serve_init()
{
    struct simplepost_serve * spsp = (struct simplepost_serve *) malloc( sizeof( struct simplepost_serve ) );
    if( spsp == NULL ) return NULL;
    
    spsp->file = NULL;
    spsp->uri = NULL;
    spsp->count = 0;
    
    spsp->next = NULL;
    spsp->prev = NULL;
    
    return spsp;
}

/*
Free the given SimpleServe instance.

Remarks:
    This function will only free from spsp to the end of the list. If spsp is not
    the first element in the list (spsp->prev != NULL), the beginning of the
    list (the part this function won't free) will be properly terminated.

Arguments:
    spsp [in]   List to free
*/
static void __simplepost_serve_free( struct simplepost_serve * spsp )
{
    if( spsp->prev ) spsp->prev->next = NULL;
    
    while( spsp )
    {
        struct simplepost_serve * p = spsp;
        spsp = spsp->next;
        
        if( p->file ) free( p->file );
        if( p->uri ) free( p->uri );
        free( p );
    }
}

/*
Insert the second list before the current element in the first list.

Arguments:
    spsp1 [in]  List to insert into
    spsp2 [in]  List to insert
                If this parameter is NULL, a single new element will be inserted.

Return Value:
    The modified list will be returned at the inserted element.
    If something went wrong, a NULL pointer will be returned instead.
*/
static struct simplepost_serve * __simplepost_serve_insert_before( struct simplepost_serve * spsp1, struct simplepost_serve * spsp2 )
{
    if( spsp2 == NULL )
    {
        spsp2 = __simplepost_serve_init();
        if( spsp2 == NULL ) return NULL;
    }
    
    spsp2->prev = spsp1->prev;
    if( spsp2->next )
    {
        struct simplepost_serve * p; // Last element in spsp2
        for( p = spsp2; p->next != NULL; p = p->next );
        p->next = spsp1;
        spsp1->prev = p;
    }
    else
    {
        spsp2->next = spsp1;
        spsp1->prev = spsp2;
    }
    
    return spsp2;
}

/*
Insert the second list after the current element in the first list.

Arguments:
    spsp1 [in]  List to insert into
    spsp2 [in]  List to insert
                If this parameter is NULL, a single new element will be inserted.

Return Value:
    The modified list will be returned at the inserted element.
    If something went wrong, a NULL pointer will be returned instead.
*/
static struct simplepost_serve * __simplepost_serve_insert_after( struct simplepost_serve * spsp1, struct simplepost_serve * spsp2 )
{
    if( spsp2 == NULL )
    {
        spsp2 = __simplepost_serve_init();
        if( spsp2 == NULL ) return NULL;
    }
    
    spsp2->prev = spsp1;
    if( spsp2->next )
    {
        struct simplepost_serve * p; // Last element in spsp2
        for( p = spsp2; p->next != NULL; p = p->next );
        p->next = spsp1->next;
    }
    else
    {
        spsp2->next = spsp1->next;
    }
    spsp1->next = spsp2;
    
    return spsp2;
}

/*
Remove the specified number of elements from the list.

Arguments:
    spsp [in]   List to modify
                The element assigned to this parameter will be the first
                element in the list to be removed. THIS FUNCTION ONLY TRAVERSES
                THE LIST FORWARD, NEVER BACKWARD.
    n [in]      Number of elements to remove
                If the list has fewer than n elements (from spsp forward), every
                element will be removed from spsp to the end of the list. n = 0
                invokes the same behavior.
*/
static void __simplepost_serve_remove( struct simplepost_serve * spsp, size_t n )
{
    if( n == 0 )
    {
        __simplepost_serve_free( spsp );
    }
    else
    {
        struct simplepost_serve * top = spsp; // Top element in the list
        struct simplepost_serve * prev = spsp->prev; // Last element in the original list
        
        for( size_t i = 0; i < n && top; i++ )
        {
            struct simplepost_serve * p = top;
            top = top->next;
            
            if( p->file ) free( p->file );
            if( p->uri ) free( p->uri );
            free( p );
        }
        
        prev->next = top;
    }
}

/*
Calculate the number of elements in the list (from the current element forward).

Arguments:
    spsp [in]    First element in the list to count

Return Value:
    The number of elements in the list will be returned.
*/
static size_t __simplepost_serve_length( struct simplepost_serve * spsp )
{
    size_t n = 0; // Number of elements in the list
    
    for( struct simplepost_serve * p = spsp; p != NULL; p = p->next ) n++;
    
    return n;
}

/*****************************************************************************
 *                Hypertext Transfer Protocol Implementation                 *
 *****************************************************************************/

/*
Inform the client that the request method has not been implemented.

Arguments:
    sock [in]   Socket connected to the client
*/
static void __http_unimplemented( int sock )
{
    char buffer[1024]; // Sent characters buffer
    int buffer_length; // Length of the buffer
    
    buffer_length = sprintf( buffer, "%s 501 Method Not Implemented\r\n", SP_HTTP_VERSION );
    send( sock, buffer, buffer_length, 0 );
    
    buffer_length = sprintf( buffer, "Server: %s/%s\r\n", SP_MAIN_SHORT_NAME, SP_MAIN_VERSION );
    send( sock, buffer, buffer_length, 0 );
    
    buffer_length = sprintf( buffer, "Content-Type: text/html\r\n" );
    send( sock, buffer, buffer_length, 0 );
    
    buffer_length = sprintf( buffer, "\r\n" );
    send( sock, buffer, buffer_length, 0 );
    
    buffer_length = sprintf( buffer, "<HTML><HEAD><TITLE>Method Not Implemented\r\n" );
    send( sock, buffer, buffer_length, 0 );
    
    buffer_length = sprintf( buffer, "</TITLE></HEAD>\r\n" );
    send( sock, buffer, buffer_length, 0 );
    
    buffer_length = sprintf( buffer, "<BODY><P>HTTP request method not supported.\r\n" );
    send( sock, buffer, buffer_length, 0 );
    
    buffer_length = sprintf( buffer, "</BODY></HTML>\r\n" );
    send( sock, buffer, buffer_length, 0 );
}

/*
Send the client a 404 error message.

Arguments:
    sock [in]   Socket connected to the client
*/
void __http_resource_not_found( int sock )
{
    char buffer[1024]; // Buffer of characters to send
    int buffer_length; // Length of the buffer
    
    buffer_length = sprintf( buffer, "%s 404 NOT FOUND\r\n", SP_HTTP_VERSION );
    send( sock, buffer, buffer_length, 0 );
    
    buffer_length = sprintf( buffer, "Server: %s/%s\r\n", SP_MAIN_SHORT_NAME, SP_MAIN_VERSION );
    send( sock, buffer, buffer_length, 0 );
    
    buffer_length = sprintf( buffer, "Content-Type: text/html\r\n" );
    send( sock, buffer, buffer_length, 0 );
    
    buffer_length = sprintf( buffer, "\r\n" );
    send( sock, buffer, buffer_length, 0 );
    
    buffer_length = sprintf( buffer, "<HTML><TITLE>Not Found</TITLE>\r\n" );
    send( sock, buffer, buffer_length, 0 );
    
    buffer_length = sprintf( buffer, "<BODY><P>The server could not fulfill\r\n" );
    send( sock, buffer, buffer_length, 0 );
    
    buffer_length = sprintf( buffer, "your request because the resource specified\r\n" );
    send( sock, buffer, buffer_length, 0 );
    
    buffer_length = sprintf( buffer, "is unavailable or nonexistent.\r\n" );
    send( sock, buffer, buffer_length, 0 );
    
    buffer_length = sprintf( buffer, "</BODY></HTML>\r\n" );
    send( sock, buffer, buffer_length, 0 );
}

/*
Send the client informational HTTP headers about a file.

Arguments:
    sock [in]   Socket connected to the client
    file [in]   Name and path of the file
*/
static void __http_send_headers( int sock, const char * file )
{
    char buffer[1024]; // Buffer of characters to send
    int buffer_length; // Length of the buffer
    magic_t hmagic; // Magic file handle
    
    buffer_length = sprintf( buffer, "%s 200 OK\r\n", SP_HTTP_VERSION );
    send( sock, buffer, buffer_length, 0 );
    
    buffer_length = sprintf( buffer, "Server: %s/%s\r\n", SP_MAIN_SHORT_NAME, SP_MAIN_VERSION );
    send( sock, buffer, buffer_length, 0 );
    
    hmagic = magic_open( MAGIC_MIME_TYPE );
    if( hmagic )
    {
        const char * mime_type = magic_file( hmagic, file );
        
        // According to RFC 2046, the content type should only be sent if it
        // can be determined. If not, the client should do its best to
        // determine what to do with the content instead. Notably, Apache used
        // to send application/octet-stream to indicate arbitrary binary data
        // when it couldn't determine the file type, but that is not correct
        // according to the specification.
        if( mime_type )
        {
            buffer_length = sprintf( buffer, "Content-Type: %s\r\n", mime_type );
            send( sock, buffer, buffer_length, 0 );
        }
        
        magic_close( hmagic );
    }
    
    buffer_length = sprintf( buffer, "\r\n" );
    send( sock, buffer, buffer_length, 0 );
}

/*
Send a file to the client.

Arguments:
    sock [in]   Socket connected to the client
    file [in]   Name and path of the file to serve
*/
static void __http_serve_file( int sock, const char * file )
{
    char * line; // Line received from the socket or file
    FILE * hfile; // File handle associated with the input file
    
    // Read and discard headers.
    while( __recv_line_dynamic( sock, &line ) ) free( line );
    
    hfile = fopen( file, "rb" );
    if( hfile == NULL )
    {
        __http_resource_not_found( sock );
    }
    else
    {
        char bytes[512]; // Data read from the file
        
        __http_send_headers( sock, file );
        
        fgets( bytes, sizeof( bytes ), hfile );
        while( feof( hfile ) == 0 )
        {
            send( sock, bytes, strlen( bytes ), 0 );
            fgets( bytes, sizeof( bytes ), hfile );
        }
        
        fclose( hfile );
    }
}

/*****************************************************************************
 *                            SimplePost Private                             *
 *****************************************************************************/

/*
SimplePost container for processing client requests
*/
struct simplepost_request
{
    struct simplepost * spp;    // SimplePost instance to act on
    int client_sock;            // Socket the client connected on
};

/*
SimplePost HTTP server status structure
*/
struct simplepost
{
    /* Initialization */
    int httpd;                          // Socket for the HTTP server
    unsigned short port;                // Port for the HTTP server
    char * address;                     // Address of the HTTP server
    pthread_t accept_thread;            // Handle of the primary thread
    pthread_mutex_t master_lock;        // Mutex for entire structure
    
    /* Files */
    struct simplepost_serve * files;    // List of files being served
    size_t files_count;                 // Number of files being served
    pthread_mutex_t files_lock;         // Mutex for files and files_count
    
    /* Clients */
    short accpeting_clients;            // Are we accepting client connections?
    size_t client_count;                // Number of clients currently being served
    pthread_mutex_t client_lock;        // Mutex for accepting_clients and client_count
    
    /* Errors */
    struct simplepost_error last_error; // Last error posted by one of our functions
    pthread_mutex_t error_lock;         // Mutex for last_error
};

/*
Set a new last error code.

Arguments:
    spp [in]    SimplePost instance to act on
    id [in]     Numeric error code
    msg [in]    Human readable message associated with the error code
                
                The message must either be NULL or a NULL-terminated string.
                If NULL, the default error message will be used; otherwise
                the string will be copied as the last error message.
                
                Optionally this string may contain embedded format specifiers
                which will be replaced by the values specified in subsequent
                additional arguments and formatted as requested. This function
                uses the same format specifications as printf(). See the
                printf() documentation for additional details.
*/
static void __set_last_error( simplepost_t spp, unsigned int id, const char * msg, ... )
{
    pthread_mutex_lock( &spp->error_lock );
    
    if( id > SP_ERROR_MAX ) spp->last_error.id = SP_ERROR_MAX;
    else spp->last_error.id = id;
    
    if( spp->last_error.msg != NULL ) free( spp->last_error.msg );
    
    if( msg == NULL )
    {
        spp->last_error.msg = NULL;
    }
    else
    {
        char buffer[4096]; // Buffer for message conversion
        va_list args; // Additional arguments passed to this function
        
        va_start( args, msg );
        if( vsprintf( buffer, msg, args ) <= 0 )
        {
            spp->last_error.msg = NULL;
        }
        else
        {
            spp->last_error.msg = (char *) malloc( sizeof( char ) * (strlen( buffer ) + 1) );
            strcpy( spp->last_error.msg, buffer );
        }
        va_end( args );
    }
    
    pthread_mutex_unlock( &spp->error_lock );
}

/*
Get the name and path of the file to serve from the given URI.

Remarks:
    This function compares the given URI to the list of files we are serving.
    The URI does not necessarily correspond one-to-one to an actual file on
    the filesystem, hence the need for this function.

Arguments:
    spp [in]    SimplePost instance to act on
    file [out]  Name and path of the file on the filesystem
                The storage for this string will be dynamically allocated. You
                are responsible for freeing it (unless it is NULL, in which
                case an error occurred).
    uri [in]    Uniform Resource Identifier to parse

Return Value:
    The number of characters written to the output string will be returned. If
    the return value is zero, either the URI does not specify a valid file, or
    another (more serious) error occurred.
*/
static size_t __get_filename_from_uri( simplepost_t spp, char ** file, const char * uri )
{
    size_t file_length = 0; // Length of the file name and path
    *file = NULL; // Failsafe
    
    pthread_mutex_lock( &spp->files_lock );
    if( spp->files )
    {
        for( struct simplepost_serve * p = spp->files; p != NULL; p = p->next )
        {
            if( strcmp( uri, p->uri ) == 0 )
            {
                *file = (char *) malloc( sizeof( char ) * (strlen( p->file ) + 1) );
                if( !*file )
                {
                    pthread_mutex_unlock( &spp->files_lock );
                    return file_length;
                }
                
                strcpy( *file, p->file );
                file_length = strlen( *file );
                
                break;
            }
        }
    }
    pthread_mutex_unlock( &spp->files_lock );
    
    return file_length;
}

/*
Process a request accepted by the server.

Arguments:
    p [in]      simplepost_request instance
                Ideally we would accept each variable in the simplepost_request
                struct as its own argument, but we are limited to a single
                void pointer for pthreads compatibility.

Return Value:
    We require a return value only for pthreads compatibility.
    NULL is always returned.
*/
static void * __process_request( void * p )
{
    struct simplepost_request * sprp = (struct simplepost_request *) p; // Convenience cast of our input parameter
    simplepost_t spp = sprp->spp; // SimplePost instance to act on
    int client_sock = sprp->client_sock; // Socket the client connected on
    
    free( sprp );
    sprp = p = NULL;
    
    char * request = NULL; // Client's request to process
    size_t request_length; // Length of the request
    const char * request_ptr; // Place in our request buffer
    
    char method[20]; // HTTP method of the request
    char * uri = NULL; // Uniform Resource Identifier of the request
    char version[60]; // HTTP version of the request
    char * part_ptr; // Place in the method, uri, or version buffer
    
    pthread_mutex_lock( &spp->client_lock );
    spp->client_count++;
    pthread_mutex_unlock( &spp->client_lock );
    
    request_length = __recv_line_dynamic( client_sock, &request );
    if( request_length == 0 )
    {
        __set_last_error( spp, SP_ERROR_RECVREQUEST, NULL );
        goto abort_request;
    }
    
    request_ptr = request;
    part_ptr = method;
    while( !(isspace( *request_ptr ) || *request_ptr == '\0') && part_ptr < (method + sizeof( method )/sizeof( method[0] ) - 1)) *part_ptr++ = *request_ptr++;
    *part_ptr = '\0';
    
    if( !isspace( *request_ptr++ ) )
    {
        __set_last_error( spp, SP_ERROR_NO_METHOD, NULL );
        goto abort_request;
    }
    
    part_ptr = uri = (char *) malloc( sizeof( char ) * (strlen( request_ptr ) + 1) );
    if( uri == NULL )
    {
        __set_last_error( spp, SP_ERROR_NO_URI, NULL );
        goto abort_request;
    }
    while( !(isspace( *request_ptr ) || *request_ptr == '\0') ) *part_ptr++ = *request_ptr++;
    *part_ptr = '\0';
    
    if( isspace( *request_ptr ) )
    {
        request_ptr++;
        part_ptr = version;
        while( !(isspace( *request_ptr ) || *request_ptr == '\0') && part_ptr < (version + sizeof( version )/sizeof( version[0] ) - 1)) *part_ptr++ = *request_ptr++;
        *part_ptr = '\0';
        
        if( *request_ptr != '\0' )
        {
            __set_last_error( spp, SP_ERROR_NO_VERSION, NULL );
            goto abort_request;
        }
    }
    else if( *request_ptr == '\0' )
    {
        strcpy( version, SP_HTTP_VERSION );
    }
    else
    {
        __set_last_error( spp, SP_ERROR_NO_VERSION, NULL );
        goto abort_request;
    }
    
    if( strcasecmp( method, "GET" ) == 0 )
    {
        char * file = NULL; // Name and path of the file to serve
        size_t file_length; // Length of the file name and path
        struct stat file_status; // File status
        
        file_length = __get_filename_from_uri( spp, &file, uri );
        if( file_length == 0 || stat( file, &file_status ) == -1 )
        {
            __http_resource_not_found( client_sock );
            __set_last_error( spp, SP_ERROR_RESOURCE_NOT_FOUND, "Resource not found: %s", uri );
            if( file ) free( file );
            goto abort_request;
        }
        
        if( S_ISDIR( file_status.st_mode ) )
        {
            const char * append_index = "/index.html";
            char * new_file = realloc( file, sizeof( char ) * (file_length + strlen( append_index ) + 1) );
            if( new_file )
            {
                file = new_file;
                strcat( file, append_index );
                if( stat( file, &file_status ) == -1 )
                {
                    __http_resource_not_found( client_sock );
                    __set_last_error( spp, SP_ERROR_RESOURCE_NOT_FOUND, "File not found: %s", file );
                    free( file );
                    goto abort_request;
                }
            }
        }
        
        if( S_ISDIR( file_status.st_mode ) )
        {
            __http_resource_not_found( client_sock );
            __set_last_error( spp, SP_ERROR_RESOURCE_NOT_FOUND, "Directory not supported: %s", file );
            free( file );
            goto abort_request;
        }
        
        // Executables generally indicate CGI. Although HTTP/1.1 doesn't
        // technically support CGI with GET requests, web servers sometimes
        // support it anyway. However our lack of CGI support is far from the
        // driving reason for not serving executable: it's a potential security
        // risk.
        //
        // TODO: Allow the user to explicitly override this restriction at his
        // own peril!
        if( (file_status.st_mode & S_IXUSR) || (file_status.st_mode & S_IXGRP) || (file_status.st_mode & S_IXOTH) )
        {
            __http_resource_not_found( client_sock );
            __set_last_error( spp, SP_ERROR_RESOURCE_NOT_FOUND, "Executables cannot be served: %s", file );
            free( file );
            goto abort_request;
        }
        
        __http_serve_file( client_sock, file );
        free( file );
    }
    else if( strcasecmp( method, "POST" ) == 0 )
    {
        // POST is primarily used for CGI, which we don't support. However
        // since it is such a common request type, it gets its own dedicated
        // method handler anyway.
        __http_unimplemented( client_sock );
        __set_last_error( spp, SP_ERROR_INVALID_METHOD, "POST is not a supported HTTP method" );
        goto abort_request;
    }
    else
    {
        __http_unimplemented( client_sock );
        __set_last_error( spp, SP_ERROR_INVALID_METHOD, "%s is not a supported HTTP method", method );
        goto abort_request;
    }
    
    abort_request:
    close( client_sock );
    
    if( request ) free( request );
    if( uri ) free( uri );
    
    pthread_mutex_lock( &spp->client_lock );
    spp->client_count--;
    pthread_mutex_unlock( &spp->client_lock );
    
    return NULL;
}

/*
Start accepting requests from clients.

Remarks:
    This function is the master thread that actually *runs* the server. It is
    responsible for accepting connections from clients and spawning a
    processing thread as quickly as possible to handle each client. It is not
    implemented in - or called directly from - simplepost_bind() so that the
    server does not block the caller of the aforementioned function.

Arguments:
    p [in]      SimplePost instance to act on

Return Value:
    We require a return value only for pthreads compatibility.
    NULL is always returned.
*/
static void * __accept_requests( void * p )
{
    simplepost_t spp = (simplepost_t) p; // Properly cast SimplePost handle
    
    int client_sock; // Socket the client connected on
    struct sockaddr_in client_name; // Name of the client
    socklen_t client_name_len; // Length of the client name structure
    pthread_t client_thread; // Handle of the client thread
    
    struct timespec timeout; // Maximum time to wait for a connection before checking the shutdown sentinel
    fd_set fds; // File descriptor set (for pselect() socket monitoring)
    
    spp->client_count = 0;
    spp->accpeting_clients = 1;
    
    while( spp->accpeting_clients )
    {
        FD_ZERO( &fds );
        FD_SET( spp->httpd, &fds );
        
        timeout.tv_sec = 2;
        timeout.tv_nsec = 0;
        
        switch( pselect( spp->httpd + 1, &fds, NULL, NULL, &timeout, NULL ) )
        {
            case -1:
                __set_last_error( spp, SP_ERROR_ACCEPT, NULL );
                spp->accpeting_clients = 0;
            case 0:
                continue;
        }
        
        client_name_len = sizeof( client_name );
        client_sock = accept( spp->httpd, (struct sockaddr *) &client_name, &client_name_len );
        if( client_sock == -1 ) continue;
        
        struct simplepost_request * sprp = (struct simplepost_request *) malloc( sizeof( struct simplepost_request ) );
        if( sprp == NULL ) continue;
        sprp->spp = spp;
        sprp->client_sock = client_sock;
        
        if( pthread_create( &client_thread, NULL, &__process_request, (void *) sprp ) == 0 )
        {
            pthread_detach( client_thread );
        }
        else
        {
            close( sprp->client_sock );
            free( sprp );
        }
    }
    
    while( spp->client_count ) usleep( SP_HTTP_SLEEP * 1000 );
    
    close( spp->httpd );
    spp->httpd = -1;
    
    return NULL;
}

/*****************************************************************************
 *                            SimplePost Public                              *
 *****************************************************************************/

/*
Initialize a new SimplePost instance.

Return Value:
    On success a pointer to the new instance will be returned.
    If we failed to allocate the requested memory, a NULL pointer will be returned.
*/
simplepost_t simplepost_init()
{
    simplepost_t spp = (simplepost_t) malloc( sizeof( struct simplepost ) );
    if( spp == NULL ) return NULL;
    
    spp->httpd = -1;
    spp->port = 0;
    spp->address = NULL;
    
    spp->files = NULL;
    spp->files_count = 0;
    
    spp->accpeting_clients = 0;
    spp->client_count = 0;
    
    spp->last_error.id = SP_ERROR_NONE;
    spp->last_error.msg = NULL;
    
    pthread_mutex_init( &spp->master_lock, NULL );
    pthread_mutex_init( &spp->files_lock, NULL );
    pthread_mutex_init( &spp->client_lock, NULL );
    pthread_mutex_init( &spp->error_lock, NULL );
    
    return spp;
}

/*
Free the given SimplePost instance.

Arguments:
    spp [in]    SimplePost instance to act on
*/
void simplepost_free( simplepost_t spp )
{
    if( spp->httpd != -1 ) simplepost_unbind( spp );
    if( spp->address ) free( spp->address );
    
    if( spp->files ) __simplepost_serve_free( spp->files );
    
    pthread_mutex_destroy( &spp->master_lock );
    pthread_mutex_destroy( &spp->files_lock );
    pthread_mutex_destroy( &spp->client_lock );
    pthread_mutex_destroy( &spp->error_lock );
    
    free( spp );
}

/*
Start the web server on the specified port.

Arguments:
    spp [in]        SimplePost instance to act on
    port [in]       Port to initialize the server on
                    If the port is 0, a port will be dynamically allocated.
    address [in]    Network address to bind the server to
                    If the address is NULL, the server will be bound to all
                    local interfaces (0.0.0.0 in netstat parlance).

Return Value:
    The port the server is bound to will be returned. If the return value is 0,
    an error occurred; consult simplepost_get_last_error().
*/
unsigned short simplepost_bind( simplepost_t spp, const char * address, unsigned short port )
{
    pthread_mutex_lock( &spp->master_lock );
    
    if( spp->httpd != -1 )
    {
        __set_last_error( spp, SP_ERROR_INITIALIZED, NULL );
        goto abort_unbound;
    }
    
    spp->httpd = socket( AF_INET, SOCK_STREAM, 0 );
    if( spp->httpd == -1 )
    {
        __set_last_error( spp, SP_ERROR_SOCKET, NULL );
        goto abort_unbound;
    }
    
    struct sockaddr_in name; // Address structure bound to the socket
    memset( &name, 0, sizeof( name ) );
    
    name.sin_family = AF_INET;
    name.sin_port = htons( port );
    
    if( address )
    {
        struct in_addr sin_addr; // Source address in network byte order
        
        if( inet_pton( AF_INET, address, (void *) &sin_addr ) != 1 )
        {
            __set_last_error( spp, SP_ERROR_ADDRESS, NULL );
            goto abort_bind;
        }
        name.sin_addr = sin_addr;
        
        if( spp->address ) free( spp->address );
        spp->address = (char *) malloc( sizeof( char ) * (strlen( address ) + 1) );
        if( spp->address == NULL )
        {
            __set_last_error( spp, SP_ERROR_ADDRESS, NULL );
            goto abort_bind;
        }
        strcpy( spp->address, address );
    }
    else
    {
        name.sin_addr.s_addr = htonl( INADDR_ANY );
        
        struct addrinfo hints; // Criteria for selecting socket addresses
        memset( (void *) &hints, 0, sizeof( hints ) );
        hints.ai_family = AF_INET; // Limit to IPv4
        
        struct addrinfo * address_info; // Address information for the local system
        if( getaddrinfo( NULL, NULL, &hints, &address_info ) == 0 )
        {
            if( spp->address ) free( spp->address );
            spp->address = (char *) malloc( sizeof( char ) * (strlen( (const char *) address_info->ai_addr ) + 1) );
            if( spp->address == NULL )
            {
                __set_last_error( spp, SP_ERROR_ADDRESS, NULL );
                goto abort_bind;
            }
            strcpy( spp->address, (const char *) address_info->ai_addr );
            freeaddrinfo( address_info );
        }
        else
        {
            if( spp->address ) free( spp->address );
            spp->address = (char *) malloc( sizeof( char ) * (strlen( "127.0.0.1" ) + 1) );
            if( spp->address == NULL )
            {
                __set_last_error( spp, SP_ERROR_ADDRESS, NULL );
                goto abort_bind;
            }
            strcpy( spp->address, "127.0.0.1" );
        }
    }
    
    if( bind( spp->httpd, (struct sockaddr *) &name, sizeof( name ) ) == -1 )
    {
        __set_last_error( spp, SP_ERROR_BIND, NULL );
        goto abort_bind;
    }
    
    if( port == 0 )
    {
        socklen_t name_len = sizeof( name ); // Length of the socket's address structure
        if( getsockname( spp->httpd, (struct sockaddr *) &name, &name_len ) == -1 )
        {
            __set_last_error( spp, SP_ERROR_PORTALLOC, NULL );
            goto abort_bind;
        }
        port = ntohs( name.sin_port );
    }
    spp->port = port;
    
    if( listen( spp->httpd, SP_HTTP_BACKLOG ) == -1 )
    {
        __set_last_error( spp, SP_ERROR_LISTEN, NULL );
        goto abort_bind;
    }
    
    if( pthread_create( &spp->accept_thread, NULL, &__accept_requests, (void *) spp ) != 0 )
    {
        __set_last_error( spp, SP_ERROR_ACCEPT, NULL );
        goto abort_bind;
    }
    
    pthread_mutex_unlock( &spp->master_lock );
    
    return port;
    
    abort_bind:
    close( spp->httpd );
    spp->httpd = -1;
    
    abort_unbound:
    pthread_mutex_unlock( &spp->master_lock );
    
    return 0;
}

/*
Shut down the web server.

Arguments:
    spp [in]    SimplePost instance to act on

Return Value:
    0   The server is not running or could not be shut down.
    1   The server has been successfully killed.
*/
short simplepost_unbind( simplepost_t spp )
{
    if( spp->httpd != -1 )
    {
        __set_last_error( spp, SP_ERROR_UNINITIALIZED, NULL );
        return 0;
    }
    
    pthread_mutex_lock( &spp->client_lock );
    spp->accpeting_clients = 0;
    pthread_mutex_unlock( &spp->client_lock );
    
    pthread_join( spp->accept_thread, NULL );
    
    return 1;
}

/*
Don't return until the server is shut down.

Arguments:
    spp [in]    SimplePost instance to act on
*/
void simplepost_block( simplepost_t spp )
{
    // NOTE: simplepost::httpd is atomic, so we don't need to worry about
    // acquiring simplepost::master_lock before reading it.
    while( spp->httpd != -1 ) usleep( SP_HTTP_SLEEP * 1000 );
}

/*
Is the server running?

Arguments:
    spp [in]    SimplePost instance to act on

Return Value:
    0   The server is not running.
    1   The server is alive!
*/
short simplepost_is_alive( simplepost_t spp )
{
    return (spp->httpd == -1) ? 0 : 1;
}

/*
Add a file to the list of files being served.

Arguments:
    spp [in]    SimplePost instance to act on
    url [out]   Address of the file being served
                
                Although you probably need this information, it is generated in
                a predictable manner. Therefore this argument is technically
                optional; you may safely make it NULL.
                
                The storage for this string will be dynamically allocated. You
                are responsible for freeing it (unless it is NULL, in which
                case an error occurred).
    file [in]   Name and path of the file to serve
    uri [in]    Uniform Resource Identifier of the file to serve
                
                This argument is completely optional. If it is NULL, the name
                of the file will be used. For example, if
                file = "/usr/bin/simplepost", the default uri (if uri = NULL)
                would be "/simplepost". If you specify a uri, it must not
                already be in use, and it must start with a "/". See the
                HTTP/1.1 specification (RFC 2616) for the requirements of valid
                URIs.
    count [in]  Number of times the file should be served
                If the count is zero, the number of times will be unlimited.

Return Value:
    The number of characters written to the url (excluding the NULL-terminating
    character) will be returned.
*/
size_t simplepost_serve_file( simplepost_t spp, char ** url, const char * file, const char * uri, unsigned int count )
{
    struct stat file_status; // Status of the input file
    struct simplepost_serve * this_file = NULL; // New file to serve
    size_t url_length = 0; // Length of the URL
    *url = NULL; // Failsafe
    
    pthread_mutex_lock( &spp->files_lock );
    
    if( file == NULL )
    {
        __set_last_error( spp, SP_ERROR_FILE_DOESNT_EXIST, NULL );
        goto abort_insert;
    }
    
    if( stat( file, &file_status ) == -1 )
    {
        __set_last_error( spp, SP_ERROR_FILE_DOESNT_EXIST, "Cannot add nonexistent file: %s", file );
        goto abort_insert;
    }
    
    if( !(S_ISREG( file_status.st_mode ) || S_ISLNK( file_status.st_mode )) )
    {
        __set_last_error( spp, SP_ERROR_FILE_DOESNT_EXIST, "File not supported: %s", file );
        goto abort_insert;
    }
    
    #if (SP_FILES_MAX > 0)
    if( spp->files_count == SP_FILES_MAX )
    {
        __set_last_error( spp, SP_ERROR_TOO_MANY_FILES, "Cannot serve more than %u files simultaneously", SP_FILES_MAX );
        goto abort_insert;
    }
    #endif
    
    if( spp->files )
    {
        this_file = spp->files = __simplepost_serve_init();
    }
    else
    {
        for( this_file = spp->files; this_file->next; this_file = this_file->next );
        this_file = __simplepost_serve_insert_after( this_file, NULL );
    }
    if( this_file == NULL ) goto cannot_insert_file;
    
    if( uri )
    {
        if( uri[0] != '/' || uri[0] == '\0' )
        {
            __set_last_error( spp, SP_ERROR_INVALID_URI, "Invalid URI: %s", uri );
            goto abort_insert;
        }
    }
    else
    {
        uri = file;
        if( strchr( uri, '/' ) )
        {
            while( *uri != '\0' ) uri++;
            while( *uri != '/' ) uri--;
        }
    }
    
    this_file->uri = (char *) malloc( sizeof( char ) * (strlen( uri ) + 1) );
    if( this_file->uri == NULL ) goto cannot_insert_file;
    if( uri[0] == '/' )
    {
        strcpy( this_file->uri, uri );
    }
    else
    {
        strcpy( this_file->uri, "/" );
        strcat( this_file->uri, uri );
    }
    
    for( struct simplepost_serve * p = spp->files; p != this_file; p = p->next )
    {
        if( strcmp( p->uri, uri ) == 0 )
        {
            __set_last_error( spp, SP_ERROR_URI_ALREADY_TAKEN, "URI already in use: %s", uri );
            goto abort_insert;
        }
    }
    
    if( url != NULL )
    {
        int url_length_2; // Length of the URL as reported by sprintf()
        
        *url = (char *) malloc( sizeof( char ) * (strlen( spp->address ) + strlen( uri ) + 50) );
        if( *url == NULL ) goto cannot_insert_file;
        
        if( spp->port == 80 ) url_length_2 = sprintf( *url, "http://%s%s", spp->address, this_file->uri );
        else url_length_2 = sprintf( *url, "http://%s:%u%s", spp->address, spp->port, this_file->uri );
        if( url_length_2 <= 0 ) goto cannot_insert_file;
        
        url_length = url_length_2;
    }
    
    this_file->file = (char *) malloc( sizeof( char ) * (strlen( file ) + 1) );
    if( this_file->file == NULL ) goto cannot_insert_file;
    strcpy( this_file->file, file );
    
    this_file->count = count;
    
    spp->files_count++;
    
    pthread_mutex_unlock( &spp->files_lock );
    
    return url_length;
    
    cannot_insert_file:
    __set_last_error( spp, SP_ERROR_FILE_INSERT_FAILED, "Cannot insert file: %s", file );
    
    abort_insert:
    if( this_file ) __simplepost_serve_remove( this_file, 1 );
    pthread_mutex_unlock( &spp->files_lock );
    
    return 0;
}

/*
Remove a file from the list of files being served.

Arguments:
    spp [in]    SimplePost instance to act on
    url [out]   Uniform Resource Identifier (URI) or Uniform Resource
                Locator (URL) of the file to remove

Return Value:
    0   The file was not being served.
    1   The file was successfully removed from the list.
*/
short simplepost_purge_file( simplepost_t spp, const char * uri )
{
    if( strncmp( "http://", uri, 7 ) == 0 )
    {
        unsigned short i = 0; // '/' count
        while( i < 3 )
        {
            if( *uri == '\0' ) return 0;
            if( *uri++ == '/' ) i++;
        }
    }
    
    pthread_mutex_lock( &spp->files_lock );
    for( struct simplepost_serve * p = spp->files; p; p = p->next )
    {
        if( p->uri && strcmp( p->uri, uri ) == 0 )
        {
            __simplepost_serve_remove( p, 1 );
            spp->files_count--;
            pthread_mutex_unlock( &spp->files_lock );
            return 1;
        }
    }
    pthread_mutex_unlock( &spp->files_lock );
    
    return 0;
}

/*
Initialize a new SimplePost File instance.

Return Value:
    On success a pointer to the new instance will be returned.
    If we failed to allocate the requested memory, a NULL pointer will be returned.
*/
simplepost_file_t simplepost_file_init()
{
    simplepost_file_t spfp = (simplepost_file_t) malloc( sizeof( struct simplepost_file ) );
    if( spfp == NULL ) return NULL;
    
    spfp->file = NULL;
    spfp->url = NULL;
    
    spfp->next = NULL;
    spfp->prev = NULL;
    
    return spfp;
}

/*
Free the given SimplePost File instance.

Arguments:
    spfp [in]   SimplePost File instance to act on
*/
void simplepost_file_free( simplepost_file_t spfp )
{
    if( spfp->prev ) spfp->prev->next = NULL;
    
    while( spfp )
    {
        simplepost_file_t p = spfp;
        spfp = spfp->next;
        
        if( p->file ) free( p->file );
        if( p->url ) free( p->url );
        free( p );
    }
}

/*
Get the address the server is bound to.

Arguments:
    spp [in]        SimplePost instance to act on
    address [out]   Address of the server
                    
                    The storage for this string will be dynamically allocated.
                    You are responsible for freeing it (unless it is NULL, in
                    which case an error occurred).

Return Value:
    The number of characters written to the address (excluding the NULL-
    terminating character) will be returned.
*/
size_t simplepost_get_address( simplepost_t spp, char ** address )
{
    size_t address_length = 0; // Length of the server address
    *address = NULL; // Failsafe
    
    if( spp->address == NULL || spp->httpd == -1 ) return 0;
    
    address_length = strlen( spp->address );
    *address = (char *) malloc( sizeof( char ) * (address_length + 1) );
    if( *address == NULL ) return 0;
    
    strcpy( *address, spp->address );
    
    return address_length;
}

/*
Get the port the server is listening on.

Arguments:
    spp [in]    SimplePost instance to act on

Return Value:
    The server's port number will be returned. If this number is zero, an error
    occurred or (more likely) the server is not running.
*/
unsigned short simplepost_get_port( simplepost_t spp )
{
    return spp->port;
}

/*
Get a list of the files currently being served.

Arguments:
    spp [in]    SimplePost instance to act on
    files [out] List of files we are currently hosting
                
                This pointer may be NULL.
                
                The storage for this string will be dynamically allocated. You
                are responsible for freeing it (unless it is NULL, in which
                case an error occurred).

Return Value:
    The number of files currently being served (or, more accurately, unique
    URIs) will be returned.
*/
size_t simplepost_get_files( simplepost_t spp, simplepost_file_t * files )
{
    simplepost_file_t tail; // Last file in the *files list
    size_t files_count = 0; // Number of unique URIs
    
    if( files == NULL ) return spp->files_count;
    
    pthread_mutex_lock( &spp->files_lock );
    for( struct simplepost_serve * p = spp->files; p; p = p->next )
    {
        if( *files == NULL )
        {
            tail = *files = simplepost_file_init();
            if( tail == NULL ) goto abort_count;
        }
        else
        {
            simplepost_file_t prev = tail;
            
            tail = simplepost_file_init();
            if( tail == NULL ) goto abort_count;
            
            tail->prev = prev;
            prev->next = tail;
        }
        
        if( p->file == NULL ) goto abort_count;
        tail->file = (char *) malloc( sizeof( char ) * (strlen( p->file ) + 1) );
        
        if( p->uri == NULL ) goto abort_count;
        tail->url = (char *) malloc( sizeof( char ) * (strlen( spp->address ) + strlen( p->uri ) + 50) );
        if( tail->url == NULL ) goto abort_count;
        if( spp->port == 80 ) sprintf( tail->url, "http://%s%s", spp->address, p->uri );
        else sprintf( tail->url, "http://%s:%u%s", spp->address, spp->port, p->uri );
        
        files_count++;
    }
    pthread_mutex_unlock( &spp->files_lock );
    
    return files_count;
    
    abort_count:
    if( *files )
    {
        simplepost_file_free( *files );
        *files = NULL;
    }
    pthread_mutex_unlock( &spp->files_lock );
    
    return 0;
}

/*
Get the last error posted by a simplepost_*() function.

Arguments:
    spp [in]        SimplePost instance to act on
    error_msg [in]  Human readable error message
                    If this parameter is NULL, the error code will be returned
                    with no message; otherwise the string pointed to by this
                    parameter (*error_msg) must be freed after this function
                    returns.

Return Value:
    The numeric code of the last error will be returned.
*/
unsigned int simplepost_get_last_error( simplepost_t spp, char ** error_msg )
{
    unsigned short id; // Numeric error code
    
    pthread_mutex_lock( &spp->error_lock );
    
    id = spp->last_error.id;
    
    if( error_msg )
    {
        if( spp->last_error.msg )
        {
            *error_msg = (char *) malloc( sizeof( char ) * (strlen( spp->last_error.msg ) + 1) );
            if( *error_msg ) strcpy( *error_msg, spp->last_error.msg );
        }
        else
        {
            *error_msg = (char *) malloc( sizeof( char ) * (strlen( simplepost_error_table[id] ) + 1) );
            if( *error_msg ) strcpy( *error_msg, simplepost_error_table[id] );
        }
    }
    
    pthread_mutex_unlock( &spp->error_lock );
    
    return id;
}
