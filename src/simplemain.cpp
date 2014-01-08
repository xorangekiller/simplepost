/*
SimplePost - A Basic, Embedded HTTP Server

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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <limits.h>
#include <string.h>
#include <string>
#include <vector>
#include <pthread.h>

/*
Global configuration details
*/
#define SP_MAIN_IDENT       "SimplePost"                            // Program name string
#define SP_MAIN_VER         "0.2.0"                                 // Program version string
#define SP_MAIN_COPYRIGHT   "Copyright (C) 2012-2014 Karl Lenz."    // Program copyright string

/*
Pipe configuration details
*/
#define SP_PIPE_DELAY       1   // Number of seconds to wait between pipe commands
#define SP_PIPE_MAX_LENGTH  5   // Number of characters representing the pipe buffer size

/*
Startup flags
*/
#define SP_FLAG_NONE     0  // No flags are present.
#define SP_FLAG_COUNT    1  // The user specified a count.
#define SP_FLAG_ADDRESS  2  // The user specified an IP address.
#define SP_FLAG_PORT     4  // The user specified a port.
#define SP_FLAG_PID      8  // Send the requested commands to another instance of this program.
#define SP_FLAG_QUIET   16  // Do not print anything to standard output.
#define SP_FLAG_HELP    32  // Display our help information.
#define SP_FLAG_VERSION 64  // Display our version information.
#define SP_FLAG_FILE   128  // The next argument passed by the user is a file.
#define SP_FLAG_ERROR  256  // An error occurred; abort!

/*
This simple structure contains the information necessary to serve a file.
*/
struct SimpleFile
{
    std::string filename; // File to be served
    unsigned int count; // Number of times the file may be downloaded
};

/*
Global variables
*/
static unsigned int flags = SP_FLAG_NONE; // Startup flags indicating user options
static std::vector< SimpleFile > files; // Files to serve
static std::string address; // Internet protocol address of the server
static unsigned short port = 0; // Port the server will be bound to
static pid_t another_pid = 0; // PID of another instance of this program
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
            files.front().count = (unsigned int) i;
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
Process the alternate instance argument.

Arguments:
    arg [in]        Argument string to process
