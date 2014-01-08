/*
SimplePost - A Basic, Embedded HTTP Server

Copyright (C) 2012-2013 xorangekiller.  All rights reserved.

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
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

/***********************************************************************
                     Independent Support Structures
***********************************************************************/

/*
This basic structure is necessary for passing multiple values to SimplePostProcessRequestThread() via pthread_create().
*/
struct SimplePostProcessRequestType
{
    SimplePost * simple_ptr; // Instance of the class this thread is associated with
    int client_sock; // Socket the client connected on
};

/***********************************************************************
                     Independent Support Functions
***********************************************************************/

/*
Call the SimplePost::Accept() instance associated with simple_ptr.
*/
void * SimplePostAcceptThread( void * ptr )
{
    SimplePost * simple_ptr = (SimplePost *) ptr; // SimplePost instance that called this function
    simple_ptr->Accept();
    return NULL;
}

/*
Call the SimplePost::ProcessRequest() instance associated with simple_ptr.
*/
void * SimplePostProcessRequestThread( void * ptr )
{
    SimplePostProcessRequestType * process_request_ptr = (SimplePostProcessRequestType *) ptr; // Input data cast
    process_request_ptr->simple_ptr->ProcessRequest( process_request_ptr->client_sock );
    delete process_request_ptr;
    return NULL;
}

/***********************************************************************
                      SimpleExcept Implementation
***********************************************************************/

/*
Initialize the exception class with an ID.

Remarks:
    If this ID is not valid, the message will be NULL.

Arguments:
    id [in]         Identification number of the exception
                    This should be a number between SEID_NONE and
                    SEID_MAX. See the definitions for a list of valid
                    exceptions.
*/
SimpleExcept::SimpleExcept( const unsigned int & id ) throw()
{
    this->id = id;
    this->message = this->CreateMessage( id );
}

/*
Initialize the exception class with a custom message.

Arguments:
    message [in]    Message to be passed as an exception
                    This string MUST be NULL terminated!
*/
SimpleExcept::SimpleExcept( const char * message ) throw()
{
    this->id = SEID_CUSTOM;
    this->message = new char[strlen( message ) + 1];
    strcpy( this->message, message );
}

/*
Construct the exception using the information from another.

Arguments:
    e [in]          Existing instance to base this instance on
*/
SimpleExcept::SimpleExcept( const SimpleExcept & e ) throw()
{
    this->id = e.id;
    if( e.message == NULL )
    {
        this->message = NULL;
    }
    else
    {
        this->message = new char[strlen( e.message ) + 1];
        strcpy( this->message, e.message );
    };
}

/*
Deallocate the message string, if necessary.
*/
SimpleExcept::~SimpleExcept() throw()
{
    if( this->message ) delete [] this->message;
}

/*
Return the identification number.
*/
unsigned int SimpleExcept::GetID() const throw()
{
    return this->id;
}

/*
Return the explanation.
*/
const char * SimpleExcept::what() const throw()
{
    return this->message;
}

/*
Return the explanation.
*/
const char * SimpleExcept::GetMessage() const throw()
{
    return this->message;
}

/*
Check if two instances of this class contain the same exception.

Arguments:
    e [in]          Instance to compare against

Return Value:
    false   The instances are not equal.
    true    Both exceptions are equivalent.
*/
bool SimpleExcept::operator==( const SimpleExcept & e ) const throw()
{
    if( this->id != e.id ) return false;
    if( this->message == NULL && e.message != NULL ) return false;
    if( this->message != NULL && e.message == NULL ) return false;
    if( this->id == SEID_CUSTOM && strcmp( this->message, e.message ) != 0 ) return false;
    return true;
}

/*
Compare the exception against an identification number.

Remarks:
    This check is not as thorough as comparing against another instance
    of this class because it cannot handle custom exception messages.

Arguments:
    id [in]         Identification number to compare against

Return Value:
    false   The IDs are not equal.
    true    The IDs are equal.
*/
bool SimpleExcept::operator==( const unsigned int & id ) const throw()
{
    return (this->id == id);
}

