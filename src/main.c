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
#include "simplearg.h"
#include "simplecmd.h"
#include "impact.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/*
Key global server instances
*/
static simplecmd_t cmdd = NULL; // Local command handler instance
static simplepost_t httpd = NULL; // Web server instance

/*
Print our help information.
*/
static void __print_help()
{
    printf( "Usage: %s [GLOBAL_OPTIONS] [FILE_OPTIONS] FILE\n\n", SP_MAIN_SHORT_NAME );
    printf( "Serve FILE COUNT times via HTTP on port PORT with IP address ADDRESS.\n" );
    printf( "Multiple FILE and FILE_OPTIONS may be specified in sequence after GLOBAL_OPTIONS.\n\n" );
    printf( "Global Options:\n" );
    printf( "  -i, --address=ADDRESS    use ADDRESS as the server's ip address\n" );
    printf( "  -p, --port=PORT          bind to PORT on the local machine\n" );
    printf( "                           a random port will be chosen if this is not specified\n" );
    printf( "      --pid=PID            act on the instance of this program with process identifier PID\n" );
    printf( "                           by default the existing instance matching ADDRESS and PORT will be used if possible\n" );
    printf( "      --new                act exclusively on the current instance of this program\n" );
    printf( "                           this option and --pid are mutually exclusive\n" );
    printf( "  -q, --quiet              do not print anything to standard output\n" );
    printf( "      --help               display this help and exit\n" );
    printf( "      --version            output version information and exit\n\n" );
    printf( "File Options:\n" );
    printf( "  -c, --count=COUNT        serve the file COUNT times\n" );
    printf( "                           by default FILE will be served until the server is shut down\n\n" );
    printf( "Examples:\n" );
    printf( "  %s -p 80 -q -c 1 FILE            Serve FILE on port 80 one time.\n", SP_MAIN_SHORT_NAME );
    printf( "  %s --pid=99031 --count=2 FILE    Serve FILE twice on the instance of simplepost with the process identifier 99031.\n", SP_MAIN_SHORT_NAME );
    printf( "  %s FILE                          Serve FILE on a random port until SIGTERM is received.\n", SP_MAIN_SHORT_NAME );
}

/*
Print our version information.
*/
static void __print_version()
{
    printf( "%s %s\n", SP_MAIN_DESCRIPTION, SP_MAIN_VERSION );
    printf( "%s\n", SP_MAIN_COPYRIGHT );
    printf( "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl.html>.\n" );
    printf( "This is free software: you are free to change and redistribute it.\n" );
    printf( "There is NO WARRANTY, to the extent permitted by law.\n" );
}

/*
Safely handle SIGPIPE by completely resetting the command server.

Remarks:
    This function effectively takes the nuclear option to handling command
    communication errors. Maybe the Chernobyl kind of nuclear (initiate damage
    control and start over), not Hiroshima (permanently wipe out everything in
    sight), but it is definitely *not* subtle.

Arguments:
    sig [in]    Signal to handle
*/
static void __server_reset_pipe( int sig )
{
    if( cmdd )
    {
        impact_printf_error( "%s: LOCAL SOCKET COMMUNICATION ERROR!\n", SP_MAIN_HEADER_NAMESPACE );
        
        impact_printf_debug( "%s: Attempting to restart command server ...\n", SP_MAIN_HEADER_NAMESPACE );
        simplecmd_free( cmdd );
        cmdd = simplecmd_init();
        
        if( cmdd ) impact_printf_debug( "%s: Command server restarted\n", SP_MAIN_HEADER_NAMESPACE );
        else impact_printf_debug( "%s: Failed to restart command server\n", SP_MAIN_HEADER_NAMESPACE );
    }
    else
    {
        impact_printf_error( "%s: Highly improbable! Received SIGPIPE with no active local sockets!\n", SP_MAIN_HEADER_NAMESPACE );
    }
}

/*
Safely handle SIGTERM by cleanly shutting down the server.

Remarks:
    Although this function is designed to handle the TERM signal, it doesn't
    actually care which signal you pass it. It will do its job and shutdown the
    server regardless.

Side Effects:
    This function exits the program.

Arguments:
    sig [in]    Signal to handle
*/
static void __server_shutdown( int sig )
{
    if( cmdd ) simplecmd_free( cmdd );
    if( httpd ) simplepost_free( httpd );
    exit( 0 );
}

/*
Safely handle SIGINT by cleanly shutting down the server.

Side Effects:
    This function exits the program.

Arguments:
    sig [in]    Signal to handle
*/
static void __server_terminal_interrupt( int sig )
{
    impact_printf_standard( "\n" ); // Terminate the ^C line
    __server_shutdown( SIGTERM );
}

