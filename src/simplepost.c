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
#include "impact.h"
#include "config.h"

#include <arpa/inet.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <microhttpd.h>

/*
Server header strings
*/
#define SP_HTTP_HEADER_NAMESPACE        "SimplePost::HTTP"
#define SP_HTTP_HEADER_MICROHTTPD       "microhttpd"

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
 *                              HTTP Responses                               *
 *****************************************************************************/

/*
HTTP response strings
*/
#define SP_HTTP_RESPONSE_BAD_REQUEST "<html><head><title>Bad Request\r\n</title></head>\r\n<body><p>HTTP request method not supported.\r\n</body></html>\r\n"
#define SP_HTTP_RESPONSE_FORBIDDEN "<html><head><title>Forbidden\r\n</title></head>\r\n<body><p>The request CANNOT be fulfilled.\r\n</body></html>\r\n"
#define SP_HTTP_RESPONSE_NOT_FOUND "<html><head><title>Not Found\r\n</title></head>\r\n<body><p>There is no resource matching the specified URI.\r\n</body></html>\r\n"
#define SP_HTTP_RESPONSE_NOT_ACCEPTABLE "<html><head><title>Not Acceptable\r\n</title></head>\r\n<body><p>HTTP headers request a resource we cannot satisfy.\r\n</body></html>\r\n"
#define SP_HTTP_RESPONSE_GONE "<html><head><title>Not Available\r\n</title></head>\r\n<body><p>The requested resource is no longer available.\r\n</body></html>\r\n"
#define SP_HTTP_RESPONSE_UNSUPPORTED_MEDIA_TYPE "<html><head><title>Unsupported Media Type\r\n</title></head>\r\n<body><p>The requested resource is not valid for the requested method.\r\n</body></html>\r\n"
#define SP_HTTP_RESPONSE_INTERNAL_SERVER_ERROR "<html><head><title>Internal Server Error\r\n</title></head>\r\n<body><p>HTTP server encountered an unexpected condition which prevented it from fulfilling the request.\r\n</body></html>\r\n"
#define SP_HTTP_RESPONSE_NOT_IMPLEMENTED "<html><head><title>Method Not Implemented\r\n</title></head>\r\n<body><p>HTTP request method not supported.\r\n</body></html>\r\n"

/*
Prepare to send a response to the client from a data buffer.

Remarks:
    The data passed to this function MUST be available until the MHD_Response
    instance returned by this function has been destroyed. DO NOT DESTROY THE
    RESPONSE until AFTER libmicrohttpd has responded to the request!

Arguments:
    connection [in]     Connection identifying the client
    status_code [in]    HTTP status code to send
    size [in]           Size of the data array to send
    data [in]           Data to send

Return Value:
    A libmicrohttpd response instance will be returned if the specified data
    has been queued for transmission to the client. An error message will be
    printed and NULL will be returned if an error occurs.
*/
static struct MHD_Response * __response_prep_data( struct MHD_Connection * connection, unsigned int status_code, size_t size, void * data )
{
    struct MHD_Response * response; // Response to the request
    
    response = MHD_create_response_from_data( size, data, MHD_NO, MHD_NO );
    if( response == NULL )
    {
        impact_printf_debug( "%s:%d: %s: Failed to allocate memory for the HTTP response %u\n", __FILE__, __LINE__, SP_MAIN_HEADER_MEMORY_ALLOC, status_code );
        return NULL;
    }
    
    if( MHD_queue_response( connection, status_code, response ) == MHD_NO )
    {
        impact_printf_error( "%s: Request 0x%lx: Cannot queue response with status %u\n", SP_HTTP_HEADER_NAMESPACE, pthread_self(), status_code );
        MHD_destroy_response( response );
        return NULL;
    }
    
    return response;
}