*/
static void SetPidFlag( const char * arg )
{
    if( flags & SP_FLAG_PID )
    {
        fprintf( stderr, "invalid option -- PID already specified\n" );
        flags |= SP_FLAG_ERROR;
    }
    else
    {
        int i; // Integer represented by arg
        i = atoi( arg );
        if( i < 1 )
        {
            fprintf( stderr, "%d: PID must be a valid process identifier\n", i );
            flags |= SP_FLAG_ERROR;
        }
        else
        {
            another_pid = (pid_t) i;
            flags |= SP_FLAG_PID;
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
Set the file flag.
*/
static void SetFileFlag()
{
    flags |= SP_FLAG_FILE;
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
Process the file argument.

Arguments:
    file [in]       File to add to the list of files to serve
*/
static void ProcessInputFile( const char * file )
{
    struct stat st; // File status information structure

    if( stat( file, &st ) == -1 )
    {
        fprintf( stderr, "%s: No such file or directory\n", file );
        flags |= SP_FLAG_ERROR;
    }
    else if( !(S_ISREG( st.st_mode ) || S_ISLNK( st.st_mode )) )
    {
        fprintf( stderr, "%s: Must of a regular file or link to a one\n", file );
        flags |= SP_FLAG_ERROR;
    }
    else
    {
        if( files.front().filename.empty() )
        {
            files.front().filename = file;
            if( !(flags & SP_FLAG_COUNT) ) files.front().count = 0;
        }
        else
        {
            files.push_back( SimpleFile() );
            files.back().filename = file;
            files.back().count = files.front().count;
        };
    };
}

/*
Parse the arguments passed to this program.

Arguments:
    argc [in]       Number of arguments in the array
    argv [in]       Array of arguments
*/
static void SetFlags( int argc, char * argv[] )
{
    if( argc < 2 )
    {
        fprintf( stderr, "invalid syntax\n" );
        fprintf( stderr, "Try `simplepost --help' for more information.\n" );
        flags |= SP_FLAG_ERROR;
    }
    else
    {
        unsigned int i; // Iterator for the arguments array

        if( files.empty() )
        {
            files.push_back( SimpleFile() );
        }
        else
        {
            files.clear();
            files.push_back( SimpleFile() );
        };

        for( i = 1; i < argc; i++ )
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
                            else if( strncmp( "--pid=", argv[i], 6 ) == 0 ) SetPidFlag( argv[i] + 6 );
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
                    SetFileFlag();
                    break;
            };

            if( flags & SP_FLAG_ERROR ) return;
            else if( flags & SP_FLAG_HELP || flags & SP_FLAG_VERSION ) return;
            else if( flags & SP_FLAG_FILE ) break;
        };

        if( !(flags & SP_FLAG_FILE) )
        {
            SetInvalidFlag( (i >= argc) ? "INTERNAL_ERROR" : argv[i] );
            return;
        };

        for( ; i < argc; i++ )
        {
            ProcessInputFile( argv[i] );
            if( flags & SP_FLAG_ERROR ) return;
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
    printf( "  -i, --ip-address=ADDRESS use ADDRESS as the server's ip address\n" );
    printf( "  -p, --port=PORT          bind to PORT on the local machine\n" );
    printf( "                           a random port will be chosen if this is not specified\n" );
    printf( "      --pid=PID            act on the instance of this program with process identifier PID\n" );
    printf( "  -q, --quiet              do not print anything to standard output\n" );
    printf( "      --help               display this help and exit\n" );
    printf( "      --version            output version information and exit\n\n" );
    printf( "Examples:\n" );
    printf( "  simplepost -p 80 -c 1 -q FILE            Serve FILE on port 80 one time.\n" );
    printf( "  simplepost --pid=99031 --count=2 FILE    Serve FILE twice on the instance of simplepost with the process identifier 99031.\n" );
    printf( "  simplepost FILE                          Serve FILE on a random port until SIGTERM is received.\n" );
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
Add a file to the current SimplePost instance.

Remarks:
    This function is a basic wrapper around SimplePost::Serve() that prints status messages.
    The arguments are the same as the final two arguments of the function we are wrapping.

Return Value:
    false   An error occurred adding the file
    true    The file is being served
*/
static bool SimplePostServe( const char * filename, unsigned int count )
{
    char url[2048]; // Uniform Resource Locator associated with the file we are serving
    bool ret = false; // Return value

    switch( httpd->Serve( url, sizeof( url ), filename, count ) )
    {
        case -3:
            if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "%s: Internal error adding file to serve!\n", SP_MAIN_IDENT );
            break;
        case -2:
            if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "%s: Cannot serve more than %u files!\n", SP_MAIN_IDENT, SP_SERV_MAX );
            break;
        case -1:
            if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "File \"%s\" does not exist or is not a file!\n", filename );
            break;
        default:
            if( !(flags & SP_FLAG_QUIET) )
            {
                switch( count )
                {
                    case 0:
                        printf( "Serving %s on %s indefinitely.\n", filename, url );
                        break;
                    case 1:
                        printf( "Serving %s on %s once.\n", filename, url );
                        break;
                    default:
                        printf( "Serving %s on %s %u times.\n", filename, url, count );
                        break;
                };
            };
            ret = true;
            break;
    };

    return ret;
}

/*
Put the padded length of the string in the buffer.

Remarks:
    This is a utility function that should be called before every write() over the pipe. Read the
    documentation carefully. This function is very easy to break (read: seg. fault) if used
    improperly. Don't abuse it!

Arguments:
    buf [out]       Buffer to hold the padded length of the input string
                        * This buffer MUST be at least SP_PIPE_MAX_LENGTH characters long!
                        * There will be NO TERMINATING CHARACTER assigned by this function.
                        * Exactly the first SP_PIPE_MAX_LENGTH bytes will be filled.
    s [in]          String to get the length of
*/
static void GetPipeStringLength( char * buf, const char * s )
{
    int length; // Number of bytes written to the buffer

    length = sprintf( buf, "%lu", strlen( s ) );
    if( length < SP_PIPE_MAX_LENGTH )
    {
        int i; // Iterator for the write buffer
        int diff; // Number of (character) zeros to add to the beginning of the write buffer

        diff = SP_PIPE_MAX_LENGTH - length;
        for( i = (length - 1); i >= 0; i-- ) buf[i + diff] = buf[i];
        for( i = 0; i < diff; i++ ) buf[i] = '0';
    };
}

/*
Send data to the named pipe.

Remarks:
    This function doesn't fail by return value. It's possible that this will cause it to hang, but
    that is a problem for the caller. If the pipe is not opened for reading on the other end
    SIGPIPE will be sent to this program. http://man7.org/linux/man-pages/man7/fifo.7.html

Arguments:
    pipe_descriptor [in]    File descriptor of the named pipe
    command [in]            Command to send to over the pipe
                            This parameter may be NULL if you *only* want to read from the pipe.
    data [in]               NULL-terminated string of data to send to the receiver
                            The terminating character WILL NOT be sent!
*/
static void SendPipeData( int pipe_descriptor, const char * command, const char * data )
{
    char write_to_buffer[SP_PIPE_MAX_LENGTH]; // Buffer for the number of characters to write to the pipe

    if( command != NULL )
    {
        GetPipeStringLength( write_to_buffer, command );
        write( pipe_descriptor, write_to_buffer, SP_PIPE_MAX_LENGTH );
        sleep( SP_PIPE_DELAY );

        write( pipe_descriptor, command, strlen( command ) );
        sleep( SP_PIPE_DELAY );
    };

    if( data != NULL )
    {
        GetPipeStringLength( write_to_buffer, data );
        write( pipe_descriptor, write_to_buffer, SP_PIPE_MAX_LENGTH );
        sleep( SP_PIPE_DELAY );

        write( pipe_descriptor, data, strlen( data ) );
        sleep( SP_PIPE_DELAY );
    };
}

/*
Send a command to the named pipe and read the response.

Arguments:
    pipe_descriptor [in]    File descriptor of the named pipe
    command [in]            Command to send to over the pipe
                            This parameter may be NULL if you *only* want to read from the pipe.
    read_buffer [out]       NULL-terminated string read from the pipe
                            The terminating character is added by this function if it is not
                            received over the pipe.
    read_buffer_size [in]   Size of the read buffer

Return Value:
    false   Failed to read from the pipe
            An error message has been printed (if appropriate)
    true    The command was sent successfully.
*/
static bool ReceivePipeData( int pipe_descriptor, const char * command, char * read_buffer, const size_t read_buffer_size )
{
    ssize_t read_length; // Number of characters read from the pipe
    int read_to; // Number of character to read from the pipe
    char read_to_buffer[SP_PIPE_MAX_LENGTH + 1]; // Buffer for the number of characters to read from the pipe

    if( command != NULL )
    {
        GetPipeStringLength( read_to_buffer, command );
        write( pipe_descriptor, read_to_buffer, SP_PIPE_MAX_LENGTH );
        sleep( SP_PIPE_DELAY );

        write( pipe_descriptor, command, strlen( command ) );
        sleep( SP_PIPE_DELAY );
    };

    read_length = 0;
    do
    {
        read_length += read( pipe_descriptor, read_to_buffer, SP_PIPE_MAX_LENGTH - read_length );
    } while( read_length < SP_PIPE_MAX_LENGTH );

    read_to_buffer[SP_PIPE_MAX_LENGTH] = '\0';
    read_to = atoi( read_to_buffer );
    if( read_to != 0 )
    {
        if( read_buffer == NULL )
        {
            if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "%s: Read buffer required for %s from %d!\n", SP_MAIN_IDENT, (command == NULL) ? "NULL" : command, another_pid );
            return false;
        };

        if( read_to < 0 || read_to > (read_buffer_size - 2) )
        {
            if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "%s: Invalid length %d from PID %d.\n", SP_MAIN_IDENT, read_to, another_pid );
            return false;
        };

        read_length = 0;
        do
        {
            read_length += read( pipe_descriptor, read_buffer, read_to - read_length );
        } while( read_length < read_to );
        if( read_length <= 0 ) return false;
        read_buffer[read_to] = '\0';
    };

    return true;
}