/*
Translate the ID into an exception message.

Remarks:
    It is the responsibility of the caller to delete any string returned
    by this class.

Arguments:
    id [in]         Identification number of the exception

Return Value:
    If the ID is out of range, NULL will be returned; otherwise a string
    containing the message will be returned.
*/
char * SimpleExcept::CreateMessage( const unsigned int & id ) throw()
{
    const char * static_message; // Temporary message string
    char * message; // Message string to return
    switch( id )
    {
        case SEID_INITIALIZED:
            static_message = "This SimplePost instance is already initialized!";
            break;
        case SEID_SOCKET:
            static_message = "SimplePost failed to allocate a socket!";
            break;
        case SEID_BIND:
            static_message = "SimplePost failed to bind!";
            break;
        case SEID_ADDRESS:
            static_message = "SimplePost failed to bind to an invalid source address!";
            break;
        case SEID_PORTALLOC:
            static_message = "SimplePost failed to allocate a port!";
            break;
        case SEID_LISTEN:
            static_message = "SimplePost cannot listen!";
            break;
        case SEID_UNINITIALIZED:
            static_message = "This SimplePost instance has not been initialized!";
            break;
        case SEID_ACCEPT:
            static_message = "SimplePost cannot accept connections!";
            break;
        default:
            static_message = NULL;
            break;
    };
    if( static_message )
    {
        message = new char[strlen( static_message ) + 1];
        strcpy( message, static_message );
    }
    else
    {
        message = NULL;
    };
    return message;
}

/***********************************************************************
                       SimplePost Implementation
***********************************************************************/

/*
Initialize the class but NOT the server.
*/
SimplePost::SimplePost()
{
    this->httpd = -1;
    this->count = 0;
    this->trigger = 0;
    pthread_mutex_init( &this->transtex, NULL );
    pthread_mutex_init( &this->clienttex, NULL );
}

/*
Deconstruct the class, shutting down the server if necessary.
*/
SimplePost::~SimplePost()
{
    if( this->httpd != -1 ) this->Kill();
    pthread_mutex_destroy( &this->clienttex );
    pthread_mutex_destroy( &this->transtex );
}

/*
Start the web server on the specified port.

Remarks:


Arguments:
    port [in]               Port to initialize the server on
                            If the port is 0, a port will be dynamically
                            allocated and assigned to this variable.
    address [in] [optional] Network address to bind the server to
                            If the address is NULL, the server will be
                            bound to all local interfaces (default).

*/
void SimplePost::Init( unsigned short * port, const char * address )
{
    struct sockaddr_in name; // Address structure bound to the socket

    if( this->httpd != -1 ) throw SimpleExcept( SEID_INITIALIZED );
    this->httpd = socket( AF_INET, SOCK_STREAM, 0 );
    if( this->httpd == -1 ) throw SimpleExcept( SEID_SOCKET );

    memset( &name, 0, sizeof( name ) );
    name.sin_family = AF_INET;
    name.sin_port = htons( *port );
    if( address )
    {
        struct in_addr sin_addr; // Source address in network byte order
        if( inet_pton( AF_INET, address, (void *) &sin_addr ) != 1 )
        {
            close( this->httpd );
            this->httpd = -1;
            throw SimpleExcept( SEID_ADDRESS );
        };
        name.sin_addr = sin_addr;
        this->address = address;
    }
    else
    {
        name.sin_addr.s_addr = htonl( INADDR_ANY );
        this->address = "127.0.0.1";
    };

    if( bind( this->httpd, (struct sockaddr *) &name, sizeof( name ) ) == -1 )
    {
        close( this->httpd );
        this->httpd = -1;
        throw SimpleExcept( SEID_BIND );
    };
    if( *port == 0 )
    {
        socklen_t name_len = sizeof( name ); // Length of the socket's address structure
        if( getsockname( this->httpd, (struct sockaddr *) &name, &name_len ) == -1 )
        {
            close( this->httpd );
            this->httpd = -1;
            throw SimpleExcept( SEID_PORTALLOC );
        };
        *port = ntohs( name.sin_port );
    };
    this->port = *port;
}