/*
Prepare to send a response to the client from a file.

Remarks:
    According to the MHD_create_response_from_fd() documentation, the file
    descriptor this function opens for the sepecified file will be closed when
    the MHD_Response instance returned by this function is destroyed. DO NOT
    DESTROY THE RESPONSE until AFTER libmicrohttpd has responded to the request!

Arguments:
    connection [in]     Connection identifying the client
    status_code [in]    HTTP status code to send
    size [in]           Size of the data portion of the response
    file [in]           Name and path of the file to send

Return Value:
    A libmicrohttpd response instance will be returned if the specified file
    has been queued for transmission to the client. An error message will be
    printed and NULL will be returned if an error occurs.
*/
static struct MHD_Response * __response_prep_file( struct MHD_Connection * connection, unsigned int status_code, size_t size, const char * file )
{
    struct MHD_Response * response; // Response to the request
    int fd; // File descriptor
    
    fd = open( file, O_RDONLY );
    if( fd == -1 )
    {
        impact_printf_error( "%s: Request 0x%lx: Cannot open FILE %s for reading\n", SP_HTTP_HEADER_NAMESPACE, pthread_self(), file );
        return NULL;
    }
    
    response = MHD_create_response_from_fd( size, fd );
    if( response == NULL )
    {
        impact_printf_debug( "%s:%d: %s: Failed to allocate memory for the HTTP response %u\n", __FILE__, __LINE__, SP_MAIN_HEADER_MEMORY_ALLOC, status_code );
        close( fd );
        return NULL;
    }
    
    if( MHD_queue_response( connection, status_code, response ) == MHD_NO )
    {
        impact_printf_error( "%s: Request 0x%lx: Cannot queue FILE %s with status %u\n", SP_HTTP_HEADER_NAMESPACE, pthread_self(), file, status_code );
        close( fd );
        MHD_destroy_response( response );
        return NULL;
    }
    
    return response;
}

/*****************************************************************************
 *                            SimplePost Private                             *
 *****************************************************************************/

/*
SimplePost request status structure
*/
struct simplepost_state
{
    struct MHD_Response * response; // Response to the request
    
    char * file; // Name and path of the file to serve
    size_t file_length; // Length of the file name and path
    
    char * data; // Data to serve
    size_t data_length; // Length of the data
};

/*
SimplePost HTTP server status structure
*/
struct simplepost
{
    /* Initialization */
    struct MHD_Daemon * httpd;          // HTTP server instance
    unsigned short port;                // Port for the HTTP server
    char * address;                     // Address of the HTTP server
    pthread_mutex_t master_lock;        // Mutex for port and address
    
    /* Files */
    struct simplepost_serve * files;    // List of files being served
    size_t files_count;                 // Number of files being served
    pthread_mutex_t files_lock;         // Mutex for files and files_count
};

/*
Get the name and path of the file to serve from the given URI.

Remarks:
    This function compares the given URI to the list of files we are serving.
    The URI does not necessarily correspond one-to-one to an actual file on
    the filesystem, hence the need for this function.

Side Effects:
    The file count is taken into consideration by this function. It will be
    appropriately decremented if a file is found matching the URI and returned
    by this function. The file will also be removed from the list of files
    being served and the instance file count decremented if the file reaches
    the maximum allowable times it may be served.

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
        for( struct simplepost_serve * p = spp->files; p; p = p->next )
        {
            if( strcmp( uri, p->uri ) == 0 )
            {
                *file = (char *) malloc( sizeof( char ) * (strlen( p->file ) + 1) );
                if( *file == NULL ) goto error;
                
                strcpy( *file, p->file );
                file_length = strlen( *file );
                
                if( p->count > 0 && --p->count == 0 )
                {
                    impact_printf_debug( "%s: FILE %s has reached its COUNT and will be removed\n", SP_HTTP_HEADER_NAMESPACE, p->file );
                    __simplepost_serve_remove( p, 1 );
                }
                
                goto error;
            }
        }
    }
    
    error:
    pthread_mutex_unlock( &spp->files_lock );
    return file_length;
}

/*
Panic! Cleanup the SimplePost instance after libmicrohttpd encountered an
unrecoverable error condition.

Arguments:
    cls [in]    SimplePost instance to act on
    file [in]   C source file where the error occured
    line [in]   Line of the C source file where the error occured
    reason [in] Error message
*/
static void __panic( void * cls, const char * file, unsigned int line, const char * reason )
{
    simplepost_t spp = (simplepost_t) cls; // Instance to act on
    
    impact_printf_debug( "%s:%u: PANIC!\n", file, line );
    impact_printf_error( "%s: %s: Emergency Shutdown: %s\n", SP_HTTP_HEADER_NAMESPACE, SP_HTTP_HEADER_MICROHTTPD, reason );
    
    simplepost_unbind( spp );
}

