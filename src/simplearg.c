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
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include "simplearg.h"
#include "impact.h"
#include "config.h"

#include <sys/stat.h>
#include <stdbool.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <getopt.h>
#include <stdio.h>

/// Arguments namespace header
#define SP_ARGS_HEADER_NAMESPACE      "SimplePost::Arguments"

/// Invalid option error string
#define SP_ARGS_HEADER_INVLAID_OPTION "Invalid Option"

/// Invalid syntax error string
#define SP_ARGS_HEADER_INVLAID_SYNTAX "Invalid Syntax"

/*!
 * \brief Process an option missing its required argument.
 *
 * \param[out] sap   Instance to act on
 * \param[in] optstr Unprocessed string containing the option that is missing
 *                   its argument
 */
static void __set_missing(simplearg_t sap, const char* optstr)
{
	impact(0, "%s: %s: '%s' requires an argument\n",
		SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_SYNTAX,
		optstr);
	impact(0, "Try 'simplepost --help' for more information.\n");
	sap->options |= SA_OPT_ERROR;
}

/*!
 * \brief Process an invalid option.
 *
 * \param[out] sap   Instance to act on
 * \param[in] optstr Unprocessed string containing the invalid option
 */
static void __set_invalid(simplearg_t sap, const char* optstr)
{
	impact(0, "%s: %s: '%s'\n",
		SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION,
		optstr);
	impact(0, "Try 'simplepost --help' for more information.\n");
	sap->options |= SA_OPT_ERROR;
}

/*!
 * \brief Process the custom IP address argument.
 *
 * \param[inout] sap Instance to act on
 * \param[in] optstr String containing the address option
 * \param[in] arg    String containing the raw address to process
 */