/*
Start the web server on the specified port.

Remarks:
    The arguments for this function are the same as for the other
    overloaded version of this function, except a reassigned port will
    not be announced. Use this version only if you are using a static
    port!
*/
void SimplePost::Init( unsigned short port, const char * address )
{
    this->Init( &port, address );
}

/*
Add a file to the list of files being served.

Exceptions:
    This function does not throw.

Arguments:
    url [out]       Address of the file being served
                    Although you probably need this information, it is
                    generated in a predictable manner. Therefore this
                    argument is technically optional.
    size [in]       Size of the array pointed to by url
                    If url is specified, this argument must be also.
                    Failure to do so correctly could lead to security
                    and stability problems.
    filename [in]   Name (and path) of the file to serve
    count [in]      Number of times the file should be served
                    If this argument is zero, the number of times will
                    be unlimited.

Return Value:
    The number of characters written to the url array will be returned
    upon success unless the url is NULL, in which case 0 will be
    returned to indicate success.
    -1      The filename does not exist.
    -2      The server has hit the maximum number of files to serve.
    -3      The buffer is too small to hold the URL.
*/
int SimplePost::Serve( char * url, unsigned int size, const char * filename, unsigned int count )
{
    struct stat st; // Status structure for calls to stat()
    const char * bare_filename; // filename without its base path

    if( this->httpd == -1 ) throw SimpleExcept( SEID_UNINITIALIZED );

    pthread_mutex_lock( &this->transtex );

    if( filename == NULL || stat( filename, &st ) == -1 || !(S_ISREG( st.st_mode ) || S_ISLNK( st.st_mode )) )
    {
        pthread_mutex_unlock( &this->transtex );
        return -1;
    };
    if( this->count == SP_SERV_MAX )
    {
        pthread_mutex_unlock( &this->transtex );
        return -2;
    };

    while( this->trans[trigger].url.empty() == false ) if( ++this->trigger == SP_SERV_MAX ) this->trigger = 0;
    this->trans[this->trigger].filename = filename;
    this->trans[this->trigger].url = "/";
    bare_filename = filename;
    for( const char * it = filename; *it != '\0'; it++ ) if( *it == '/' ) bare_filename = filename + 1;
    this->trans[this->trigger].url += bare_filename;
    this->trans[this->trigger].count = count;
    this->count++;

    if( url )
    {
        char * buf = new char[this->address.size() + 50]; // Buffer for the url
        sprintf( buf, "http://%s:%u", this->address.c_str(), this->port );
        if( size <= (strlen( buf ) + this->trans[this->trigger].url.size()) )
        {
            this->trans[this->trigger].url.clear();
            if( this->trigger == 0 ) this->trigger = SP_SERV_MAX - 1;
            else this->trigger--;
            this->count--;
            delete [] buf;

            pthread_mutex_unlock( &this->transtex );
            return -3;
        };
        strcpy( url, buf );
        strcat( url, this->trans[this->trigger].url.c_str() );
        size = strlen( buf ) + this->trans[this->trigger].url.size();
        delete [] buf;
    }
    else
    {
        size = 0;
    };

    pthread_mutex_unlock( &this->transtex );
    return size;
}

/*
Start listening for connections from clients.

Remarks:
    The server MUST be intialized and have at least one file to serve
    BEFORE this function is called!
*/
void SimplePost::Run()
{
    if( this->httpd == -1 ) throw SimpleExcept( SEID_UNINITIALIZED );
    if( listen( this->httpd, SP_BACKLOG ) == -1 )
    {
        close( this->httpd );
        this->httpd = -1;
        throw SimpleExcept( SEID_LISTEN );
    };
    if( pthread_create( &this->accept_thread, NULL, &SimplePostAcceptThread, (void *) this ) != 0 )
    {
        close( this->httpd );
        this->httpd = -1;
        throw SimpleExcept( SEID_ACCEPT );
    };
}

/*
Don't return until the server is shut down.

Exceptions:
    This function does not throw.
*/
void SimplePost::Block()
{
    while( this->httpd != -1 ) usleep( SP_SLEEP * 1000 );
}