/*
Initialize the server.
*/
int main( int argc, char * argv[] )
{
    simplearg_t args; // SimplePost arguments
    
    args = simplearg_init();
    if( args == NULL )
    {
        impact_printf_debug( "%s: %s: Failed to allocate memory for %s arguments instance\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC, SP_MAIN_DESCRIPTION );
        return 0;
    }
    simplearg_parse( args, argc, argv );
    impact_quiet = args->quiet;
    
    if( args->error ) return 1;
    if( args->help )
    {
        __print_help();
        goto no_error;
    }
    if( args->version )
    {
        __print_version();
        goto no_error;
    }
    
    if( args->pid )
    {
        simplecmd_list_t sclp; // List of SimplePost Command instances
        simplecmd_list_t p; // SimplePost Command instance matching the user-specified PID
        
        simplecmd_list_instances( &sclp );
        for( p = sclp; p; p = p->next )
            if( p->instance_pid == args->pid ) break;
        
        if( p == NULL )
        {
            impact_printf_error( "%s: Found no %s command instances with PID %d\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, args->pid );
            goto error;
        }
        
        simplecmd_list_free( sclp );
    }
    else if( args->new == 0 )
    {
        simplecmd_list_t sclp; // List of SimplePost Command instances
        pid_t lowest_pid = 0; // PID of the oldest SimplePost command instance matching our requirements
        
        simplecmd_list_instances( &sclp );
        for( simplecmd_list_t p = sclp; p; p = p->next )
        {
            if( p->instance_pid <= lowest_pid ) continue;
            
            if( args->address )
            {
                char * address; // Address of the server
                
                if( simplecmd_get_address( p->instance_pid, &address ) == 0 ) continue;
                
                int address_match = strcmp( args->address, address );
                free( address );
                if( address_match ) continue;
            }
            
            if( args->port )
            {
                unsigned short port; // Port the server is listening on
                
                port = simplecmd_get_port( p->instance_pid );
                if( port != args->port ) continue;
            }
            
            lowest_pid = p->instance_pid;
        }
        simplecmd_list_free( sclp );
        
        #ifdef DEBUG
        if( lowest_pid == 0 )
        {
            impact_printf_debug( "%s: No %s instances with open pipes", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION );
            if( args->address ) impact_printf_debug( " bound to ADDRESS %s", args->address );
            if( args->port ) impact_printf_debug( " listening on PORT %u", args->port );
            impact_printf_debug( "\n" );
        }
        #endif // DEBUG
        
        args->pid = lowest_pid;
    }
    
    if( args->pid )
    {
        char * address; // Destination server's address
        unsigned short port; // Destination server's port
        
        impact_printf_debug( "%s: Trying to connect to the %s instance with PID %d ...\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, args->pid );
        
        if( simplecmd_get_address( args->pid, &address ) == 0 )
        {
            impact_printf_error( "%s: Failed to get the ADDRESS of the %s instance with PID %d\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, args->pid );
            goto error;
        }
        
        port = simplecmd_get_port( args->pid );
        if( port == 0 )
        {
            impact_printf_error( "%s: Failed to get the PORT of the %s instance with PID %d\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, args->pid );
            free( address );
            goto error;
        }
        
        #ifdef DEBUG
        char * version; // Destination server's version
        
        if( simplecmd_get_version( args->pid, &version ) == 0 )
        {
            impact_printf_error( "%s: Failed to get the version of the %s instance with PID %d\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, args->pid );
            free( address );
            goto error;
        }
        
        impact_printf_debug( "%s: Serving FILESs on the %s %s instance with PID %d\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, version, args->pid );
        
        free( version );
        #endif // DEBUG
        
        for( simplefile_t p = args->files; p; p = p->next )
        {
            if( simplecmd_set_file( args->pid, p->file, p->count ) == 0 )
            {
                impact_printf_error( "%s: Failed to add FILE %s to the %s instance with PID %d\n", SP_MAIN_HEADER_NAMESPACE, p->file, SP_MAIN_DESCRIPTION, args->pid );
            }
            else
            {
                impact_printf_standard( "%s: Instance %d: Serving %s on http://%s:%u/%s ", SP_MAIN_HEADER_NAMESPACE, args->pid, p->file, address, port, p->file );
                switch( p->count )
                {
                    case 0:
                        impact_printf_standard( "indefinitely\n" );
                        break;
                    case 1:
                        impact_printf_standard( "exactly once\n" );
                        break;
                    default:
                        impact_printf_standard( "%u times\n", p->count );
                        break;
                }
            }
        }
        
        free( address );
        
        goto no_error;
    }
    
    httpd = simplepost_init();
    if( httpd == NULL )
    {
        impact_printf_debug( "%s: %s: Failed to allocate memory for %s HTTP server instance\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC, SP_MAIN_DESCRIPTION );
        goto error;
    }
    
    if( simplepost_bind( httpd, args->address, args->port ) == 0 ) goto error;
    for( simplefile_t p = args->files; p; p = p->next )
    {
        char * url; // URL of the file being served
        
        if( simplepost_serve_file( httpd, &url, p->file, NULL, p->count ) == 0 ) goto error;
        free( url );
    }
    
    signal( SIGPIPE, &__server_reset_pipe );
    signal( SIGINT, &__server_terminal_interrupt );
    signal( SIGTSTP, &__server_shutdown );
    signal( SIGQUIT, &__server_shutdown );
    signal( SIGTERM, &__server_shutdown );
    
    cmdd = simplecmd_init();
    if( cmdd == NULL )
    {
        impact_printf_debug( "%s: %s: Failed to allocate memory for %s command server instance\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC, SP_MAIN_DESCRIPTION );
        goto error;
    }
    
    simplecmd_activate( cmdd, httpd );
    simplepost_block_files( httpd );
    
    no_error:
    __server_shutdown( SIGTERM );
    if( args ) simplearg_free( args );
    return 0;
    
    error:
    __server_shutdown( SIGTERM );
    if( args ) simplearg_free( args );
    return 1;
}