/*
Print error messages from libmicrohttpd.

Arguments:
    cls [in]    SimplePost instance to act on
    format [in] Format string
    ap [in]     List of format arguments
*/
static void __log_microhttpd_messages( void * cls, const char * format, va_list ap )
{
    char buffer[2048]; // libmicrohttpd error message
    int length; // Number of characters written to the buffer
    
    length = vsprintf( buffer, format, ap );
    
    if( length < 0 ) impact_printf_debug( "%s:%d: vsprintf() encountered a serious error condition processing a libmicrohttpd error message\n", __FILE__, __LINE__ );
    else impact_printf_error( "%s: %s: %s\n", SP_HTTP_HEADER_NAMESPACE, SP_HTTP_HEADER_MICROHTTPD, buffer );
}

/*
Process a request accepted by the server.

Arguments:
    cls [in]                SimplePost instance to act on
    connection [in]         Connection handle
    uri [in]                Uniform Resource Identifier of the request
    method [in]             HTTP method
    version [in]            HTTP version
    data [in]               Data sent by the client (excluding HEADERS)
    data_size [in] [out]    Size of the client-provided data
                            
                            Initially this must be the size of the data
                            provided. This function will update it to the
                            number of bytes NOT processed.
    state [out]             Data preserved for future calls of this request

Return Value:
    MHD_NO will be returned if the socket must be closed due to a serious error
    we encountered while handling the request.
    MHD_YES will be returned if the connection was handled successfully.
*/
static int __process_request( void * cls, struct MHD_Connection * connection, const char * uri, const char * method, const char * version, const char * data, size_t * data_size, void ** state )
{
    simplepost_t spp = (simplepost_t) cls; // Instance to act on
    struct simplepost_state * spsp = NULL; // Request state
    
    impact_printf_debug( "%s: Request 0x%lx: method: %s\n", SP_HTTP_HEADER_NAMESPACE, pthread_self(), method );
    impact_printf_debug( "%s: Request 0x%lx: URI: %s\n", SP_HTTP_HEADER_NAMESPACE, pthread_self(), uri );
    impact_printf_debug( "%s: Request 0x%lx: version: %s\n", SP_HTTP_HEADER_NAMESPACE, pthread_self(), version );
    
    #ifdef DEBUG
    if( *state )
    {
        impact_printf_error( "%s: Request 0x%lx: Request should be stateless\n", SP_HTTP_HEADER_NAMESPACE, pthread_self() );
        goto finalize_request;
    }
    #endif // DEBUG
    
    spsp = (struct simplepost_state *) malloc( sizeof( struct simplepost_state ) );
    if( spsp == NULL )
    {
        impact_printf_debug( "%s: Request 0x%lx: %s: Failed to allocate memory for the request state\n", SP_HTTP_HEADER_NAMESPACE, pthread_self(), SP_MAIN_HEADER_MEMORY_ALLOC );
        goto finalize_request;
    }
    *state = (void *) spsp;
    
    // struct simplepost_state should probably gets its own initialization and
    // destruction methods. There is also probably a better way to implement
    // __response_prep_data() and __response_prep_file(), one that would allow
    // them to directly handle struct simplepost_state and alleviate this
    // function of more responsibility.
    //
    // I'm still undecided on exactly how much control this function should
    // delegate and how that interface should look. For now we are just going
    // to handle construction here and destruction in __finalize_request().
    spsp->response = NULL;
    spsp->file = NULL;
    spsp->file_length = 0;
    spsp->data = NULL;
    spsp->data_length = 0;
    
    // We really don't care what data the client sent us. Nothing handled by
    // SimplePost actually requires the client to send additional data.
    *data_size = 0;
    
    if( strcmp( method, MHD_HTTP_METHOD_GET ) == 0 )
    {
        struct stat file_status; // File status
        
        spsp->file_length = __get_filename_from_uri( spp, &spsp->file, uri );
        if( spsp->file_length == 0 || stat( spsp->file, &file_status ) == -1 )
        {
            impact_printf_error( "%s: Request 0x%lx: Resource not found: %s\n", SP_HTTP_HEADER_NAMESPACE, pthread_self(), uri );
            spsp->response = __response_prep_data( connection, MHD_HTTP_NOT_FOUND, strlen( SP_HTTP_RESPONSE_NOT_FOUND ), (void *) SP_HTTP_RESPONSE_NOT_FOUND );
            goto finalize_request;
        }
        
        if( S_ISDIR( file_status.st_mode ) )
        {
            const char * append_index = "/index.html";
            char * new_file = realloc( spsp->file, sizeof( char ) * (spsp->file_length + strlen( append_index ) + 1) );
            if( new_file )
            {
                spsp->file = new_file;
                strcat( spsp->file, append_index );
                if( stat( spsp->file, &file_status ) == -1 )
                {
                    impact_printf_debug( "%s: Request 0x%lx: File not found: %s\n", SP_HTTP_HEADER_NAMESPACE, pthread_self(), spsp->file );
                    spsp->response = __response_prep_data( connection, MHD_HTTP_NOT_FOUND, strlen( SP_HTTP_RESPONSE_NOT_FOUND ), (void *) SP_HTTP_RESPONSE_NOT_FOUND );
                    goto finalize_request;
                }
            }
        }
        
        if( S_ISDIR( file_status.st_mode ) )
        {
            impact_printf_error( "%s: Request 0x%lx: Directory not supported: %s\n", SP_HTTP_HEADER_NAMESPACE, pthread_self(), spsp->file );
            spsp->response = __response_prep_data( connection, MHD_HTTP_FORBIDDEN, strlen( SP_HTTP_RESPONSE_FORBIDDEN ), (void *) SP_HTTP_RESPONSE_FORBIDDEN );
            goto finalize_request;
        }
        
        // Executables generally indicate CGI. Although HTTP/1.1 doesn't technically
        // support CGI with GET requests, web servers sometimes support it anyway.
        // However our lack of CGI support is far from the driving reason for not
        // serving executable: it's a potential security risk.
        //
        // TODO: Allow the user to override this restriction at his own peril!
        if( (file_status.st_mode & S_IXUSR) || (file_status.st_mode & S_IXGRP) || (file_status.st_mode & S_IXOTH) )
        {
            impact_printf_error( "%s: Request 0x%lx: Executables cannot be served: %s\n", SP_HTTP_HEADER_NAMESPACE, pthread_self(), spsp->file );
            spsp->response = __response_prep_data( connection, MHD_HTTP_FORBIDDEN, strlen( SP_HTTP_RESPONSE_FORBIDDEN ), (void *) SP_HTTP_RESPONSE_FORBIDDEN );
            goto finalize_request;
        }
        
        impact_printf_debug( "%s: Request 0x%lx: Serving FILE %s\n", SP_HTTP_HEADER_NAMESPACE, pthread_self(), spsp->file );
        spsp->response = __response_prep_file( connection, MHD_HTTP_OK, file_status.st_size, spsp->file );
    }
    else
    {
        impact_printf_debug( "%s: Request 0x%lx: %s is not a supported HTTP method\n", SP_HTTP_HEADER_NAMESPACE, pthread_self(), method );
        spsp->response = __response_prep_data( connection, MHD_HTTP_BAD_REQUEST, strlen( SP_HTTP_RESPONSE_BAD_REQUEST ), (void *) SP_HTTP_RESPONSE_BAD_REQUEST );
    }
    
    finalize_request:
    if( spsp == NULL )
    {
        impact_printf_debug( "%s: Request 0x%lx: Prematurely terminating response ...\n", SP_HTTP_HEADER_NAMESPACE, pthread_self() );
        return MHD_NO;
    }
    
    if( spsp->response )
    {
        impact_printf_debug( "%s: Request 0x%lx: Sending response ...\n", SP_HTTP_HEADER_NAMESPACE, pthread_self() );
        return MHD_YES;
    }
    
    impact_printf_debug( "%s: Request 0x%lx: Terminating response ...\n", SP_HTTP_HEADER_NAMESPACE, pthread_self() );
    
    if( spsp->file ) free( spsp->file );
    if( spsp->data ) free( spsp->data );
    free( spsp );
    *state = spsp = NULL;
    
    return MHD_NO;
}

