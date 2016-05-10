/*
 * SimplePost - A Simple HTTP Server
 *
 * Copyright (C) 2012-2016 Karl Lenz.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have recieved a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include "simplepost.h"
#include "simplearg.h"
#include "simplecmd.h"
#include "impact.h"
#include "config.h"

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>

/// Local command handler instance
static simplecmd_t cmdd = NULL;

/// Web server instance
static simplepost_t httpd = NULL;

/*!
 * \brief Resolve the PID of the SimplePost instance to connect to.
 *
 * \param[inout] args Arguments passed to this program
 *
 * \return true if the PID was resolved successfully, false if not
 *
 * \note This function will return false only if no SimplePost instance exists
 * with the given PID or if the stated arguments are invalid (or contradictory)
 * and no PID can be resolved. Use the return value as a sanity check before
 * attempting to use the PID. Although the various simplecmd methods WILL
 * catch the error and return properly if an invalid PID is given, it should
 * never get that far. This method's error messages are far more user-friendly.
 *
 * \note Just because this function returns true does not necessarily mean
 * that there is a PID, even if one was set from the command line. If the
 * "--new" argument was given, this function will clear the PID if one was set
 * and return true.
 */
static bool __resolve_pid(simplearg_t args)
{
	if(args->options & SA_OPT_NEW)
	{
		args->pid = 0;
	}
	else if(args->pid)
	{
		pid_t pid = simplecmd_find_inst(args->address, args->port, args->pid);
		if(pid == 0)
		{
			impact_printf_error("%s: Found no %s command instance with PID %d\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, args->pid);
			return false;
		}
	}
	else
	{
		args->pid = simplecmd_find_inst(args->address, args->port, args->pid);
	}

	return true;
}

/*!
 * \brief Do we have a valid PID to a SimplePost instance?
 *
 * \param[in] args Arguments passed to this program
 *
 * \return true if the PID is valid, false if not
 *
 * \note This function should always be called after __resolve_pid()!
 */
static bool __is_pid_valid(const simplearg_t args)
{
	if(args->pid == 0)
	{
		if(args->address && args->port) impact_printf_error("%s: There is no %s instance bound to ADDRESS %s listening on PORT %hu\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, args->address, args->port);
		else if(args->address) impact_printf_error("%s: There is no %s instance bound to ADDRESS %s\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, args->address);
		else if(args->port) impact_printf_error("%s: There is no %s instance listening on PORT %hu\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, args->port);
		else impact_printf_error("%s: There are no other accessible %s instances\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION);
		return false;
	}
	else if(args->options & SA_OPT_NEW)
	{
		impact_printf_error("%s: No %s PID may be given with the '--new' option\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION);
		return false;
	}

	return true;
}

/*!
 * \brief Print the list of accessible SimplePost instances.
 *
 * \return true if all instances were enumerated successfully, false if not
 */
static bool __list_inst()
{
	simplecmd_list_t sclp; // List of SimplePost Command instances
	char* address;         // Address of the server
	unsigned short port;   // Port the server is listening on
	char* version;         // Server's version
	size_t failures = 0;   // Number of instances that we failed to query

	simplecmd_list_inst(&sclp);
	for(simplecmd_list_t p = sclp; p; p = p->next)
	{
		if(simplecmd_get_version(p->inst_pid, &version) == 0)
		{
			impact_printf_error("%s: Failed to get the version of the %s instance with PID %d\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, p->inst_pid);
			++failures;
			continue;
		}

		if(simplecmd_get_address(p->inst_pid, &address) == 0)
		{
			impact_printf_error("%s: Failed to get the address of the %s instance with PID %d\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, p->inst_pid);
			++failures;
			free(version);
			continue;
		}

		port = simplecmd_get_port(p->inst_pid);
		if(port == 0)
		{
			impact_printf_error("%s: Failed to get the port of the %s instance with PID %d\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, p->inst_pid);
			++failures;
			free(address);
			free(version);
			continue;
		}

		printf("[PID %d] %s %s serving files on %s:%hu\n", p->inst_pid, SP_MAIN_DESCRIPTION, version, address, port);
		free(address);
		free(version);
	}
	simplecmd_list_free(sclp);

	return (failures == 0);
}

/*!
 * \brief Print the list of files in the specified SimplePost instance.
 *
 * \param[in] args Arguments passed to this program
 *
 * \return true if all files being served by the specified instance were
 * enumerated successfully, false if not
 */
static bool __list_files(const simplearg_t args)
{
	simplepost_file_t files; // List of files being served by the server
	ssize_t count;           // Number of files being served

	count = simplecmd_get_files(args->pid, &files);
	if(count < 0)
	{
		impact_printf_error("%s: Failed to get the list of files being served by the %s instance with PID %d\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, args->pid);
		return false;
	}

	for(simplepost_file_t p = files; p; p = p->next)
	{
		printf("[PID %d] Serving %s on %s\n", args->pid, p->file, p->url);
	}

	simplepost_file_free(files);

	return true;
}

/*!
 * \brief Add new files to be served to another SimplePost instance.
 *
 * \param[in] args Arguments passed to this program
 *
 * \return true if all files were added to the specified instance successfully,
 * false if not
 */
static bool __add_to_other_inst(const simplearg_t args)
{
	char* address;       // Destination server's address
	unsigned short port; // Destination server's port

	impact_printf_debug("%s: Trying to connect to the %s instance with PID %d ...\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, args->pid);

	if(simplecmd_get_address(args->pid, &address) == 0)
	{
		impact_printf_error("%s: Failed to get the ADDRESS of the %s instance with PID %d\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, args->pid);
		return false;
	}

	port = simplecmd_get_port(args->pid);
	if(port == 0)
	{
		impact_printf_error("%s: Failed to get the PORT of the %s instance with PID %d\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, args->pid);
		free(address);
		return false;
	}

	#ifdef DEBUG
	char* version; // Destination server's version

	if(simplecmd_get_version(args->pid, &version) == 0)
	{
		impact_printf_error("%s: Failed to get the version of the %s instance with PID %d\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, args->pid);
		free(address);
		return false;
	}

	impact_printf_debug("%s: Serving FILESs on the %s %s instance with PID %d\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, version, args->pid);

	free(version);
	#endif // DEBUG

	for(simplefile_t p = args->files; p; p = p->next)
	{
		if(simplecmd_set_file(args->pid, p->file, p->count) == false)
		{
			impact_printf_error("%s: Failed to add FILE %s to the %s instance with PID %d\n", SP_MAIN_HEADER_NAMESPACE, p->file, SP_MAIN_DESCRIPTION, args->pid);
		}
		else
		{
			impact_printf_standard("%s: Instance %d: Serving %s on http://%s:%u/%s ", SP_MAIN_HEADER_NAMESPACE, args->pid, p->file, address, port, p->file);
			switch(p->count)
			{
				case 0:
					impact_printf_standard("indefinitely\n");
					break;

				case 1:
					impact_printf_standard("exactly once\n");
					break;

				default:
					impact_printf_standard("%u times\n", p->count);
					break;
			}
		}
	}

	free(address);

	return true;
}

/*!
 * \brief Add the files to our SimplePost instance and start the HTTP server.
 *
 * \param[in] args Arguments passed to this program
 *
 * \return true if the HTTP server was started successfully, false if not
 */
static bool __start_httpd(const simplearg_t args)
{
	httpd = simplepost_init();
	if(httpd == NULL)
	{
		impact_printf_debug("%s: %s: Failed to allocate memory for %s HTTP server instance\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC, SP_MAIN_DESCRIPTION);
		return false;
	}

	if(simplepost_bind(httpd, args->address, args->port) == 0) return false;
	for(simplefile_t p = args->files; p; p = p->next)
	{
		char* url; // URL of the file being served

		if(simplepost_serve_file(httpd, &url, p->file, NULL, p->count) == 0) return false;
		free(url);
	}

	return true;
}

/*!
 * \brief Print our help information.
 */
static void __print_help()
{
	printf("Usage: %s [GLOBAL_OPTIONS] [FILE_OPTIONS] FILE\n\n", SP_MAIN_SHORT_NAME);
	printf("Serve FILE COUNT times via HTTP on port PORT with IP address ADDRESS.\n");
	printf("Multiple FILE and FILE_OPTIONS may be specified in sequence after GLOBAL_OPTIONS.\n\n");
	printf("Global Options:\n");
	printf("  -i, --address=ADDRESS    use ADDRESS as the server's ip address\n");
	printf("  -p, --port=PORT          bind to PORT on the local machine\n");
	printf("                           a random port will be chosen if this is not specified\n");
	printf("      --pid=PID            act on the instance of this program with process identifier PID\n");
	printf("                           by default the existing instance matching ADDRESS and PORT will be used if possible\n");
	printf("      --new                act exclusively on the current instance of this program\n");
	printf("                           this option and --pid are mutually exclusive\n");
	printf("      --daemon             fork to the background and run as a system daemon\n");
	printf("  -l, --list=LTYPE         list the requested LTYPE of information about an instance of this program\n");
	printf("                           LTYPE=i,inst,instances    list all server instances that we can connect to\n");
	printf("                           LTYPE=f,files             list all files being served by the selected server instance\n");
	printf("  -q, --quiet              do not print anything to standard output\n");
	printf("      --help               display this help and exit\n");
	printf("      --version            output version information and exit\n\n");
	printf("File Options:\n");
	printf("  -c, --count=COUNT        serve the file COUNT times\n");
	printf("                           by default FILE will be served until the server is shut down\n\n");
	printf("Examples:\n");
	printf("  %s --list=instances              List all available instances of this program\n", SP_MAIN_SHORT_NAME);
	printf("  %s -p 80 -q -c 1 FILE            Serve FILE on port 80 one time.\n", SP_MAIN_SHORT_NAME);
	printf("  %s --pid=99031 --count=2 FILE    Serve FILE twice on the instance of simplepost with the process identifier 99031.\n", SP_MAIN_SHORT_NAME);
	printf("  %s FILE                          Serve FILE on a random port until SIGTERM is received.\n\n", SP_MAIN_SHORT_NAME);
}

/*!
 * \brief Print our version information.
 */
static void __print_version()
{
	printf("%s %s\n", SP_MAIN_DESCRIPTION, SP_MAIN_VERSION);
	printf("%s\n", SP_MAIN_COPYRIGHT);
	printf("License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl.html>.\n");
	printf("This is free software: you are free to change and redistribute it.\n");
	printf("There is NO WARRANTY, to the extent permitted by law.\n");
}

/*!
 * \brief Safely handle SIGPIPE by completely resetting the command server.
 *
 * \note This function effectively takes the nuclear option to handling
 * command communication errors. Maybe the Chernobyl kind of nuclear (initiate
 * damage control and start over), not Hiroshima (permanently wipe out
 * everything in sight), but it is definitely *not* subtle.
 *
 * \param[in] sig Signal to handle
 */
static void __server_reset_pipe(int sig)
{
	// Unused parameters
	(void) sig;

	if(cmdd)
	{
		impact_printf_error("%s: LOCAL SOCKET COMMUNICATION ERROR!\n", SP_MAIN_HEADER_NAMESPACE);

		impact_printf_debug("%s: Attempting to restart command server ...\n", SP_MAIN_HEADER_NAMESPACE);
		simplecmd_free(cmdd);
		cmdd = simplecmd_init();

		if(cmdd) impact_printf_debug("%s: Command server restarted\n", SP_MAIN_HEADER_NAMESPACE);
		else impact_printf_debug("%s: Failed to restart command server\n", SP_MAIN_HEADER_NAMESPACE);
	}
	else
	{
		impact_printf_error("%s: Highly improbable! Received SIGPIPE with no active local sockets!\n", SP_MAIN_HEADER_NAMESPACE);
	}
}

/*!
 * \brief Safely handle SIGTERM by cleanly shutting down the server.
 *
 * \note Although this function is designed to handle the TERM signal, it
 * doesn't actually care which signal you pass it. It will do its job and
 * shutdown the server regardless.
 *
 * \warning This function exits the program!
 *
 * \param[in] sig Signal to handle
 */
static void __server_shutdown(int sig)
{
	// Unused parameters
	(void) sig;

	simplecmd_free(cmdd);
	simplepost_free(httpd);
	exit(0);
}

/*!
 * \brief Safely handle SIGINT by cleanly shutting down the server.
 *
 * \warning This function exits the program!
 *
 * \param[in] sig Signal to handle
 */
static void __server_terminal_interrupt(int sig)
{
	impact_printf_standard("\n"); // Terminate the ^C line
	__server_shutdown(sig);
}

/*!
 * \brief Initialize the server.
 */
int main(int argc, char* argv[])
{
	simplearg_t args; // SimplePost arguments

	args = simplearg_init();
	if(args == NULL)
	{
		impact_printf_debug("%s: %s: Failed to allocate memory for %s arguments instance\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC, SP_MAIN_DESCRIPTION);
		return 0;
	}
	simplearg_parse(args, argc, argv);
	impact_quiet = (args->options & SA_OPT_QUIET) ? true : false;

	if(args->options & SA_OPT_ERROR) return 1;
	if(args->actions)
	{
		if(args->actions & SA_ACT_HELP)
		{
			__print_help();
			goto no_error;
		}
		else if(args->actions & SA_ACT_VERSION)
		{
			__print_version();
			goto no_error;
		}
		else if(args->actions & SA_ACT_LIST_INST)
		{
			if(__list_inst()) goto no_error;
			else goto error;
		}
		else if(args->actions & SA_ACT_LIST_FILES)
		{
			if(__resolve_pid(args) == false) goto error;
			if(__is_pid_valid(args) == false) goto error;
			if(__list_files(args)) goto no_error;
			else goto error;
		}
		else
		{
			impact_printf_error("%s: BUG! Failed to handle action 0x%02X\n", __PRETTY_FUNCTION__, args->actions);
			goto error;
		}
	}

	if(__resolve_pid(args) == false) goto error;

	if(args->pid)
	{
		if(__add_to_other_inst(args)) goto no_error;
		else goto error;
	}

	if(args->options & SA_OPT_DAEMON)
	{
		impact_printf_standard("%s: Daemonizing and forking to the background\n", SP_MAIN_HEADER_NAMESPACE);
		if(daemon(1, 0) == -1)
		{
			impact_printf_error("%s: Failed to daemonize %s: %s\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_DESCRIPTION, strerror(errno));
			goto error;
		}
	}

	if(__start_httpd(args) == false) goto error;

	signal(SIGPIPE, &__server_reset_pipe);
	signal(SIGINT, &__server_terminal_interrupt);
	signal(SIGTSTP, &__server_shutdown);
	signal(SIGQUIT, &__server_shutdown);
	signal(SIGTERM, &__server_shutdown);

	cmdd = simplecmd_init();
	if(cmdd == NULL)
	{
		impact_printf_debug("%s: %s: Failed to allocate memory for %s command server instance\n", SP_MAIN_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC, SP_MAIN_DESCRIPTION);
		goto error;
	}

	simplecmd_activate(cmdd, httpd);
	simplepost_block_files(httpd);

no_error:
	simplecmd_free(cmdd);
	simplepost_free(httpd);
	simplearg_free(args);
	return 0;

error:
	simplecmd_free(cmdd);
	simplepost_free(httpd);
	simplearg_free(args);
	return 1;
}