/*
Shut down the server.
*/
void SimplePost::Kill()
{
    if( this->httpd == -1 ) throw SimpleExcept( SEID_UNINITIALIZED );
    pthread_mutex_lock( &this->transtex );
    if( this->count )
    {
        this->count = 0;
        for( unsigned int i = 0; i < SP_SERV_MAX; i++ ) if( this->trans->url.empty() == false ) this->trans->url.clear();
    };
    pthread_mutex_unlock( &this->transtex );
    pthread_join( this->accept_thread, NULL );
}

/*
Is the server running?

Exceptions:
    This function does not throw.
*/
bool SimplePost::Running() const
{
    return (this->httpd != -1);
}

/*
Send a file to the client.

Arguments:
    client [in]     Socket connected to the client
    filename [in]   Name (and path) of the file to serve
*/
void SimplePost::ServeFile( const int & client, const char * filename )
{
    char line[1024]; // Line received from the socket or file
    int length; // Number of characters received in the line
    FILE * fh; // File handle associated with the input file

    // Put SOMETHING in the buffer so the loop below runs at least once.
    line[0] = 'A';
    line[1] = '\0';
    length = 1;

    // Read and discard headers the same way as SimplePost::ProcessRequest()
    while( (length > 0) && strcmp( "\n", line ) ) length = SimplePost::GetLine( client, line, sizeof( line ) );

    fh = fopen( filename, "r" );
    if( fh == NULL )
    {
        SimplePost::NotFound( client );
    }
    else
    {
        SimplePost::Headers( client, filename );
        fgets( line, sizeof( line ), fh );
        while( feof( fh ) == 0 )
        {
            send( client, line, strlen( line ), 0 );
            fgets( line, sizeof( line ), fh );
        };
    };
    fclose( fh );
}

/*
Get a line of text from a socket.

Remarks:
    This function handles strings that end in LF, CR, or CRLF.

Arguments:
    sock [in]       Socket to retrieve the characters from
    buf [out]       Buffer that the NULL-terminated string will be written to
                    If the end of the buffer is encountered before a newline,
                    the buffer will be terminated with a NULL character.
    size [in]       Length of the buffer, above

Return Value:
    The number of bytes written to the buffer, excluding the NULL character.
*/
int SimplePost::GetLine( const int & sock, char * buf, const int & size )
{
    int i = 0; // Iterator for the buffer
    char c = '\0'; // Character reveived from the socket
    int n; // Return value from the recv()

    while( (i < size - 1) && (c != '\n') )
    {
        n = recv( sock, &c, 1, 0 );
        if( n > 0 )
        {
           if( c == '\r' )
           {
               n = recv( sock, &c, 1, MSG_PEEK );
               if( (n > 0) && (c == '\n') ) recv( sock, &c, 1, 0 );
               else c = '\n';
           };
           buf[i] = c;
           i++;
        }
        else
        {
            c = '\n';
        };
    };
    buf[i] = '\0';

    return i;
}