/*
Create a named pipe and accept commands from another instance of this program.

Arguments:
    ptr [in]        Ignored!
                    This parameter should be NULL. It only exists for pthread comaptibility.

Return Value:
    NULL will always be returned.
*/
void * AddFilePipeThread( void * ptr )
{
    char pipe_path[500]; // Path of the named pipe
    int pipe_descriptor; // File descriptor associated with the named pipe
    char read_buffer[500]; // Data read from the pipe
    struct stat buf; // File status buffer

    another_pid = -1;

    sprintf( pipe_path, "/tmp/simplepost_pipe_%d", getpid() );
    if( stat( pipe_path, &buf ) == 0 )
    {
        if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "%s: Pipe %s already exists.\n", SP_MAIN_IDENT, pipe_path );
        return NULL;
    };

    if( mkfifo( pipe_path, O_RDWR | O_CREAT | S_IRWXU | S_IRWXG | S_IRWXO ) != 0 )
    {
        if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "%s: Failed to create pipe %s.\n", SP_MAIN_IDENT, pipe_path );
        return NULL;
    };

    pipe_descriptor = open( pipe_path, O_RDWR | O_APPEND );
    if( pipe_descriptor == -1 )
    {
        if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "%s: Failed to open pipe %s.\n", SP_MAIN_IDENT, pipe_path );
        remove( pipe_path );
        return NULL;
    };

    while( 1 )
    {
        if( ReceivePipeData( pipe_descriptor, NULL, read_buffer, sizeof( read_buffer ) / sizeof( read_buffer[0] ) ) == false ) break;

        if( strcmp( read_buffer, "GetProgramVersion" ) == 0 )
        {
            SendPipeData( pipe_descriptor, NULL, SP_MAIN_VER );
        }
        else if( strcmp( read_buffer, "ServeFile" ) == 0 )
        {
            if( ReceivePipeData( pipe_descriptor, NULL, read_buffer, sizeof( read_buffer ) / sizeof( read_buffer[0] ) ) == true )
            {
                const char * s; // Iterator for the read buffer
                unsigned int i; // Index of the parser in the buffer
                char * filename; // Name (and path) of the file to serve
                unsigned int length; // Length of the filename
                unsigned int count; // Number of times the file should be served

                i = 0;
                filename = (char *) malloc( sizeof( char ) * (strlen( read_buffer ) + 1) );
                length = 0;
                count = 0;

                for( s = read_buffer; *s != '\0'; s++ )
                {
                    switch( i )
                    {
                        case 0:
                            if( *s == ';' )
                            {
                                filename[length] = '\0';
                                i++;
                            }
                            else
                            {
                                filename[length++] = *s;
                            };
                            break;
                        case 1:
                            if( *s == ';' ) i++;
                            else if( isdigit( *s ) ) count = count * 10 + (*s - '0');
                            break;
                        default:
                            i++;
                            break;
                    };
                };

                if( i == 2 )
                {
                    SimplePostServe( filename, count );
                }
                else
                {
                    if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "%s: Invalid response \"%s\".\n", SP_MAIN_IDENT, read_buffer );
                };

                free( filename );
            };
        }
        else if( strcmp( read_buffer, "ClosePipe" ) == 0 )
        {
            break;
        }
        else
        {
            if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "%s: Invalid command \"%s\".\n", SP_MAIN_IDENT, read_buffer );
        };
    };

    #ifdef DEBUG
    if( !(flags & SP_FLAG_QUIET) ) printf( "%s: Exiting pipe handler thread 0x%lx...\n", SP_MAIN_IDENT, pthread_self() );
    #endif // DEBUG

    close( pipe_descriptor );
    sleep( SP_PIPE_DELAY );
    remove( pipe_path );

    return NULL;
}