static void __set_address(simplearg_t sap, const char* optstr, const char* arg)
{
	if(sap->address)
	{
		impact(0, "%s: %s: ADDRESS already set\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION);
		sap->options |= SA_OPT_ERROR;
		return;
	}

	if(arg == NULL)
	{
		impact(0, "%s:%d: BUG! No ADDRESS given to process\n",
			__PRETTY_FUNCTION__, __LINE__);
		sap->options |= SA_OPT_ERROR;
		return;
	}

	if(arg[0] == '-')
	{
		__set_missing(sap, optstr);
		return;
	}

	sap->address = (char*) malloc(sizeof(char) * (strlen(arg) + 1));
	if(sap->address == NULL)
	{
		impact(0, "%s: %s: Failed to allocate memory for the ADDRESS\n",
			SP_ARGS_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC);
		sap->options |= SA_OPT_ERROR;
		return;
	}

	strcpy(sap->address, arg);
	#ifdef DEBUG
	impact(1, "%s: Processed ADDRESS: %s\n",
		SP_ARGS_HEADER_NAMESPACE,
		sap->address);
	#endif // DEBUG
}

/*!
 * \brief Process the custom port argument.
 *
 * \param[inout] sap Instance to act on
 * \param[in] optstr String containing the port option
 * \param[in] arg    Argument string to process
 */
static void __set_port(simplearg_t sap, const char* optstr, const char* arg)
{
	if(sap->port)
	{
		impact(0, "%s: %s: PORT already set\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION);
		sap->options |= SA_OPT_ERROR;
		return;
	}

	if(arg == NULL)
	{
		impact(0, "%s:%d: BUG! No PORT given to process\n",
			__PRETTY_FUNCTION__, __LINE__);
		sap->options |= SA_OPT_ERROR;
		return;
	}

	if(arg[0] == '-')
	{
		__set_missing(sap, optstr);
		return;
	}

	int i;
	if(sscanf(arg, "%d", &i) != 1)
	{
		impact(0, "%s: %s: PORT must be a positive integer: %s\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION,
			arg);
		sap->options |= SA_OPT_ERROR;
	}
	else if(i < 1 || i > USHRT_MAX)
	{
		impact(0, "%s: %s: PORT must be between 1 and %hu: %d\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION,
			USHRT_MAX, i);
		sap->options |= SA_OPT_ERROR;
	}
	else
	{
		sap->port = (unsigned short) i;
		#ifdef DEBUG
		impact(1, "%s: Processed PORT: %hu\n",
			SP_ARGS_HEADER_NAMESPACE,
			sap->port);
		#endif // DEBUG
	}
}

/*!
 * \brief Process the list argument.
 *
 * \param[inout] sap Instance to act on
 * \param[in] optstr String containing the port option
 * \param[in] arg    Argument string to process
 */
static void __set_list(simplearg_t sap, const char* optstr, const char* arg)
{
	if(sap->actions & (SA_ACT_LIST_INST | SA_ACT_LIST_FILES))
	{
		impact(0, "%s: %s: LTYPE already set\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION);
		sap->options |= SA_OPT_ERROR;
		return;
	}

	if(arg == NULL)
	{
		impact(0, "%s:%d: BUG! No LTYPE given to process\n",
			__PRETTY_FUNCTION__, __LINE__);
		sap->options |= SA_OPT_ERROR;
		return;
	}

	if(arg[0] == '-')
	{
		__set_missing(sap, optstr);
		return;
	}

	if(strcmp(arg, "i") == 0 || strcmp(arg, "inst") == 0 || strcmp(arg, "instances") == 0)
	{
		sap->actions |= SA_ACT_LIST_INST;
		#ifdef DEBUG
		impact(1, "%s: Processed LTYPE: 0x%02X\n",
			SP_ARGS_HEADER_NAMESPACE,
			sap->actions & SA_ACT_LIST_INST);
		#endif // DEBUG
	}
	else if(strcmp(arg, "f") == 0 || strcmp(arg, "files") == 0)
	{
		sap->actions |= SA_ACT_LIST_FILES;
		#ifdef DEBUG
		impact(1, "%s: Processed LTYPE: 0x%02X\n",
			SP_ARGS_HEADER_NAMESPACE,
			sap->actions & SA_ACT_LIST_FILES);
		#endif // DEBUG
	}
	else
	{
		impact(0, "%s: %s: Invalid LTYPE: %s\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION,
			arg);
		sap->options |= SA_OPT_ERROR;
	}
}

/*!
 * \brief Process the alternate instance argument.
 *
 * \param[inout] sap Instance to act on
 * \param[in] optstr String containing the PID option
 * \param[in] arg    Argument string to process
 */
static void __set_pid(simplearg_t sap, const char* optstr, const char* arg)
{
	if(sap->pid)
	{
		impact(0, "%s: %s: PID already specified\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION);
		sap->options |= SA_OPT_ERROR;
		return;
	}

	if(sap->options & SA_OPT_NEW)
	{
		impact(0, "%s: %s: The \"process identifier\" and \"new\" options are mutually exclusive\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION);
		sap->options |= SA_OPT_ERROR;
		return;
	}

	if(arg == NULL)
	{
		impact(0, "%s:%d: BUG! No PID given to process\n",
			__PRETTY_FUNCTION__, __LINE__);
		sap->options |= SA_OPT_ERROR;
		return;
	}

	if(arg[0] == '-')
	{
		__set_missing(sap, optstr);
		return;
	}

	int i;
	if(sscanf(arg, "%d", &i) != 1)
	{
		impact(0, "%s: %s: PID must be a positive integer: %s\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION,
			arg);
		sap->options |= SA_OPT_ERROR;
	}
	else if(i <= 1)
	{
		impact(0, "%s: %s: PID must be a valid process identifier: %d\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION,
			i);
		sap->options |= SA_OPT_ERROR;
	}
	else
	{
		sap->pid = (pid_t) i;
		#ifdef DEBUG
		impact(1, "%s: Processed PID: %d\n",
			SP_ARGS_HEADER_NAMESPACE,
			sap->pid);
		#endif // DEBUG
	}
}

/*!
 * \brief Process the new argument.
 *
 * \param[inout] sap Instance to act on
 */
static void __set_new(simplearg_t sap)
{
	if(sap->options & SA_OPT_NEW)
	{
		impact(0, "%s: %s: new argument may only be specified once\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION);
		sap->options |= SA_OPT_ERROR;
	}
	else if(sap->pid)
	{
		impact(0, "%s: %s: The \"process identifier\" and \"new\" options are mutually exclusive\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION);
		sap->options |= SA_OPT_ERROR;
	}
	else if(sap->actions & SA_ACT_SHUTDOWN)
	{
		impact(0, "%s: %s: The \"new\" and \"kill\" options are mutually exclusive\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION);
		sap->options |= SA_OPT_ERROR;
	}
	else
	{
		sap->options |= SA_OPT_NEW;
		#ifdef DEBUG
		impact(1, "%s: Processed new argument: 0x%02X\n",
			SP_ARGS_HEADER_NAMESPACE,
			sap->options & SA_OPT_NEW);
		#endif // DEBUG
	}
}

/*!
 * \brief Process the kill argument.
 *
 * \param[inout] sap Instance to act on
 */
static void __set_shutdown(simplearg_t sap)
{
	if(sap->actions & SA_ACT_SHUTDOWN)
	{
		impact(0, "%s: %s: kill argument may only be specified once\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION);
		sap->options |= SA_OPT_ERROR;
	}
	else if(sap->options & SA_OPT_NEW)
	{
		impact(0, "%s: %s: The \"new\" and \"kill\" options are mutually exclusive\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION);
		sap->options |= SA_OPT_ERROR;
	}
	else
	{
		sap->actions |= SA_ACT_SHUTDOWN;
		#ifdef DEBUG
		impact(1, "%s: Processed kill argument: 0x%02X\n",
			SP_ARGS_HEADER_NAMESPACE,
			sap->actions & SA_ACT_SHUTDOWN);
		#endif // DEBUG
	}
}

/*!
 * \brief Process the daemon argument.
 *
 * \param[inout] sap Instance to act on
 */
static void __set_daemon(simplearg_t sap)
{
	if(sap->options & SA_OPT_DAEMON)
	{
		impact(0, "%s: %s: daemon argument may only be specified once\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION);
		sap->options |= SA_OPT_ERROR;
	}
	else
	{
		sap->options |= SA_OPT_DAEMON;
		#ifdef DEBUG
		impact(1, "%s: Processed daemon argument: 0x%02X\n",
			SP_ARGS_HEADER_NAMESPACE,
			sap->options & SA_OPT_DAEMON);
		#endif // DEBUG
	}
}

/*!
 * \brief Process the quiet argument.
 *
 * \param[inout] sap Instance to act on
 */
static void __set_quiet(simplearg_t sap)
{
	if(sap->options & SA_OPT_QUIET)
	{
		impact(0, "%s: %s: The quiet option is already set\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION);
		sap->options |= SA_OPT_ERROR;
		return;
	}

	sap->verbosity = -1;

	sap->options |= SA_OPT_QUIET;
	#ifdef DEBUG
	impact(1, "%s: Processed quiet argument: 0x%02X\n",
		SP_ARGS_HEADER_NAMESPACE,
		sap->options & SA_OPT_QUIET);
	#endif // DEBUG
}

/*!
 * \brief Process the suppress argument.
 *
 * \param[inout] sap Instance to act on
 */
static void __set_suppress(simplearg_t sap)
{
	if(sap->options & SA_OPT_SUPPRESS)
	{
		impact(0, "%s: %s: Messages are already suppressed\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION);
		sap->options |= SA_OPT_ERROR;
	}

	if(!(sap->options & SA_OPT_QUIET))
	{
		sap->verbosity = 0;
	}

	sap->options |= SA_OPT_SUPPRESS;
	#ifdef DEBUG
	impact(1, "%s: Processed suppress argument: 0x%02X\n",
		SP_ARGS_HEADER_NAMESPACE,
		sap->options & SA_OPT_SUPPRESS);
	#endif // DEBUG
}

/*!
 * \brief Process the verbose argument.
 *
 * \param[inout] sap Instance to act on
 */
static void __set_verbose(simplearg_t sap)
{
	if(!(sap->options & (SA_OPT_QUIET | SA_OPT_SUPPRESS)))
	{
		++(sap->verbosity);
	}

	#ifdef DEBUG
	impact(1, "%s: Processed verbose argument: %d\n",
		SP_ARGS_HEADER_NAMESPACE,
		sap->verbosity);
	#endif // DEBUG
}

/*!
 * \brief Process the help argument.
 *
 * \param[out] sap Instance to act on
 */
static void __set_help(simplearg_t sap)
{
	sap->actions |= SA_ACT_HELP;
	#ifdef DEBUG
	impact(1, "%s: Processed help argument: 0x%02X\n",
		SP_ARGS_HEADER_NAMESPACE,
		sap->actions & SA_ACT_HELP);
	#endif // DEBUG
}

/*!
 * \brief Process the version argument.
 *
 * \param[out] sap Instance to act on
 */
static void __set_version(simplearg_t sap)
{
	sap->actions |= SA_ACT_VERSION;
	#ifdef DEBUG
	impact(1, "%s: Processed version argument: 0x%02X\n",
		SP_ARGS_HEADER_NAMESPACE,
		sap->actions & SA_ACT_VERSION);
	#endif // DEBUG
}

/*!
 * \brief Get the last file in the list.
 *
 * \param[inout] sap Instance to act on
 * \param[in] new    Add a new entry to the end of the list if necessary?
 *
 * \return the last file in the list. If the list is empty or a valid entry
 * does not exist, a new entry will be added to the end of the list.
 */
static simplefile_t __get_last_file(simplearg_t sap, short new)
{
	simplefile_t last; // Last file in the list

	if(sap->files == NULL)
	{
		if(new)
		{
			sap->files = (simplefile_t) malloc(sizeof(struct simplefile));
			if(sap->files == NULL) return NULL;

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

	for(last = sap->files; last->next; last = last->next);

	if(last->file && new)
	{
		last->next = (simplefile_t) malloc(sizeof(struct simplefile));
		if(last->next == NULL) return NULL;

		last->next->count = 0;
		last->next->file = NULL;

		last->next->next = NULL;
		last->next->prev = last;

		last = last->next;
	}

	return last;
}

/*!
 * \brief Process the count argument.
 *
 * \param[inout] sap Instance to act on
 * \param[in] optstr String containing the count option
 * \param[in] arg    Argument string to process
 */
static void __set_count(simplearg_t sap, const char* optstr, const char* arg)
{
	simplefile_t last = __get_last_file(sap, 1);
	if(last == NULL)
	{
		impact(0, "%s: %s: Failed to allocate memory for FILE\n",
			SP_ARGS_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC);
		sap->options |= SA_OPT_ERROR;
		return;
	}

	if(last->count)
	{
		impact(0, "%s: %s: COUNT already set for FILE\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION);
		sap->options |= SA_OPT_ERROR;
		return;
	}

	if(arg == NULL)
	{
		impact(0, "%s:%d: BUG! No COUNT given to process\n",
			__PRETTY_FUNCTION__, __LINE__);
		sap->options |= SA_OPT_ERROR;
		return;
	}

	if(arg[0] == '-')
	{
		__set_missing(sap, optstr);
		return;
	}

	int i;
	if(sscanf(arg, "%d", &i) != 1)
	{
		impact(0, "%s: %s: COUNT must be a positive integer: %s\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION,
			arg);
		sap->options |= SA_OPT_ERROR;
	}
	else if(i < 0)
	{
		impact(0, "%s: %s: COUNT must be between 0 and %d: %d\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION,
			INT_MAX, i);
		sap->options |= SA_OPT_ERROR;
	}
	else
	{
		last->count = (unsigned int) i;
		#ifdef DEBUG
		impact(1, "%s: Processed COUNT: %u\n",
			SP_ARGS_HEADER_NAMESPACE,
			last->count);
		#endif // DEBUG
	}
}

/*!
 * Process the FILE argument.
 *
 * \param[inout] sap Instance to act on
 * \param[in] file   File to add to the list of files to serve
 */
static void __set_file(simplearg_t sap, const char* file)
{
	struct stat file_status; // FILE status

	if(stat(file, &file_status) == -1)
	{
		/* Technically this extra sanity check is not necessary, but most
		 * likely if the file does not exist and starts with a "-", it was
		 * really intended to be an argument. This just helps to make our
		 * error messages slightly more consistent.
		 */
		if(file[0] == '-')
		{
			__set_invalid(sap, file);
		}
		else
		{
			impact(0, "%s: %s: No such file or directory: %s\n",
				SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION,
				file);
			sap->options |= SA_OPT_ERROR;
		}
		return;
	}

	if(!(S_ISREG(file_status.st_mode) || S_ISLNK(file_status.st_mode)))
	{
		impact(0, "%s: %s: Must be a regular file or link to a one: %s\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_OPTION,
			file);
		sap->options |= SA_OPT_ERROR;
		return;
	}

	simplefile_t last = __get_last_file(sap, 1);
	if(last == NULL)
	{
		impact(0, "%s: %s: Failed to allocate memory for FILE\n",
			SP_ARGS_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC);
		sap->options |= SA_OPT_ERROR;
		return;
	}

	if(last->file)
	{
		impact(0, "%s:%d: BUG! Logic flaw or potential memory leak detected\n",
			__PRETTY_FUNCTION__, __LINE__);
		sap->options |= SA_OPT_ERROR;
		return;
	}

	last->file = (char*) malloc(sizeof(char) * (strlen(file) + 1));
	if(last->file == NULL)
	{
		impact(0, "%s: %s: Failed to allocate memory for FILE\n",
			SP_ARGS_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC);
		sap->options |= SA_OPT_ERROR;
		return;
	}

	strcpy(last->file, file);
	#ifdef DEBUG
	impact(1, "%s: Processed FILE: %s\n",
		SP_ARGS_HEADER_NAMESPACE,
		last->file);
	#endif // DEBUG
}

/*!
 * \brief Is the given option defined the long options array?
 *
 * This function determines if the given option is defined in the long options
 * array. Typically it would not be necessary if we were only using one set of
 * options with getopt_long(), but unfortunately with multiple sets of options
 * it is difficult to tell if getopt_long() encountered a real syntax error or
 * if it encountered an option in a different set of options when it returns
 * '?'. The intent of this function is to differentiate those cases. If it
 * returns true, then getopt_long() found a true syntax error. If it returns
 * false, then getopt_long() just encountered an option that it doesn't
 * understand, which may be defined in another set of options.
 *
 * \warning This function assumes that all short options being processed have
 * a long option equivalent (and are therefore present in the long options
 * array). If that is not the case, the result of this function will only be
 * meaningful when it returns true - you will not be able to definitively
 * conclude that the option is not defined if it returns false.
 *
 * \param[in] longopts Canonical array of options to search
 * \param[in] opt
 * \parblock
 * Short option code to find
 *
 * If there is no short option, make this parameter '-' to indicate that we
 * should be looking for a long option instead.
 * \endparblock
 * \param[in] arg
 * \parblock
 * Complete argument from the arguments array that was processed into the
 * given short option code
 *
 * This parameter is only used for processing long options which have no short
 * option equivalent. However that behavior is subject to change, so it is
 * strongly recommended that you pass in a valid string for this parameter
 * every time.
 * \endparblock
 *
 * \return true if the option is defined, false if not
 */
static bool __is_longopt(const struct option* longopts, int opt, const char* arg)
{
	if(opt == '-')
	{
		if(arg == NULL || arg[0] != '-' || arg[1] != '-')
		{
			return false;
		}

		for(int i = 0; longopts[i].name; ++i)
		{
			if(strcmp(longopts[i].name, arg + 2) == 0)
			{
				return true;
			}
		}
	}
	else
	{
		for(int i = 0; longopts[i].name; ++i)
		{
			if(longopts[i].flag == NULL && longopts[i].val == opt)
			{
				return true;
			}
		}
	}

	return false;
}

/*!
 * \brief Parse the global options passed to this program.
 *
 * The "global options" parsed by this function apply to all actions performed
 * by this program. Regardless of the other command line arguments, these
 * should be accepted.
 *
 * \note These options should always be parsed first; they may affect the way
 * other options are interpreted or the actions taken.
 *
 * \param[out] sap Instance to act on
 * \param[in] argc Number of arguments in the array below
 * \param[in] argv Array of arguments
 *
 * \return the index of the next argument to process in the argv array.
 *
 * \note The return value of this function does not indicate if there was a
 * parsing error. Check for SA_OPT_ERROR after it returns to determine that.
 */
static int __parse_global_opts(simplearg_t sap, int argc, char* argv[])
{
	int have_pid = 0;     // Is the pid argument set?
	int have_new = 0;     // Is the new argument set?
	int have_daemon = 0;  // Is the daemon argument set?
	int have_help = 0;    // Is the help argument set?
	int have_version = 0; // Is the version argument set?

	int opt_index = 0; // Index of the next option to process in argv
	int opt_long;      // Index of the current option in global_longopts
	int opt_arg;       // Short option code being processed

	struct option global_longopts[] =
	{
		{"address",     required_argument, NULL,        'i'},
		{"port",        required_argument, NULL,        'p'},
		{"pid",         required_argument, &have_pid,     1},
		{"new",         no_argument,       &have_new,     1},
		{"kill",        no_argument,       NULL,        'k'},
		{"daemon",      no_argument,       &have_daemon,  1},
		{"list",        required_argument, NULL,        'l'},
		{"quiet",       no_argument,       NULL,        'q'},
		{"no-messages", no_argument,       NULL,        's'},
		{"verbose",     no_argument,       NULL,        'v'},
		{"help",        no_argument,       &have_help,    1},
		{"version",     no_argument,       &have_version, 1},
		{0, 0, 0, 0}
	};

	if(sap->options & SA_OPT_ERROR)
	{
		impact(0, "%s:%d: BUG! Not processing any options because simplearg is already in error state\n",
			__PRETTY_FUNCTION__, __LINE__);
		return opt_index;
	}

	// Reset getopt's place in the argument array.
	optind = 1;

	while(!(sap->options & SA_OPT_ERROR))
	{
		/* getopt() always skips the first argument in the argv array because
		 * it is assumed to be the program name. Since that is not the case in
		 * this context, arbitrarily adjust our array to accommodate it.
		 */
		opt_arg = getopt_long(argc + 1, argv - 1, "i:p:kl:qsv", global_longopts, &opt_long);

		if(optind < 1 || (optind - 1) < opt_index)
		{
			impact(0, "%s:%d: BUG! optind = %d, opt_index = %d, argc = %d\n",
				__PRETTY_FUNCTION__, __LINE__,
				optind, opt_index, argc);
			sap->options |= SA_OPT_ERROR;
			break;
		}

		switch(opt_arg)
		{
			case -1:
				// There are no more options to process.
				return opt_index;

			case 0:
				if(global_longopts[opt_long].flag == &have_pid)
				{
					__set_pid(sap, argv[opt_index], optarg);
				}
				else if(global_longopts[opt_long].flag == &have_new)
				{
					__set_new(sap);
				}
				else if(global_longopts[opt_long].flag == &have_daemon)
				{
					__set_daemon(sap);
				}
				else if(global_longopts[opt_long].flag == &have_help)
				{
					__set_help(sap);
				}
				else if(global_longopts[opt_long].flag == &have_version)
				{
					__set_version(sap);
				}
				else
				{
					__set_invalid(sap, argv[opt_index]);
				}
				break;

			case 'i':
				__set_address(sap, argv[opt_index], optarg);
				break;

			case 'p':
				__set_port(sap, argv[opt_index], optarg);
				break;

			case 'k':
				__set_shutdown(sap);
				break;

			case 'l':
				__set_list(sap, argv[opt_index], optarg);
				break;

			case 'q':
				__set_quiet(sap);
				break;

			case 's':
				__set_suppress(sap);
				break;

			case 'v':
				__set_verbose(sap);
				break;

			case '?':
				if(__is_longopt(global_longopts, optopt, argv[opt_index]) == false)
				{
					// There are no more global options to process.
					return opt_index;
				}
				__set_missing(sap, argv[opt_index]);
				break;

			default:
				impact(0, "%s:%d: BUG! Unexpected option '%d'\n",
					__PRETTY_FUNCTION__, __LINE__,
					opt_arg);
				sap->options |= SA_OPT_ERROR;
				break;
		}

		opt_index = optind - 1;
	}

	return opt_index;
}

/*!
 * \brief Parse the file options passed to this program.
 *
 * As a basic webserver, this program obviously needs some way to accept a
 * list of files to serve. This function parses that list of files and the
 * options specific to each one from the command line arguments.
 *
 * \param[out] sap Instance to act on
 * \param[in] argc Number of arguments in the array below
 * \param[in] argv Array of arguments
 *
 * \return the index of the next argument to process in the argv array.
 *
 * \note The return value of this function does not indicate if there was a
 * parsing error. Check for SA_OPT_ERROR after it returns to determine that.
 */
static int __parse_file_opts(simplearg_t sap, int argc, char* argv[])
{
	int opt_index = 0; // Index of the next option to process in argv
	int opt_long;      // Index of the current option in file_longopts
	int opt_arg;       // Short option code being processed

	struct option file_longopts[] =
	{
		{"count", required_argument, NULL, 'c'},
		{0, 0, 0, 0}
	};

	if(sap->options & SA_OPT_ERROR)
	{
		impact(0, "%s:%d: BUG! Not processing any options because simplearg is already in error state\n",
			__PRETTY_FUNCTION__, __LINE__);
		return opt_index;
	}

	// Reset getopt's place in the argument array.
	optind = 1;

	while(opt_index < argc)
	{
		bool is_last_file_option = false;
		while(!(sap->options & SA_OPT_ERROR) && is_last_file_option == false)
		{
			if(opt_index >= argc || argv[opt_index][0] != '-')
			{
				/* getopt does not handle successive non-option arguments well,
				 * so break out and process the next argument as a file.
				 */
				is_last_file_option = true;
				break;
			}

			opt_arg = getopt_long(argc + 1, argv - 1, "c:", file_longopts, &opt_long);

			if(optind < 1 || (optind - 1) < opt_index)
			{
				impact(0, "%s:%d: BUG! optind = %d, opt_index = %d, argc = %d\n",
					__PRETTY_FUNCTION__, __LINE__,
					optind, opt_index, argc);
				sap->options |= SA_OPT_ERROR;
				break;
			}

			switch(opt_arg)
			{
				case -1:
					// There are no more options to process.
					is_last_file_option = true;
					break;

				case 'c':
					__set_count(sap, argv[opt_index], optarg);
					break;

				case '?':
					if(__is_longopt(file_longopts, optopt, argv[opt_index]) == false)
					{
						// There are no more file options to process.
						is_last_file_option = true;
						optind = opt_index + 1;
					}
					else
					{
						__set_missing(sap, argv[opt_index]);
					}
					break;

				default:
					impact(0, "%s:%d: BUG! Unexpected option '%d'\n",
						__PRETTY_FUNCTION__, __LINE__,
						opt_arg);
					sap->options |= SA_OPT_ERROR;
					break;
			}

			opt_index = optind - 1;
		}

		if(sap->options & SA_OPT_ERROR) return opt_index;

		if(opt_index < argc)
		{
			__set_file(sap, argv[opt_index++]);
			++optind;
		}
		else
		{
			simplefile_t last = __get_last_file(sap, 0);
			if(last && last->file == NULL)
			{
				impact(0, "%s: %s: A file option was specified with no FILE\n",
					SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_SYNTAX);
				sap->options |= SA_OPT_ERROR;
			}
		}
	}

	return opt_index;
}

/*!
 * \brief Initialize a SimplePost Arguments instance.
 *
 * \return an initialized arguments instance on success or NULL on error.
 */
simplearg_t simplearg_init()
{
	simplearg_t sap = (simplearg_t) malloc(sizeof(struct simplearg));
	if(sap == NULL) return NULL;

	memset(sap, 0, sizeof(struct simplearg));

	sap->verbosity = DEFAULT_IMPACT_LEVEL;

	return sap;
}

/*!
 * \brief Free the given SimplePost Arguments instance.
 *
 * \param[in] sap Instance to act on
 */
void simplearg_free(simplearg_t sap)
{
	if(sap == NULL) return;

	free(sap->address);

	while(sap->files)
	{
		simplefile_t p = sap->files;
		sap->files = sap->files->next;
		free(p->file);
		free(p);
	}

	free(sap);
}

/*!
 * \brief Parse the arguments passed to this program.
 *
 * \param[out] sap Instance to act on
 * \param[in] argc Number of arguments in the array below
 * \param[in] argv Array of arguments
 */
void simplearg_parse(simplearg_t sap, int argc, char* argv[])
{
	if(argc < 2)
	{
		impact(0, "%s: %s\n", SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_SYNTAX);
		impact(0, "Try 'simplepost --help' for more information.\n");
		sap->options |= SA_OPT_ERROR;
		return;
	}

	int opt_index = 1; // Index of the current option in argv

	// Do not print getopt error messages to standard error.
	opterr = 0;

	opt_index += __parse_global_opts(sap, argc - opt_index, argv + opt_index);
	if(sap->options & SA_OPT_ERROR || sap->actions != SA_ACT_NONE)
	{
		return;
	}

	opt_index += __parse_file_opts(sap, argc - opt_index, argv + opt_index);
	if(sap->options & SA_OPT_ERROR)
	{
		return;
	}

	simplefile_t last = __get_last_file(sap, 0);
	if(last == NULL)
	{
		impact(0, "%s: %s: At least one FILE must be specified\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_SYNTAX);
		sap->options |= SA_OPT_ERROR;
		return;
	}

	if(last->file == NULL)
	{
		impact(0, "%s: %s: Last argument must be a FILE\n",
			SP_ARGS_HEADER_NAMESPACE, SP_ARGS_HEADER_INVLAID_SYNTAX);

		if(sap->files == last) sap->files = NULL;
		if(last->prev) last->prev = NULL;
		free(last);

		sap->options |= SA_OPT_ERROR;
		return;
	}
}