/*
Inform the client that the request method has not been implemented.

Arguments:
    client [in]     Socket connected to the client
*/
void SimplePost::UnImplemented( const int & client )
{
    char buf[1024]; // Sent characters buffer
    sprintf( buf, "HTTP/1.1 501 Method Not Implemented\r\n" );
    send( client, buf, strlen( buf ), 0 );
    sprintf( buf, "Server: %s/%s\r\n", SP_IDENT, SP_VER );
    send( client, buf, strlen( buf ), 0 );
    sprintf( buf, "Content-Type: text/html\r\n" );
    send( client, buf, strlen( buf ), 0 );
    sprintf( buf, "\r\n" );
    send( client, buf, strlen( buf ), 0 );
    sprintf( buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n" );
    send( client, buf, strlen( buf ), 0 );
    sprintf( buf, "</TITLE></HEAD>\r\n" );
    send( client, buf, strlen( buf ), 0 );
    sprintf( buf, "<BODY><P>HTTP request method not supported.\r\n" );
    send( client, buf, strlen( buf ), 0 );
    sprintf( buf, "</BODY></HTML>\r\n" );
    send( client, buf, strlen( buf ), 0 );
}

/*
Send the client a 404 error message.

Arguments:
    client [in]     Socket connected to the client
*/
void SimplePost::NotFound( const int & client )
{
    char buf[1024]; // Sent characters buffer
    sprintf( buf, "HTTP/1.1 404 NOT FOUND\r\n" );
    send( client, buf, strlen( buf ), 0 );
    sprintf( buf, "Server: %s/%s\r\n", SP_IDENT, SP_VER );
    send( client, buf, strlen( buf ), 0 );
    sprintf( buf, "Content-Type: text/html\r\n" );
    send( client, buf, strlen( buf ), 0 );
    sprintf( buf, "\r\n" );
    send( client, buf, strlen( buf ), 0 );
    sprintf( buf, "<HTML><TITLE>Not Found</TITLE>\r\n" );
    send( client, buf, strlen( buf ), 0 );
    sprintf( buf, "<BODY><P>The server could not fulfill\r\n" );
    send( client, buf, strlen( buf ), 0 );
    sprintf( buf, "your request because the resource specified\r\n" );
    send( client, buf, strlen( buf ), 0 );
    sprintf( buf, "is unavailable or nonexistent.\r\n" );
    send( client, buf, strlen( buf ), 0 );
    sprintf( buf, "</BODY></HTML>\r\n" );
    send( client, buf, strlen( buf ), 0 );
}

/*
Send the client informational HTTP headers about a file.

Arguments:
    client [in]     Socket connected to the client
    filename [in]   Name (and path) of the file to inform the client of
*/
void SimplePost::Headers( const int & client, const char * filename )
{
    char buf[1024]; // Sent characters buffer
    const char * mime_type; // MIME type of the input file
    strcpy( buf, "HTTP/1.1 200 OK\r\n" );
    send( client, buf, strlen( buf ), 0 );
    sprintf( buf, "Server: %s/%s\r\n", SP_IDENT, SP_VER );
    send( client, buf, strlen( buf ), 0 );
    if( NULL != (mime_type = SimplePost::GetMimeType( filename )) )
    {
        // According to RFC 2046, the content type should only one sent
        // if it can be determined. If not, the client should do its
        // best to determine what to do with the content instead.
        //
        // Notably, Apache used to send application/octet-stream to
        // indicate arbitrary binary data when it couldn't determine the
        // file type, but that is not correct according to the spec.
        sprintf( buf, "Content-Type: %s\r\n", mime_type );
        send( client, buf, strlen( buf ), 0 );
    };
    strcpy( buf, "\r\n" );
    send( client, buf, strlen( buf ), 0 );
}

/*
Get the internet media type of a file.

Remarks:
    A list of valid MIME types can be found here:
    http://en.wikipedia.org/wiki/MIME_type

Arguments:
    filename [in]   Name (and path) of the file whose MIME type is to be determined

Return Value:
    A static string containing the MIME type will be returned if it can be determined.
    If no type can be determined this function will return NULL.
*/
const char * SimplePost::GetMimeType( const char * filename )
{
    // For now we are going to base the type on the file extension.
    // A more robust method should probably be implemented at some point.
    // Until then, the list is losely ranked from most common to least
    // common to marginally improve the performance of this function.
    if( SimplePost::WildCmp( "*.html", filename, false ) ) return "text/html";
    if( SimplePost::WildCmp( "*.css", filename, false ) ) return "text/css";
    if( SimplePost::WildCmp( "*.txt", filename, false ) ) return "text/plain";
    if( SimplePost::WildCmp( "*.log", filename, false ) ) return "text/plain";
    if( SimplePost::WildCmp( "*.csv", filename, false ) ) return "text/csv";
    if( SimplePost::WildCmp( "*.xml", filename, false ) ) return "text/xml";
    if( SimplePost::WildCmp( "*.json", filename, false ) ) return "application/json";
    if( SimplePost::WildCmp( "*.js", filename, false ) ) return "application/javascript";
    if( SimplePost::WildCmp( "*.pdf", filename, false ) ) return "application/pdf";
    if( SimplePost::WildCmp( "*.zip", filename, false ) ) return "application/zip";
    if( SimplePost::WildCmp( "*.gzip", filename, false ) ) return "application/gzip";
    if( SimplePost::WildCmp( "*.exe", filename, false ) ) return "application/octet-stream";
    if( SimplePost::WildCmp( "*.bin", filename, false ) ) return "application/octet-stream";
    if( SimplePost::WildCmp( "*.elf", filename, false ) ) return "application/octet-stream";
    if( SimplePost::WildCmp( "*.gif", filename, false ) ) return "image/gif";
    if( SimplePost::WildCmp( "*.jpg", filename, false ) ) return "image/jpeg";
    if( SimplePost::WildCmp( "*.jpeg", filename, false ) ) return "image/jpeg";
    if( SimplePost::WildCmp( "*.png", filename, false ) ) return "image/png";
    if( SimplePost::WildCmp( "*.svg", filename, false ) ) return "image/svg+xml";
    if( SimplePost::WildCmp( "*.tiff", filename, false ) ) return "image/tiff";
    if( SimplePost::WildCmp( "*.ico", filename, false ) ) return "image/vnd.microsoft.icon";
    if( SimplePost::WildCmp( "*.flv", filename, false ) ) return "video/x-flv";
    if( SimplePost::WildCmp( "*.mp4", filename, false ) ) return "video/mp4";
    if( SimplePost::WildCmp( "*.webm", filename, false ) ) return "video/webm";
    if( SimplePost::WildCmp( "*.ogv", filename, false ) ) return "video/ogg";
    if( SimplePost::WildCmp( "*.mkv", filename, false ) ) return "video/x-matroska";
    if( SimplePost::WildCmp( "*.mpg", filename, false ) ) return "video/mpeg";
    if( SimplePost::WildCmp( "*.mpeg", filename, false ) ) return "video/mpeg";
    if( SimplePost::WildCmp( "*.wmv", filename, false ) ) return "video/x-ms-wmv";
    if( SimplePost::WildCmp( "*.ogg", filename, false ) ) return "audio/ogg";
    if( SimplePost::WildCmp( "*.mka", filename, false ) ) return "audio/x-matroska";
    if( SimplePost::WildCmp( "*.wma", filename, false ) ) return "audio/x-ms-wma";
    if( SimplePost::WildCmp( "*.fla", filename, false ) ) return "audio/x-fla";
    if( SimplePost::WildCmp( "*.aac", filename, false ) ) return "audio/x-aac";
    if( SimplePost::WildCmp( "*.ps", filename, false ) ) return "application/postscript";
    if( SimplePost::WildCmp( "*.rdf", filename, false ) ) return "application/rdf+xml";
    if( SimplePost::WildCmp( "*.rss", filename, false ) ) return "application/rss+xml";
    if( SimplePost::WildCmp( "*.tar", filename, false ) ) return "application/x-tar";
    if( SimplePost::WildCmp( "*.rar", filename, false ) ) return "application/x-rar-compressed";
    if( SimplePost::WildCmp( "*.ttf", filename, false ) ) return "application/x-font-ttf";
    if( SimplePost::WildCmp( "*.swf", filename, false ) ) return "application/x-shockwave-flash";
    return NULL;
}

/*
Wildcard String Compare

Match a string against a wildcard string such as "*.*" or "bl?h.*" etc.
This is good for file globbing or matching hostmasks.

Remarks:
    This function originated with my xFileSystem class (version 1.3.1 from 25 March 2012).

Delimeters:
    *                       Any number of characters may be skipped until the next matching character is found.
                            This does not NECESSARILY mean that ANY characters will be skipped!
    ?                       Skip exactly ONE character before moving on.
    ^                       Escape the next character, forcing it to be taken literally.

Arguments:
    wild_string [in]        String containing wildcards
                            This is the pattern that match_string will be searched for.
                            Delimeters can ONLY be used in THIS string!
    match_string [in]       Text string to search for the specified pattern
    case_sensitive [in]     Should we bother with characters' case when searching?

Return Value:
    false       No matching pattern was found.
    true        The strings match.
*/
bool SimplePost::WildCmp( const char * wild_string, const char * match_string, const bool case_sensitive )
{
    const char * valid_string; // Last known matching pattern in match_string
    while( *wild_string && (*match_string != '\0' || *wild_string == '*') )
    {
        switch( *wild_string )
        {
            case '?': // Match any single character.
                wild_string++;
                match_string++;
                break;
            case '*': // Match any number of characters.
                while( *wild_string == '*' ) wild_string++;
                if( *wild_string == '\0' ) return true;
                valid_string = NULL;
                while( *match_string )
                {
                    while( *match_string && SimplePost::WildCmp( wild_string, match_string, case_sensitive ) == false ) match_string++;
                    if( *match_string )
                    {
                        valid_string = match_string;
                        match_string++;
                    };
                };
                if( valid_string == NULL ) return false;
                else match_string = valid_string;
                break;
            case '^': // Match the escape character.
                wild_string++;
                if( *wild_string == '\0' ) return false; // There must be at least one character to escape!
                // Fall through to the default case.
                // The next character in wild_string will be taken literally no matter what.
            default: // Any other character.
                if( case_sensitive )
                {
                    if( *wild_string != *match_string ) return false;
                }
                else
                {
                    if( toupper( *wild_string ) != toupper( *match_string ) ) return false;
                };
                wild_string++;
                match_string++;
                break;
        };
    };
    // At least one of the strings should be invalid since that is the main loop's break condition.
    // However, we only succeeded if both are invalid, indicating that both strings are the same (relative) length.
    return ( *wild_string == '\0' && *match_string == '\0' );
}

/*
Start accepting requests from clients.

Exceptions:
    This function does not throw.
*/
void SimplePost::Accept()
{
    int client_sock; // Socket the client connected on
    struct sockaddr_in client_name; // Name of the client
    socklen_t client_name_len; // Length of the client name structure
    SimplePostProcessRequestType * client_ptr; // Structure associated with the client request thread
    pthread_t client_thread; // Handle of the client thread
    struct timespec timeout; // Maximum time to wait for a connection before checking the shutdown sentinel
    fd_set fds; // File descriptor set (for pselect() socket monitoring)

    this->client_count = 0;

    while( this->count )
    {
        FD_ZERO( &fds );
        FD_SET( this->httpd, &fds );
        timeout.tv_sec = 2;
        timeout.tv_nsec = 0;
        if( pselect( this->httpd + 1, &fds, NULL, NULL, &timeout, NULL ) > 0 )
        {
            client_name_len = sizeof( client_name );
            if( -1 != (client_sock = accept( this->httpd, (struct sockaddr *) &client_name, &client_name_len )) )
            {
                client_ptr = new SimplePostProcessRequestType;
                client_ptr->simple_ptr = this;
                client_ptr->client_sock = client_sock;
                if( pthread_create( &client_thread, NULL, &SimplePostProcessRequestThread, (void *) client_ptr ) == 0 )
                {
                    pthread_detach( client_thread );
                }
                else
                {
                    // TODO: Alert the user in some way and attempt to mitigate.
                    // We should probably either give the user some choice as to what happens here.
                    // Either we need to let the client timeout, alert the client that we can't
                    // serve its request (is that counterproductive?), or wait for another thread
                    // to exit so we are below PTHREAD_THREADS_MAX or have enough resources to
                    // spawn a response thread.
                    close( client_ptr->client_sock );
                    delete client_ptr;
                };
            };
        };
    };

    while( this->client_count != 0 ) usleep( SP_SLEEP * 1000 );

    close( this->httpd );
    this->httpd = -1;
}

/*
Process a request accepted by the server.

Exceptions:
    This function does not throw.

Arguments:
    client [in]     Socket connected to the client
*/
void SimplePost::ProcessRequest( const int & client )
{
    char line[1024]; // Line received from the socket
    int length; // Number of characters received in the line
    char * lineit; // Iterator for line
    char method[255]; // Type of request made by the client
    char url[255]; // URL provided by the client
    char path[512]; // Path to the file/document to be retrieved
    unsigned int i; // Iterator for the method, url, and path
    char * queryit; // Iterator for url -> the query string
    struct stat st; // Status structure for calls to stat()

    pthread_mutex_lock( &this->clienttex );
    this->client_count++;
    pthread_mutex_unlock( &this->clienttex );

    length = SimplePost::GetLine( client, line, sizeof( line ) );

    for( i = 0; i < (sizeof( method ) - 1); i++ )
    {
        if( isspace( line[i] ) ) break;
        method[i] = line[i];
    };
    method[i] = '\0';

    if( strcasecmp( method, "GET" ) && strcasecmp( method, "POST" ) )
    {
        // DO NOT THROW AN EXCEPTION!
        // Although this looks like an error, it is actually a valid
        // response to the request.
        SimplePost::UnImplemented( client );
        goto close_client_count;
    };

    if( strcasecmp( method, "POST" ) == 0 )
    {
        // We received a CGI request, which we don't support.
        SimplePost::UnImplemented( client );
        goto close_client_count;
    };

    for( lineit = line + i; *lineit != '\0'; lineit++ ) if( isspace( *lineit ) ) break;
    i = 0;
    for( ++lineit; *lineit != '\0'; lineit++ )
    {
        if( isspace( *lineit ) || i >= (sizeof( url ) - 1) ) break;
        url[i++] = *lineit;
    };
    url[i] = '\0';

    if( strcasecmp( method, "GET" ) == 0 )
    {
        queryit = url;
        while( (*queryit != '?') && (*queryit != '\0') ) queryit++;
        // A '?' is another indication that we received a CGI request.
        // So, url will be truncated to be merely the URL, and queryit
        // will point to the CGI request, which we will never use.
        if( *queryit == '?' ) *queryit++ = '\0';
    }
    else
    {
        queryit = NULL;
    };

    if( this->TranslateRequest( path, sizeof( path ), url ) <= 0 || stat( path, &st ) == -1 )
    {
        // Read and discard headers before telling the client we give up.
        while( (length > 0) && strcmp( "\n", line ) ) length = SimplePost::GetLine( client, line, sizeof( line ) );
        SimplePost::NotFound( client );
        goto close_client_count;
    }
    else
    {
        if( (st.st_mode & S_IFMT) == S_IFDIR ) strcat( path, "/index.html" );
        if( (st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH) ) {} // More CGI. Do nothing.
        else SimplePost::ServeFile( client, path );
    };

    close_client_count:

    close( client );

    pthread_mutex_lock( &this->clienttex );
    this->client_count--;
    pthread_mutex_unlock( &this->clienttex );
}

/*
Translate the URL into a file name and path.

Exceptions:
    This function does not throw.

Arguments:
    path [out]      File name and path
    size [in]       Size of the character array above
    url [in]        URL to translate

Return Value:
    The number of characters written to the path array will be returned
    unless there is an error.
    -1      The path array is not large enough to hold the file name
            and path.
    -2      The requested file is not being served by us.
*/
int SimplePost::TranslateRequest( char * path, int size, const char * url )
{
    unsigned int i; // Iterator for this->trans

    pthread_mutex_lock( &this->transtex );

    if( SimplePost::WildCmp( "http://*/*", url, false ) )
    {
        unsigned int n = 0; // Number of '/' encountered so far in the url
        while( n < 3 ) if( *url++ == '/' ) n++;
    };

    for( i = 0; i < SP_SERV_MAX; i++ )
        if( this->trans[i].url.compare( url ) == 0 )
        {
            if( size <= this->trans[i].filename.size() )
            {
                pthread_mutex_unlock( &this->transtex );
                return -1;
            };

            strcpy( path, this->trans[i].filename.c_str() );
            size = this->trans[i].filename.size();

            if( this->trans[i].count != 0 && --this->trans[i].count == 0 )
            {
                this->trans[i].url.clear();
                this->count--;
            };
            break;
        };

    pthread_mutex_unlock( &this->transtex );
    return ((i == SP_SERV_MAX) ? -2 : size);
}