/*
Cleanup the resources allocated for the request.

Arguments:
    cls [in]        SimplePost instance to act on
    connection [in] Connection handle
    state [in]      Data preserved from the request handler
    toe [in]        Reason the request was terminated
*/
void __finalize_request( void * cls, struct MHD_Connection * connection, void ** state, enum MHD_RequestTerminationCode toe )
{
    struct simplepost_state * spsp = (struct simplepost_state *) *state; // Request to cleanup
    
    #ifdef DEBUG
    if( spsp == NULL )
    {
        impact_printf_debug( "%s: Request 0x%lx: Cannot cleanup stateless request\n", SP_HTTP_HEADER_NAMESPACE, pthread_self() );
        return;
    }
    #endif // DEBUG
    
    if( spsp->response ) MHD_destroy_response( spsp->response );
    else impact_printf_debug( "%s:%d: BUG! __process_request() should have returned MHD_NO if it failed to queue a response!\n", __FILE__, __LINE__ );
    
    #ifdef DEBUG
    if( spsp->file == NULL && spsp->file_length ) impact_printf_debug( "%s:%d: BUG! simplepost_state::file should NEVER be NULL while simplepost_state::file_length is non-zero\n", __FILE__, __LINE__ );
    #endif // DEBUG
    if( spsp->file ) free( spsp->file );
    
    #ifdef DEBUG
    if( spsp->data == NULL && spsp->data_length ) impact_printf_debug( "%s:%d: BUG! simplepost_state::data should NEVER be NULL while simplepost_state::data_length is non-zero\n", __FILE__, __LINE__ );
    #endif // DEBUG
    if( spsp->data ) free( spsp->data );
    
    #ifdef DEBUG
    impact_printf_debug( "%s: Request 0x%lx: ", SP_HTTP_HEADER_NAMESPACE, pthread_self() );
    switch( toe )
    {
        case MHD_REQUEST_TERMINATED_COMPLETED_OK:
            impact_printf_debug( "Successfully sent the response\n" );
            break;
        case MHD_REQUEST_TERMINATED_WITH_ERROR:
            impact_printf_debug( "Error handling the connection\n" );
            break;
        case MHD_REQUEST_TERMINATED_TIMEOUT_REACHED:
            impact_printf_debug( "No activity on the connection until the timeout was reached\n" );
            break;
        case MHD_REQUEST_TERMINATED_DAEMON_SHUTDOWN:
            impact_printf_debug( "Connection terminated because the server is shutting down\n" );
            break;
        case MHD_REQUEST_TERMINATED_READ_ERROR:
            impact_printf_debug( "Connection died because the client did not send the expected data\n" );
            break;
        case MHD_REQUEST_TERMINATED_CLIENT_ABORT:
            impact_printf_debug( "The client terminated the connection by closing the socket for writing (TCP half-closed)\n" );
            break;
        default:
            impact_printf_debug( "Invalid Termination Code: %d\n", toe );
            break;
    }
    #endif // DEBUG
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
    
    spp->httpd = NULL;
    spp->port = 0;
    spp->address = NULL;
    
    spp->files = NULL;
    spp->files_count = 0;
    
    pthread_mutex_init( &spp->master_lock, NULL );
    pthread_mutex_init( &spp->files_lock, NULL );
    
    return spp;
}

