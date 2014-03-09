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

#include "simplearg.h"
#include "impact.h"
#include "config.h"

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

/*
Arguments header strings
*/
#define SP_ARGS_HEADER_NAMESPACE        "SimplePost::Arguments"
#define SP_ARGS_HEADER_INVLAID_OPTION   "Invalid Option"
#define SP_ARGS_HEADER_INVLAID_SYNTAX   "Invalid Syntax"

/*
Process the custom IP address argument.

Arguments:
    sap [in]    Instance to act on
    arg [in]    Argument string to process
*/
static void __set_address( simplearg_t sap, const char * arg )
{
    if( sap->address )
    {
        impact_printf_error( "%s: %s: ADDRESS already set\n", SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION );
        sap->error = 1;
    }
    else
    {
        sap->address = (char *) malloc( sizeof( char ) * (strlen( arg ) + 1) );
        if( sap->address == NULL )
        {
            impact_printf_debug( "%s: %s: Failed to allocate memory for the ADDRESS\n", SP_ARGS_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC );
            sap->error = 1;
            return;
        }
        
        strcpy( sap->address, arg );
        impact_printf_debug( "%s: Processed ADDRESS: %s\n", SP_ARGS_HEADER_NAMESPACE, sap->address );
    }
}

/*
Process the custom port argument.

Arguments:
    sap [in]    Instance to act on
    arg [in]    Argument string to process
*/
static void __set_port( simplearg_t sap, const char * arg )
{
    if( sap->port )
    {
        impact_printf_error( "%s: %s: PORT already set\n", SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION );
        sap->error = 1;
    }
    else
    {
        int i = atoi( arg );
        if( i < 1 )
        {
            impact_printf_error( "%s: %s: PORT must be between 1 and %u: %d\n", SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION, USHRT_MAX, i );
            sap->error = 1;
        }
        else
        {
            sap->port = (unsigned short) i;
            impact_printf_debug( "%s: Processed PORT: %u\n", SP_ARGS_HEADER_NAMESPACE, sap->port );
        }
    }
}

/*
Process the alternate instance argument.

Arguments:
    sap [in]    Instance to act on
    arg [in]    Argument string to process
*/
static void __set_pid( simplearg_t sap, const char * arg )
{
    if( sap->pid )
    {
        impact_printf_error( "%s: %s: PID already specified\n", SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION );
        sap->error = 1;
    }
    else if( sap->new )
    {
        impact_printf_error( "%s: %s: The \"process identifier\" and \"new\" arguments are mutually exclusive\n", SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION );
        sap->error = 1;
    }
    else
    {
        int i = atoi( arg );
        if( i <= 1 )
        {
            impact_printf_error( "%s: %s: PID must be a valid process identifier: %d\n", SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION, i );
            sap->error = 1;
        }
        else
        {
            sap->pid = (pid_t) i;
            impact_printf_debug( "%s: Processed PID: %d\n", SP_ARGS_HEADER_NAMESPACE, sap->pid );
        }
    }
}

/*
Process the new argument.

Arguments:
    sap [in]    Instance to act on
*/
static void __set_new( simplearg_t sap )
{
    if( sap->new )
    {
        impact_printf_error( "%s: %s: new argument may only be specified once\n", SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION );
        sap->error = 1;
    }
    else if( sap->pid )
    {
        impact_printf_error( "%s: %s: The \"process identifier\" and \"new\" arguments are mutually exclusive\n", SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION );
        sap->error = 1;
    }
    else
    {
        sap->new = 1;
        impact_printf_debug( "%s: Processed new argument: %u\n", SP_ARGS_HEADER_NAMESPACE, sap->new );
    }
}

/*
Process the quiet argument.

Arguments:
    sap [in]    Instance to act on
*/
static void __set_quiet( simplearg_t sap )
{
    if( sap->quiet )
    {
        impact_printf_error( "%s: %s: Standard output is already suppressed\n", SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION );
        sap->error = 1;
    }
    else
    {
        sap->quiet = 1;
        impact_printf_debug( "%s: Processed quiet argument: %u\n", SP_ARGS_HEADER_NAMESPACE, sap->quiet );
    }
}

/*
Process the help argument.

Arguments:
    sap [in]    Instance to act on
*/
static void __set_help( simplearg_t sap )
{
    sap->help = 1;
    impact_printf_debug( "%s: Processed help argument: %u\n", SP_ARGS_HEADER_NAMESPACE, sap->help );
}

