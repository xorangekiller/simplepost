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
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <limits.h>
#include <string.h>
#include <string>

/*
Configuration details
*/
#define SP_MAIN_IDENT       "SimplePost"                             // Program name string
#define SP_MAIN_VER         "0.1.0"                                  // Program version string
#define SP_MAIN_COPYRIGHT   "Copyright (C) 2012-2013 xorangekiller." // Program copyright string

/*
Startup flags
*/
#define SP_FLAG_NONE     0  // No flags are present.
#define SP_FLAG_COUNT    1  // The user specified a count.
#define SP_FLAG_ADDRESS  2  // The user specified an IP address.
#define SP_FLAG_PORT     4  // The user specified a port.
#define SP_FLAG_QUIET    8  // Do not print anything to standard output.
#define SP_FLAG_HELP    16  // Display our help information.
#define SP_FLAG_VERSION 32  // Display our version information.
#define SP_FLAG_ERROR   64  // An error occurred; abort!

/*
Global variables
*/
static unsigned int flags = SP_FLAG_NONE; // Startup flags indicating user options
static std::string filename; // File to be served
static unsigned int count = 0; // Number of times the file may be downloaded
static std::string address; // Internet protocol address of the server
static unsigned short port = 0; // Port the server will be bound to
static SimplePost * httpd = NULL; // Simple HTTP server instance

/*
Process the serve count argument.

Arguments:
    arg [in]        Argument string to process
*/
static void SetCountFlag( const char * arg )
{
    if( flags & SP_FLAG_COUNT )
    {
        fprintf( stderr, "invalid option -- count already set\n" );
        flags |= SP_FLAG_ERROR;
    }
    else
    {
        int i; // Integer represented by arg
        i = atoi( arg );
        if( i < 0 )
        {
            fprintf( stderr, "%d: COUNT must be between 0 and %d\n", i, INT_MAX );
            flags |= SP_FLAG_ERROR;
        }
        else
        {
            count = (unsigned int) i;
            flags |= SP_FLAG_COUNT;
        };
    };
}

/*
Process the custom IP address argument.

Arguments:
    arg [in]        Argument string to process
*/
static void SetAddressFlag( const char * arg )
{
    if( flags & SP_FLAG_ADDRESS )
    {
        fprintf( stderr, "invalid option -- ip address already set\n" );
        flags |= SP_FLAG_ERROR;
    }
    else
    {
        address = arg;
        flags |= SP_FLAG_ADDRESS;
    };
}

/*
Process the custom port argument.

Arguments:
    arg [in]        Argument string to process
*/
static void SetPortFlag( const char * arg )
{
    if( flags & SP_FLAG_PORT )
    {
        fprintf( stderr, "invalid option -- port already set\n" );
        flags |= SP_FLAG_ERROR;
    }
    else
    {
        int i; // Integer represented by arg
        i = atoi( arg );
        if( i < 1 )
        {
            fprintf( stderr, "%d: PORT must be between 1 and %u\n", i, USHRT_MAX );
            flags |= SP_FLAG_ERROR;
        }
        else
        {
            port = (unsigned short) i;
            flags |= SP_FLAG_PORT;
        };
    };
}

/*
Process the standard output suppression argument.
*/
static void SetQuietFlag()
{
    if( flags & SP_FLAG_QUIET )
    {
        fprintf( stderr, "invalid option -- standard output already suppressed\n" );
        flags |= SP_FLAG_ERROR;
    }
    else
    {
        flags |= SP_FLAG_QUIET;
    };
}

/*
Set the help flag.
*/
static void SetHelpFlag()
{
    flags |= SP_FLAG_HELP;
}

/*
Set the version flag.
*/
static void SetVersionFlag()
{
    flags |= SP_FLAG_VERSION;
}

/*
Process an invalid argument.

Arguments:
    arg [in]        Argument string to process
*/
static void SetInvalidFlag( const char * arg )
{
    fprintf( stderr, "invalid option -- %s\n", arg );
    fprintf( stderr, "Try `simplepost --help' for more information.\n" );
    flags |= SP_FLAG_ERROR;
}

/*
Parse the arguments passed to this program.

Arguments:
    argc [in]       Number of arguments in the array
    argv [in]       Array of arguments
*/
static void SetFlags( int argc, char * argv[] )
{
    struct stat st; // Status structure for calls to stat()

    if( argc < 2 )
    {
        fprintf( stderr, "invalid syntax\n" );
        fprintf( stderr, "Try `simplepost --help' for more information.\n" );
        flags |= SP_FLAG_ERROR;
    }
    else
    {
        for( unsigned int i = 1; i < argc; i++ )
        {
            switch( argv[i][0] )
            {
                case '-':
                    switch( argv[i][1] )
                    {
                        case '-':
                            if( strncmp( "--count=", argv[i], 8 ) == 0 ) SetCountFlag( argv[i] + 8 );
                            else if( strncmp( "--ip-address=", argv[i], 13 ) == 0 ) SetAddressFlag( argv[i] + 13 );
                            else if( strncmp( "--port=", argv[i], 7 ) == 0 ) SetPortFlag( argv[i] + 7 );
                            else if( strcmp( "--quiet", argv[i] ) == 0 ) SetQuietFlag();
                            else if( strcmp( "--help", argv[i] ) == 0 ) SetHelpFlag();
                            else if( strcmp( "--version", argv[i] ) == 0 ) SetVersionFlag();
                            else SetInvalidFlag( argv[i] );
                            break;
                        case 'c':
                            SetCountFlag( argv[++i] );
                            break;
                        case 'i':
                            SetAddressFlag( argv[++i] );
                            break;
                        case 'p':
                            SetPortFlag( argv[++i] );
                            break;
                        case 'q':
                            SetQuietFlag();
                            break;
                        default:
                            SetInvalidFlag( argv[i] );
                            break;
                    };
                    break;
                default:
                    if( !((i == (argc - 1)) && !(flags & SP_FLAG_HELP || flags & SP_FLAG_VERSION)) ) SetInvalidFlag( argv[i] );
                    break;
            };
            if( flags & SP_FLAG_ERROR ) break;
        };

        if( !(flags & SP_FLAG_ERROR || flags & SP_FLAG_HELP || flags & SP_FLAG_VERSION) )
        {
            filename = argv[argc - 1];
            if( stat( filename.c_str(), &st ) == -1 )
            {
                fprintf( stderr, "%s: No such file or directory\n", filename.c_str() );
                flags |= SP_FLAG_ERROR;
            };
        };
    };
}