/*
Free the given SimplePost instance.

Arguments:
    spp [in]    SimplePost instance to act on
*/
void simplepost_free( simplepost_t spp )
{
    if( spp->httpd ) simplepost_unbind( spp );
    if( spp->address ) free( spp->address );
    
    if( spp->files ) __simplepost_serve_free( spp->files );
    
    pthread_mutex_destroy( &spp->master_lock );
    pthread_mutex_destroy( &spp->files_lock );
    
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
    
    if( spp->httpd )
    {
        impact_printf_error( "%s: Server is already initialized\n", SP_HTTP_HEADER_NAMESPACE );
        goto error;
    }
    
    struct sockaddr_in source; // Source address and port the server should be bound to
    memset( &source, 0, sizeof( source ) );
    
    source.sin_family = AF_INET;
    source.sin_port = htons( port );
    
    if( address )
    {
        struct in_addr sin_addr; // Source address in network byte order
        
        if( inet_pton( AF_INET, address, (void *) &sin_addr ) != 1 )
        {
            impact_printf_error( "%s: Invalid source address specified\n", SP_HTTP_HEADER_NAMESPACE );
            goto error;
        }
        source.sin_addr = sin_addr;
        
        if( spp->address ) free( spp->address );
        spp->address = (char *) malloc( sizeof( char ) * (strlen( address ) + 1) );
        if( spp->address == NULL )
        {
            impact_printf_error( "%s: %s: Failed to allocate memory for the source address\n", SP_HTTP_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC );
            goto error;
        }
        strcpy( spp->address, address );
    }
    else
    {
        source.sin_addr.s_addr = htonl( INADDR_ANY );
        
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
                impact_printf_error( "%s: %s: Failed to allocate memory for the source address\n", SP_HTTP_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC );
                goto error;
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
                impact_printf_error( "%s: %s: Failed to allocate memory for the source address\n", SP_HTTP_HEADER_NAMESPACE , SP_MAIN_HEADER_MEMORY_ALLOC );
                goto error;
            }
            strcpy( spp->address, "127.0.0.1" );
        }
    }
    
    MHD_set_panic_func( &__panic, (void *) spp );
    spp->httpd = MHD_start_daemon( MHD_USE_THREAD_PER_CONNECTION, port,
        NULL, NULL,
        &__process_request, (void *) spp,
        MHD_OPTION_NOTIFY_COMPLETED, &__finalize_request, (void *) spp,
        MHD_OPTION_CONNECTION_LIMIT, SP_HTTP_BACKLOG,
        MHD_OPTION_SOCK_ADDR, &source,
        MHD_OPTION_EXTERNAL_LOGGER, &__log_microhttpd_messages, (void *) spp,
        MHD_OPTION_END );
    if( spp->httpd == NULL )
    {
        impact_printf_error( "%s: Failed to initialize the server on port %u\n", SP_HTTP_HEADER_NAMESPACE, port );
        goto error;
    }
    
    if( port == 0 )
    {
        socklen_t source_len = sizeof( source ); // Length of the socket's source address
        const union MHD_DaemonInfo * httpd_sock; // Socket the server is listening on
        
        httpd_sock = MHD_get_daemon_info( spp->httpd, MHD_DAEMON_INFO_LISTEN_FD );
        if( httpd_sock == NULL )
        {
            impact_printf_error( "%s: Failed to lock the socket the server is listening on\n", SP_HTTP_HEADER_NAMESPACE );
            goto error;
        }
        
        if( getsockname( httpd_sock->listen_fd, (struct sockaddr *) &source, &source_len ) == -1 )
        {
            impact_printf_error( "%s: Port could not be allocated\n", SP_HTTP_HEADER_NAMESPACE );
            goto error;
        }
        
        port = ntohs( source.sin_port );
    }
    spp->port = port;
    
    impact_printf_standard( "%s: Bound HTTP server to ADDRESS %s listening on PORT %u with PID %d\n", SP_HTTP_HEADER_NAMESPACE, spp->address, spp->port, getpid() );
    pthread_mutex_unlock( &spp->master_lock );
    
    return port;
    
    error:
    if( spp->httpd )
    {
        MHD_stop_daemon( spp->httpd );
        spp->httpd = NULL;
    }
    
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
    if( spp->httpd == NULL )
    {
        impact_printf_error( "%s: Server is not running\n", SP_HTTP_HEADER_NAMESPACE );
        return 0;
    }
    
    #ifdef DEBUG
    uintptr_t server_id = (uintptr_t) spp->httpd;
    #endif // DEBUG
    
    impact_printf_standard( "%s: Shutting down ...\n", SP_HTTP_HEADER_NAMESPACE );
    MHD_stop_daemon( spp->httpd );
    spp->httpd = NULL;
    
    impact_printf_debug( "%s: 0x%lx cleanup complete\n", SP_HTTP_HEADER_NAMESPACE, server_id );
    
    return 1;
}