/*
Process the version argument.

Arguments:
    sap [in]    Instance to act on
*/
static void __set_version( simplearg_t sap )
{
    sap->version = 1;
    impact_printf_debug( "%s: Processed version argument: %u\n", SP_ARGS_HEADER_NAMESPACE, sap->version );
}

/*
Get the last file in the list.

Arguments:
    sap [in]    Instance to act on
    new [in]    Add a new entry to the end of the list if appropriate?

Return Value:
    The last file in the list will be returned. If the list is empty or a valid
    entry does not exist, a new entry will be added to the end of the list.
*/
static simplefile_t __get_last_file( simplearg_t sap, short new )
{
    simplefile_t last; // Last file in the list
    
    if( sap->files == NULL )
    {
        if( new )
        {
            sap->files = (simplefile_t) malloc( sizeof( struct simplefile ) );
            if( sap->files == NULL ) return NULL;
            
            sap->files->count = 0;
            sap->files->file = NULL;
            
            sap->files->next = NULL;
            sap->files->prev = NULL;
        }
        else
        {
            return NULL;
        }
    }
    
    for( last = sap->files; last->next; last = last->next );
    
    if( last->file && new )
    {
        last->next = (simplefile_t) malloc( sizeof( struct simplefile ) );
        if( last->next == NULL ) return NULL;
        
        last->next->count = 0;
        last->next->file = NULL;
        
        last->next->next = NULL;
        last->next->prev = last;
        
        last = last->next;
    }
    
    return last;
}

/*
Process the count argument.

Arguments:
    sap [in]    Instance to act on
    arg [in]    Argument string to process
*/
static void __set_count( simplearg_t sap, const char * arg )
{
    simplefile_t last = __get_last_file( sap, 1 );
    if( last == NULL )
    {
        impact_printf_debug( "%s: %s: Failed to allocate memory for FILE\n", SP_ARGS_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC );
        sap->error = 1;
        return;
    }
    
    if( last->count )
    {
        impact_printf_error( "%s: %s: COUNT already set for FILE\n", SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION );
        sap->error = 1;
    }
    else
    {
        int i = atoi( arg );
        if( i < 0 )
        {
            impact_printf_error( "%s: %s: COUNT must be between 0 and %d: %d\n", SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION, INT_MAX, i );
            sap->error = 1;
        }
        else
        {
            last->count = (unsigned int) i;
            impact_printf_debug( "%s: Processed COUNT: %u\n", SP_ARGS_HEADER_NAMESPACE, last->count );
        }
    }
}

/*
Process the FILE argument.

Arguments:
    sap [in]    Instance to act on
    file [in]   File to add to the list of files to serve
*/
static void __set_file( simplearg_t sap, const char * file )
{
    struct stat file_status; // FILE status
    
    if( stat( file, &file_status ) == -1 )
    {
        impact_printf_error( "%s: %s: No such file or directory: %s\n", SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION, file );
        sap->error = 1;
    }
    else if( !(S_ISREG( file_status.st_mode ) || S_ISLNK( file_status.st_mode )) )
    {
        impact_printf_error( "%s: %s: Must of a regular file or link to a one: %s\n", SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION, file );
        sap->error = 1;
    }
    else
    {
        simplefile_t last = __get_last_file( sap, 1 );
        if( last == NULL )
        {
            impact_printf_debug( "%s: %s: Failed to allocate memory for FILE\n", SP_ARGS_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC );
            sap->error = 1;
            return;
        }
        
        if( last->file )
        {
            impact_printf_debug( "%s:%d => logic flaw or potential memory leak detected\n", __FILE__, __LINE__ );
            sap->error = 1;
            return;
        }
        
        last->file = (char *) malloc( sizeof( char ) * (strlen( file ) + 1) );
        if( last->file == NULL )
        {
            impact_printf_debug( "%s: %s: Failed to allocate memory for FILE\n", SP_ARGS_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC );
            sap->error = 1;
            return;
        }
        
        strcpy( last->file, file );
        impact_printf_debug( "%s: Processed FILE: %s\n", SP_ARGS_HEADER_NAMESPACE, last->file );
    }
}

