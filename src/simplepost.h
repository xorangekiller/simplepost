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

#ifndef _SIMPLEPOST_H_
#define _SIMPLEPOST_H_

#include <exception>
#include <string>
#include <pthread.h>

/*
Static configuration details for the server.
*/
#define SP_PORT_MAX 65535        // Highest port number
#define SP_BACKLOG  5            // Maximum number of pending connections before clients start getting refused
#define SP_SLEEP    100          // Milliseconds to sleep between shutdown checks while blocking
#define SP_SERV_MAX 50           // Maximum number of files that may be served at once
#define SP_IDENT    "simplepost" // String containing the short name of the server (no spaces allowed)
#define SP_VER      "0.1.0"      // String identifying the server version

/*
The definitions below are the only valid IDs for SimpleExcept.
*/
#define SEID_NONE           0 // No exception; oops!
#define SEID_CUSTOM         1 // Custom exception message was set
#define SEID_INITIALIZED    2 // Server is already initialized
#define SEID_SOCKET         3 // Socket could not be created
#define SEID_BIND           4 // Server failed to bind to socket
#define SEID_ADDRESS        5 // Invalid source address specified
#define SEID_PORTALLOC      6 // Port could not be allocated
#define SEID_LISTEN         7 // Cannot listen on socket
#define SEID_ACCEPT         8 // Cannot accept connections on socket
#define SEID_UNINITIALIZED  9 // Server is not running
#define SEID_MAX            9 // Not really an ID, just a definition for the highest numbered ID

/*
This is the SimplePost exception class.
*/
class SimpleExcept: public std::exception
{
    public:
        // Initialization
        SimpleExcept( const unsigned int & id ) throw();
        SimpleExcept( const char * message ) throw();
        SimpleExcept( const SimpleExcept & e ) throw();
        ~SimpleExcept() throw();
        // Get Functions
        unsigned int GetID() const throw();
        const char * what() const throw();
        const char * GetMessage() const throw();
        // Operators
        bool operator==( const SimpleExcept & e ) const throw();
        bool operator==( const unsigned int & id ) const throw();
    protected:
        static char * CreateMessage( const unsigned int & id ) throw();
    private:
        unsigned int id; // Identification number of the exception
        char * message; // Short message explaining the exception
};

/*
This is a simple HTTP server class.

Exceptions:
    All public methods in this class throw errors of type SimpleExcept
    unless it is explicitly noted otherwise in their documentation or
    they are static. It would break HTTP control flow to implement
    exceptions so no static methods throw.

Remarks:
    This class is based on TinyHTTPd 0.1.0 by J. David Blackstone. It
    has been updated, ported to C++, and had its CGI support stripped.

Example Usage:
    SimplePost sp; // Example SimplePost instance
    sp.Init( 41471 );
    sp.Serve( NULL, 0, "example1.txt", 2 );
    sp.Serve( NULL, 0, "example2.txt", 0 );
    sp.Run();
    // Use signals or another thread to release the block with sp.Kill()
    sp.Block();
*/
class SimplePost
{
    public:
        SimplePost();
        ~SimplePost();
        void Init( unsigned short * port, const char * address = NULL );
        void Init( unsigned short port, const char * address = NULL );
        int Serve( char * url, int size, const char * filename, unsigned int count );
        void Run();
        void Block();
        void Kill();
        bool Running() const;
    protected:
        static void ServeFile( const int & client, const char * filename );
        static int GetLine( const int & sock, char * buf, const int & size );
        static void UnImplemented( const int & client );
        static void NotFound( const int & client );
        static void Headers( const int & client, const char * filename );
        static const char * GetMimeType( const char * filename );
        static bool WildCmp( const char * wild_string, const char * match_string, const bool case_sensitive );
    private:
        friend void * SimplePostAcceptThread( void * ptr );
        friend void * SimplePostProcessRequestThread( void * ptr );
        void Accept();
        void ProcessRequest( const int & client );
        int TranslateRequest( char * path, int size, const char * url );
    private:
        /* This translation type can store the data associated with each file being served. */
        struct TransType
        {
            std::string filename; // Name (and path) of the file on the filesystem
            std::string url; // URL assigned to the file
            unsigned int count; // Number of times the file may be downloaded
        };
    private:
        // Initialization
        int httpd; // Socket for the HTTP server
        unsigned short port; // Port for the HTTP server
        std::string address; // Address of the HTTP server
        pthread_t accept_thread; // Handle of the primary thread
        // Files
        TransType trans[SP_SERV_MAX]; // Array of files being served
        unsigned int count; // Number of files being served
        unsigned int trigger; // Current write position in the trans array
        pthread_mutex_t transtex; // Mutex for trans, count, and trigger
        // Client Tracking
        unsigned int client_count; // Number of clients currently being served
        pthread_mutex_t clienttex; // Mutex for client_count
};

#endif // _SIMPLEPOST_H_