/*
Don't return until the server is shut down.

Arguments:
    spp [in]    SimplePost instance to act on
*/
void simplepost_block( simplepost_t spp )
{
    while( spp->httpd ) usleep( SP_HTTP_SLEEP * 1000 );
}

/*
Don't return until the server has no more files to serve.

Arguments:
    spp [in]    SimplePost instance to act on
*/
void simplepost_block_files( simplepost_t spp )
{
    while( spp->files_count > 0 ) usleep( SP_HTTP_SLEEP * 1000 );
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
    return (spp->httpd == NULL) ? 0 : 1;
}

/*
Add a file to the list of files being served.

Remarks:
    If and only if url != NULL the final status of this operation will be
    printed to STDOUT upon successful completion.

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
    if( url ) *url = NULL; // Failsafe
    
    pthread_mutex_lock( &spp->files_lock );
    
    if( file == NULL )
    {
        impact_printf_debug( "%s:%d: simplepost_serve_file() requires a FILE\n", __FILE__, __LINE__ );
        goto abort_insert;
    }
    
    if( stat( file, &file_status ) == -1 )
    {
        impact_printf_error( "%s: Cannot serve nonexistent FILE: %s\n", SP_HTTP_HEADER_NAMESPACE, file );
        goto abort_insert;
    }
    
    if( !(S_ISREG( file_status.st_mode ) || S_ISLNK( file_status.st_mode )) )
    {
        impact_printf_error( "%s: FILE not supported: %s\n", SP_HTTP_HEADER_NAMESPACE, file );
        goto abort_insert;
    }
    
    #if (SP_HTTP_FILES_MAX > 0)
    if( spp->files_count == SP_HTTP_FILES_MAX )
    {
        impact_printf_error( "%s: Cannot serve more than %u files simultaneously\n", SP_HTTP_HEADER_NAMESPACE, SP_HTTP_FILES_MAX );
        goto abort_insert;
    }
    #endif
    
    if( spp->files )
    {
        for( this_file = spp->files; this_file->next; this_file = this_file->next );
        this_file = __simplepost_serve_insert_after( this_file, NULL );
    }
    else
    {
        this_file = spp->files = __simplepost_serve_init();
    }
    if( this_file == NULL ) goto cannot_insert_file;
    
    if( uri )
    {
        if( uri[0] != '/' || uri[0] == '\0' )
        {
            impact_printf_error( "%s: Invalid URI: %s\n", SP_HTTP_HEADER_NAMESPACE, uri );
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
            impact_printf_error( "%s: URI already in use: %s\n", SP_HTTP_HEADER_NAMESPACE, uri );
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
    
    if( url_length )
    {
        switch( count )
        {
            case 0:
                impact_printf_standard( "%s: Serving %s on %s indefinitely\n", SP_HTTP_HEADER_NAMESPACE, file, *url );
                break;
            case 1:
                impact_printf_standard( "%s: Serving %s on %s exactly once\n", SP_HTTP_HEADER_NAMESPACE, file, *url );
                break;
            default:
                impact_printf_standard( "%s: Serving %s on %s %u times\n", SP_HTTP_HEADER_NAMESPACE, file, *url, count );
                break;
        }
    }
    
    return url_length;
    
    cannot_insert_file:
    impact_printf_error( "%s: Cannot insert FILE: %s\n", SP_HTTP_HEADER_NAMESPACE, file );
    
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
    -1  Internal error
    0   The file was not being served.
    1   The file was successfully removed from the list.
*/
short simplepost_purge_file( simplepost_t spp, const char * uri )
{
    if( uri == NULL )
    {
        impact_printf_debug( "%s:%d: simplepost_purge_file() requires a URI\n", __FILE__, __LINE__ );
        return -1;
    }
    
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
            impact_printf_standard( "%s: Removing URI %s from service ...\n", SP_HTTP_HEADER_NAMESPACE, uri );
            
            __simplepost_serve_remove( p, 1 );
            spp->files_count--;
            
            pthread_mutex_unlock( &spp->files_lock );
            return 1;
        }
    }
    pthread_mutex_unlock( &spp->files_lock );
    
    impact_printf_error( "%s: Cannot purge nonexistent URI %s\n", SP_HTTP_HEADER_NAMESPACE, uri );
    
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
    
    if( spp->address == NULL || spp->httpd == NULL ) return 0;
    
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