/*
Process an invalid argument.

Arguments:
    sap [in]    Instance to act on
    arg [in]    Argument string to process
*/
static void __set_invalid( simplearg_t sap, const char * arg )
{
    impact_printf_error( "%s: %s: %s\n", SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION, arg );
    impact_printf_error( "Try `simplepost --help` for more information.\n" );
    sap->error = 1;
}

/*
Initialize a SimplePost Arguments instance.

Return Value:
    An initialized arguments instance will be returned unless an error
    occurred, in which case NULL will be returned instead.
*/
simplearg_t simplearg_init()
{
    simplearg_t sap = (simplearg_t) malloc( sizeof( struct simplearg ) );
    if( sap == NULL ) return NULL;
    
    sap->address = NULL;
    sap->port = 0;
    sap->pid = 0;
    
    sap->new = 0;
    sap->quiet = 0;
    sap->help = 0;
    sap->version = 0;
    sap->error = 0;
    
    sap->files = NULL;
    
    return sap;
}

/*
Free the given SimplePost Arguments instance.

Arguments:
    sap [in]    Instance to act on
*/
void simplearg_free( simplearg_t sap )
{
    if( sap->address ) free( sap->address );
    
    if( sap->files )
    {
        while( sap->files )
        {
            simplefile_t p = sap->files;
            sap->files = sap->files->next;
            if( p->file ) free( p->file );
            free( p );
        }
    }
    
    free( sap );
}

/*
Parse the arguments passed to this program.

Arguments:
    sap [in]    Instance to act on
    argc [in]   Number of arguments in the array below
    argv [in]   Array of arguments
*/
void simplearg_parse( simplearg_t sap, int argc, char * argv[] )
{
    if( argc < 2 )
    {
        impact_printf_error( "%s: %s\n", SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_SYNTAX );
        impact_printf_error( "Try `simplepost --help` for more information.\n" );
        sap->error = 1;
    }
    else
    {
        for( int i = 1; i < argc; i++ )
        {
            switch( argv[i][0] )
            {
                case '-':
                    switch( argv[i][1] )
                    {
                        case '-':
                            if( strncmp( "--count=", argv[i], 8 ) == 0 ) __set_count( sap, argv[i] + 8 );
                            else if( strncmp( "--address=", argv[i], 10 ) == 0 ) __set_address( sap, argv[i] + 10 );
                            else if( strncmp( "--ip-address=", argv[i], 13 ) == 0 ) __set_address( sap, argv[i] + 13 );
                            else if( strncmp( "--port=", argv[i], 7 ) == 0 ) __set_port( sap, argv[i] + 7 );
                            else if( strncmp( "--pid=", argv[i], 6 ) == 0 ) __set_pid( sap, argv[i] + 6 );
                            else if( strcmp( "--new", argv[i] ) == 0 ) __set_new( sap );
                            else if( strcmp( "--quiet", argv[i] ) == 0 ) __set_quiet( sap );
                            else if( strcmp( "--help", argv[i] ) == 0 ) __set_help( sap );
                            else if( strcmp( "--version", argv[i] ) == 0 ) __set_version( sap );
                            else __set_invalid( sap, argv[i] );
                            break;
                        case 'c':
                            __set_count( sap, (++i < argc) ? argv[i] : "" );
                            break;
                        case 'i':
                            __set_address( sap, (++i < argc) ? argv[i] : "" );
                            break;
                        case 'p':
                            __set_port( sap, (++i < argc) ? argv[i] : "" );
                            break;
                        case 'q':
                            __set_quiet( sap );
                            break;
                        default:
                            __set_invalid( sap, argv[i] );
                            break;
                    }
                    break;
                default:
                    __set_file( sap, argv[i] );
                    break;
            }
            
            if( sap->error || sap->help || sap->version ) return;
        }
        
        simplefile_t last = __get_last_file( sap, 0 );
        if( last == NULL )
        {
            impact_printf_error( "%s: %s: At least one FILE must be specified\n", SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_SYNTAX );
            sap->error = 1;
            return;
        }
        
        if( last->file == NULL )
        {
            impact_printf_error( "%s: %s: Last argument must be a FILE\n", SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_SYNTAX );
            
            if( sap->files == last ) sap->files = NULL;
            if( last->prev ) last->prev = NULL;
            free( last );
            
            sap->error = 1;
            return;
        }
    }
}