/*
Kill the pipe thread if it is running.
*/
void KillPipeThread()
{
    char pipe_path[500]; // Path of the named pipe
    int pipe_descriptor; // File descriptor associated with the named pipe

    sprintf( pipe_path, "/tmp/simplepost_pipe_%d", getpid() );
    pipe_descriptor = open( pipe_path, O_RDWR | O_APPEND );
    if( pipe_descriptor == -1 ) return;

    SendPipeData( pipe_descriptor, "ClosePipe", NULL );

    close( pipe_descriptor );
    sleep( SP_PIPE_DELAY );
    remove( pipe_path );
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
        case SIGUSR1:
            {
                pthread_t pipe_thread; // Handle of the pipe thread

                if( pthread_create( &pipe_thread, NULL, &AddFilePipeThread, NULL ) == 0 )
                {
                    #ifdef DEBUG
                    if( !(flags & SP_FLAG_QUIET) ) printf( "%s: Spawned pipe handler thread 0x%lx.\n", SP_MAIN_IDENT, pipe_thread );
                    #endif // DEBUG
                    pthread_detach( pipe_thread );
                }
                else
                {
                    if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "%s: Failed to spawn a thread to handle the pipe request!\n", SP_MAIN_IDENT );
                };
            }
            break;
        case SIGPIPE:
            if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "%s: PIPE COMMUNICATION ERROR!\n", SP_MAIN_IDENT );
            KillPipeThread();
            if( httpd ) delete httpd;
            exit( 4 );
            break;
        case SIGTSTP:
        case SIGQUIT:
        case SIGTERM:
            if( !(flags & SP_FLAG_QUIET) ) printf( "%s: Shutting down...\n", SP_MAIN_IDENT );
            KillPipeThread();
            if( httpd ) delete httpd;
            exit( 0 );
            break;
        default:
            if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "%s: PANIC!\n", SP_MAIN_IDENT );
            KillPipeThread();
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
    int ret = 0; // Program's return code

    signal( SIGUSR1, &ShutdownHandler );
    signal( SIGPIPE, &ShutdownHandler );
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
    if( flags & SP_FLAG_PID )
    {
        char pipe_path[500]; // Path of the named pipe
        int pipe_descriptor; // File descriptor associated with the named pipe
        char read_buffer[500]; // Data read from the pipe

        kill( another_pid, SIGUSR1 );
        sleep( SP_PIPE_DELAY );

        sprintf( pipe_path, "/tmp/simplepost_pipe_%d", another_pid );
        pipe_descriptor = open( pipe_path, O_RDWR | O_APPEND );
        if( pipe_descriptor == -1 )
        {
            if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "%s: Failed to open the pipe to PID %d.\n", SP_MAIN_IDENT, another_pid );
            return 1;
        };

        if( ReceivePipeData( pipe_descriptor, "GetProgramVersion", read_buffer, sizeof( read_buffer ) / sizeof( read_buffer[0] ) ) == false ) return 1;
        if( strcmp( read_buffer, SP_MAIN_VER ) != 0 )
        {
            if( !(flags & SP_FLAG_QUIET) ) fprintf( stderr, "%s: %s is incomaptible with %s.\n", SP_MAIN_IDENT, SP_MAIN_VER, read_buffer );
            SendPipeData( pipe_descriptor, "ClosePipe", NULL );
            return 1;
        };

        for( std::vector< SimpleFile >::iterator it = files.begin(); it != files.end(); it++ )
        {
            sprintf( read_buffer, "%s;%u;", it->filename.c_str(), it->count );
            SendPipeData( pipe_descriptor, "ServeFile", read_buffer );
        };
        SendPipeData( pipe_descriptor, "ClosePipe", NULL );

        return 0;
    };

    try
    {
        httpd = new SimplePost;

        if( flags & SP_FLAG_PORT ) httpd->Init( port, address.empty() ? NULL : address.c_str() );
        else httpd->Init( &port, address.empty() ? NULL : address.c_str() );
        if( !(flags & SP_FLAG_QUIET) ) printf( "Started %s HTTP server on port %u with PID %d.\n", SP_MAIN_IDENT, port, getpid() );

        for( std::vector< SimpleFile >::iterator it = files.begin(); it != files.end(); it++ )
        {
            if( SimplePostServe( it->filename.c_str(), it->count ) == false )
            {
                ret = 1;
                break;
            };
        };

        if( ret == 0 )
        {
            httpd->Run();
            httpd->Block();
        };
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
