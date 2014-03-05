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

#include <stdio.h>
#include <stdarg.h>

unsigned short impact_quiet = 0; // Don't print anything to stdout

/*
All of the functions below are nearly identical. They each take the same
arguments and return the same values as printf(). The only deviation from
printf() is that they will automatically not print anything and return zero if
the global variable impact_quiet is non-zero. They are differentiated from
each other by purpose, which is documented above each function definition.
*/

/*
Print to standard output.
*/
int impact_printf_standard( const char * format, ... )
{
    if( impact_quiet ) return 0;
    
    int ret; // printf() return value
    va_list args; // Arguments passed to this function
    
    va_start( args, format );
    ret = vfprintf( stdout, format, args );
    va_end( args );
    
    return ret;
}

/*
Print to standard error.
*/
int impact_printf_error( const char * format, ... )
{
    if( impact_quiet ) return 0;
    
    int ret; // printf() return value
    va_list args; // Arguments passed to this function
    
    va_start( args, format );
    ret = vfprintf( stderr, format, args );
    va_end( args );
    
    return ret;
}

#ifdef DEBUG

/*
Print debugging messages.
*/
int impact_printf_debug( const char * format, ... )
{
    if( impact_quiet ) return 0;
    
    int ret; // printf() return value
    va_list args; // Arguments passed to this function
    
    va_start( args, format );
    ret = vfprintf( stdout, format, args );
    va_end( args );
    
    return ret;
}

#endif // DEBUG