/*
Display our help information.
*/
static void Help()
{
    printf( "Usage: simplepost [OPTION] FILE\n" );
    printf( "Serve FILE COUNT times via http on port PORT with ip address ADDRESS.\n\n" );
    printf( "  -c, --count=COUNT        serve the file COUNT times\n" );
    printf( "                           by default FILE will be served until the server is shut down\n" );
    printf( "  -i, --ip-address=ADDRESS use ADDRESS as the server's ip address \n" );
    printf( "  -p, --port=PORT          bind to PORT on the local machine\n" );
    printf( "                           a random port will be chosen if this is not specified\n" );
    printf( "  -q, --quiet              do not print anything to standard output\n" );
    printf( "      --help               display this help and exit\n" );
    printf( "      --version            output version information and exit\n\n" );
    printf( "Examples:\n" );
    printf( "  simplepost -p 80 -c 1 -q file.txt     Serve file.txt on port 80 one time.\n" );
    printf( "  simplepost file.txt                   Serve file.txt on a random.\n" );
}

/*
Display our version information.
*/
static void Version()
{
    printf( "%s %s\n", SP_MAIN_IDENT, SP_MAIN_VER );
    printf( "%s\n", SP_MAIN_COPYRIGHT );
    printf( "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl.html>.\n" );
    printf( "This is free software: you are free to change and redistribute it.\n" );
    printf( "There is NO WARRANTY, to the extent permitted by law.\n" );
}

/*
Shutdown signal handler.

Arguments:
    sig [in]    Signal to handle
*/
void ShutdownHandler( int sig )
{
    switch( sig )
    {
        case SIGTSTP:
        case SIGQUIT:
        case SIGTERM:
            if( !(flags & SP_FLAG_QUIET) ) printf( "%s: Shutting down...\n", SP_MAIN_IDENT );
            if( httpd ) delete httpd;
            exit( 0 );
            break;
        default:
            if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "%s: PANIC!\n", SP_MAIN_IDENT );
            if( httpd ) delete httpd;
            exit( 4 );
            break;
    };
}

/*
Initialize the simple HTTP server.
*/
int main( int argc, char * argv[] )
{
    char url[2048]; // Uniform Resource Locator associated with the file we are serving
    int ret = 0; // Program's return code

    signal( SIGTSTP, &ShutdownHandler );
    signal( SIGQUIT, &ShutdownHandler );
    signal( SIGTERM, &ShutdownHandler );

    SetFlags( argc, argv );

    if( flags & SP_FLAG_ERROR )
    {
        return 1;
    };
    if( flags & SP_FLAG_HELP )
    {
        Help();
        return 0;
    };
    if( flags & SP_FLAG_VERSION )
    {
        Version();
        return 0;
    };

    try
    {
        httpd = new SimplePost;

        if( flags & SP_FLAG_PORT ) httpd->Init( port, address.empty() ? NULL : address.c_str() );
        else httpd->Init( &port, address.empty() ? NULL : address.c_str() );
        if( !(flags & SP_FLAG_QUIET) ) printf( "Started %s HTTP server on port %u with PID %d.\n", SP_MAIN_IDENT, port, getpid() );

        switch( httpd->Serve( url, sizeof( url ), filename.c_str(), count ) )
        {
            case -3:
                if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "%s: Internal error adding file to serve!\n", SP_MAIN_IDENT );
                break;
            case -2:
                if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "%s: Cannot serve more than %u files!\n", SP_MAIN_IDENT, SP_SERV_MAX );
                break;
            case -1:
                if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "File \"%s\" does not exist or is not a file!\n", filename.c_str() );
                break;
            default:
                if( !(flags & SP_FLAG_QUIET) )
                {
                    switch( count )
                    {
                        case 0:
                            printf( "Serving %s on %s indefinitely.\n", filename.c_str(), url );
                            break;
                        case 1:
                            printf( "Serving %s on %s once.\n", filename.c_str(), url );
                            break;
                        default:
                            printf( "Serving %s on %s %u times.\n", filename.c_str(), url, count );
                            break;
                    };
                };
                break;
        };

        httpd->Run();
        httpd->Block();
        if( !(flags & SP_FLAG_QUIET) ) printf( "%s: Shutting down...\n", SP_MAIN_IDENT );
    }
    catch( SimpleExcept & e )
    {
        if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "%s\n", e.GetMessage() );
        ret = 2;
    }
    catch( std::exception & e )
    {
        if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "%s\n", e.what() );
        ret = 3;
    }
    catch( ... )
    {
        if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "%s: PANIC!\n", SP_MAIN_IDENT );
        ret = 4;
    };

    if( httpd ) delete httpd;
    return ret;
}
