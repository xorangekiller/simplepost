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

#include "simplecmd.h"
#include "impact.h"
#include "config.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <dirent.h>
#include <regex.h>

/// Command namespace header
#define SP_COMMAND_HEADER_NAMESPACE      "SimplePost::Command"

/// Protocol error string
#define SP_COMMAND_HEADER_PROTOCOL_ERROR "Local Protocol Error"

/// Directory where this program opens its command sockets
#define SP_COMMAND_SOCK_DIR              "/tmp"

/*****************************************************************************
 *                              Socket Support                               *
 *****************************************************************************/

/*!
 * Send a command to the client.
 *
 * \note This function doesn't fail by return value. It's possible that this
 * will cause it to hang, but that is a problem for the caller. If the socket
 * is not opened for reading on the other end, SIGPIPE will be sent to this
 * program. See socket(2).
 *
 * \param[in] sock    Socket descriptor
 * \param[in] command
 * \parblock
 * Command to send to the client
 *
 * If this parameter is NULL, no command will be sent.
 * \endparblock
 * \param[in] data
 * \parblock
 * Data to send to the client
 *
 * The terminating character WILL NOT be sent!
 * If this parameter is NULL, no data will be sent.
 * \endparblock
 */
static void __sock_send(int sock, const char* command, const char* data)
{
	char buffer[30]; // Number of characters to be written

	if(command)
	{
		sprintf(buffer, "%zu", strlen(command));
		write(sock, buffer, strlen(buffer) + 1);
		write(sock, command, strlen(command));
	}

	if(data)
	{
		sprintf(buffer, "%zu", strlen(data));
		write(sock, buffer, strlen(buffer) + 1);

		/* According to POSIX.1-2001, the results of attempting to write() a
		 * zero-length string to anything other than a regular file are
		 * undefined. Since the command set handled by this program is well-
		 * defined, we don't need to worry about the contingency of sending a
		 * zero-length command above; if it occurs, it is clearly a bug. On
		 * the other hand, the data accompanying each command could be almost
		 * anything. To avoid calling the equivalent of
		 * write(descriptor, "", 0); we will write() the length "0" (above),
		 * but no accompanying data.
		 */
		if(!(buffer[0] == '0' && buffer[1] == '\0')) write(sock, data, strlen(data));
	}
}

/*!
 * \brief Send a command to the client and read the response.
 *
 * \param[in] sock    Socket descriptor
 * \param[in] command
 * \parblock
 * Command to send to the client
 *
 * If this parameter is NULL, no command will be sent.
 * \endparblock
 * \param[out] data
 * \parblock
 * NULL-terminated string from the client
 *
 * If *data != NULL, you are responsible for freeing it.
 * \endparblock
 *
 * \return the number of bytes written to the read buffer. If an error
 * occurred (or the operation timed out), zero will be returned instead
 */
static size_t __sock_recv(int sock, const char* command, char** data)
{
	char buffer[30]; // Number of characters to be read from the buffer
	size_t length;   // Number of characters received
	char b;          // Last byte read from the client
	*data = NULL;    // Failsafe

	if(command) __sock_send(sock, command, NULL);

	for(size_t i = 0; read(sock, (void*) &b, 1) == 1; ++i)
	{
		if(i == sizeof(buffer))
		{
			impact(0, "%s: %s: String size cannot be longer than %zu bytes\n",
				SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR,
				sizeof(buffer));
			return 0;
		}

		buffer[i] = b;
		if(b == '\0') break;
	}

	if(sscanf(buffer, "%zu", &length) != 1)
	{
		impact(0, "%s: %s: %s is not a valid string size\n",
			SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR,
			buffer);
		return 0;
	}

	*data = (char*) malloc(sizeof(char) * (length + 1));
	if(*data == NULL)
	{
		impact(2, "%s: %s: Failed to allocate memory for command data buffer\n",
			SP_COMMAND_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC);

		if(command)
		{
			impact(0, "%s: Data buffer required for command %s\n",
				SP_COMMAND_HEADER_NAMESPACE,
				command);
		}
		else
		{
			impact(0, "%s: Buffer required to receive data\n",
				SP_COMMAND_HEADER_NAMESPACE);
		}

		return 0;
	}

	/* The data received may be a zero-length string. The following loop
	 * handles this condition implicitly, but it is not to be overlooked.
	 * Since no terminating character is sent for data (unlike for its length),
	 * a zero-length data string effectively means that we should not attempt
	 * to read anything from the socket.
	 */
	for(size_t i = 0; i < length; ++i)
	{
		if(read(sock, (void*) &b, 1) == 1)
		{
			(*data)[i] = b;
		}
		else
		{
			impact(0, "%s: Read of %s aborted after receiving only %zu of %zu bytes\n",
				SP_COMMAND_HEADER_NAMESPACE,
				command ? command : "data", i, length);

			free(*data);
			*data = NULL;

			return 0;
		}
	}
	(*data)[length] = '\0';

	return length;
}

/*****************************************************************************
 *                            SimpleCommand List                             *
 *****************************************************************************/

/*!
 * \brief Initialize a SimplePost Command List instance.
 *
 * \return an initialized SimplePost Command List instance on success, NULL on
 * error.
 */
simplecmd_list_t simplecmd_list_init()
{
	simplecmd_list_t sclp = (simplecmd_list_t) malloc(sizeof(struct simplecmd_list));
	if(sclp == NULL) return NULL;

	sclp->sock_name = NULL;
	sclp->inst_pid = 0;

	sclp->next = NULL;
	sclp->prev = NULL;

	return sclp;
}

/*!
 * \brief Free the given SimplePost Command List instance.
 *
 * \param[in] sclp Instance to act on
 */
void simplecmd_list_free(simplecmd_list_t sclp)
{
	while(sclp)
	{
		simplecmd_list_t p = sclp;
		sclp = sclp->next;
		if(p->sock_name) free(p->sock_name);
		free(p);
	}
}

/*!
 * \brief Get a list of all SimplePost instances on the system with open sockets.
 *
 * \note This function has two very important exclusions. (1) First, it will
 * not include the socket created by this SimplePost instance in the resulting
 * list. (2) Second, it will only include sockets that we have read/write
 * access to.
 *
 * \param[out] sclp
 * \parblock
 * List of all open SimplePost sockets (excluding ours)
 *
 * If *sclp != NULL, you are responsible for freeing it.
 * \endparblock
 *
 * \return the number of instances in the list. If zero is returned, either a
 * memory allocation error occurred, or, more likely, there are no other
 * instances of SimplePost currently active
 */
size_t simplecmd_list_inst(simplecmd_list_t* sclp)
{
	DIR* dp;                    // Directory handle
	struct dirent* ep;          // Entity in the directory
	char suspect[512];          // Absolute path of the current entity
	int suspect_len;            // Length of the current entity's absolute path
	struct stat suspect_status; // Status of the current entity

	char sock_name[512]; // Name of our socket
	regex_t regex;       // Compiled SimplePost socket matching regular expression

	simplecmd_list_t tail; // Last element in the list
	size_t count = 0;      // Number of items in the list
	tail = *sclp = NULL;   // Failsafe

	dp = opendir(SP_COMMAND_SOCK_DIR);
	if(dp == NULL)
	{
		impact(0, "%s: Failed to open the command socket directory %s: %s\n",
			SP_COMMAND_HEADER_NAMESPACE,
			SP_COMMAND_SOCK_DIR, strerror(errno));
		return 0;
	}

	sprintf(sock_name, "^%s_sock_[0-9]+$", SP_MAIN_SHORT_NAME);
	if(regcomp(&regex, sock_name, REG_EXTENDED | REG_NOSUB | REG_NEWLINE))
	{
		impact(0, "%s: BUG! Failed to compile the socket matching regular expression\n",
			__PRETTY_FUNCTION__);
		closedir(dp);
		return 0;
	}

	sprintf(sock_name, "%s_sock_%d", SP_MAIN_SHORT_NAME, getpid());

	while((ep = readdir(dp)))
	{
		suspect_len = snprintf(suspect, sizeof(suspect), "%s/%s", SP_COMMAND_SOCK_DIR, ep->d_name);
		if(suspect_len < 1 || (size_t) suspect_len >= sizeof(suspect))
		{
			impact(0, "%s: Skipping %s/%s because only %zu of %d bytes are available in the buffer\n",
				SP_COMMAND_HEADER_NAMESPACE,
				SP_COMMAND_SOCK_DIR, ep->d_name,
				sizeof(suspect), suspect_len);
			continue;
		}

		if(stat(suspect, &suspect_status) == 0 && S_ISSOCK(suspect_status.st_mode) && access(suspect, R_OK | W_OK) == 0)
		{
			int regex_ret = regexec(&regex, ep->d_name, 0, NULL, 0);
			if(regex_ret == 0 && strcmp(sock_name, ep->d_name) != 0)
			{
				if(tail)
				{
					tail->next = simplecmd_list_init();
					if(tail->next == NULL)
					{
						impact(0, "%s: %s: Failed to add a new element to the element list\n",
							SP_COMMAND_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC);

						simplecmd_list_free(*sclp);
						*sclp = NULL;
						count = 0;

						break;
					}
					tail->next->prev = tail;
					tail = tail->next;
				}
				else
				{
					tail = *sclp = simplecmd_list_init();
					if(tail == NULL)
					{
						impact(0, "%s:%d: Failed to initialize the list\n",
							__FILE__, __LINE__);
						break;
					}
				}
				count++;

				tail->sock_name = (char*) malloc(sizeof(char) * (strlen(suspect) + 1));
				if(tail->sock_name == NULL)
				{
					impact(2, "%s: %s: Failed to allocate memory for socket name\n",
						SP_COMMAND_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC);

					simplecmd_list_free(*sclp);
					*sclp = NULL;
					count = 0;

					break;
				}
				strcpy(tail->sock_name, suspect);

				const char* pid_ptr = suspect;
				while(isdigit(*pid_ptr) == 0) ++pid_ptr;
				sscanf(pid_ptr, "%d", &tail->inst_pid);

				impact(4, "%s: Found %s:%d socket %s\n",
					SP_COMMAND_HEADER_NAMESPACE,
						SP_MAIN_DESCRIPTION, tail->inst_pid,
						tail->sock_name);
			}
			else if(regex_ret != REG_NOMATCH)
			{
				char buffer[1024]; // regerror() error message

				regerror(regex_ret, &regex, buffer, sizeof(buffer)/sizeof(buffer[0]));
				impact(0, "%s: %s\n", SP_COMMAND_HEADER_NAMESPACE, buffer);

				if(*sclp)
				{
					simplecmd_list_free(*sclp);
					*sclp = NULL;
					count = 0;
				}

				break;
			}
		}
	}

	regfree(&regex);
	closedir(dp);

	return count;
}

/*!
 * \brief Find the SimplePost instance with the given parameters.
 *
 * \param[in] address
 * \parblock
 * IP address of the instance to find
 *
 * If this parameter is NULL, a SimplePost instance with any address that
 * matches the other given parameters will be chosen.
 * \endparblock
 * \param[in] port
 * \parblock
 * Port that the instance to find is listening on
 *
 * If this parameter is zero, a SimplePost instance listening on any port that
 * matches the other given parameters will be chosen.
 * \endparblock
 * \param[in] pid
 * \parblock
 * Process identifier of the instance to find
 *
 * If this parameter is zero, a SimplePost instance with any PID that matches
 * the other given parameters will be chosen.
 * \endparblock
 *
 * \return the PID of the first SimplePost instance that we can find matching
 * the given parameters, or zero if there is no such instance
 */
pid_t simplecmd_find_inst(const char* address, unsigned short port, pid_t pid)
{
	simplecmd_list_t sclp; // List of SimplePost Command instances
	pid_t lowest_pid = 0;  // PID of the oldest SimplePost command instance matching our requirements

	simplecmd_list_inst(&sclp);
	for(simplecmd_list_t p = sclp; p; p = p->next)
	{
		if(pid)
		{
			if(p->inst_pid != pid) continue;
		}
		else if(p->inst_pid <= lowest_pid)
		{
			continue;
		}

		if(address)
		{
			char* inst_address; // Address of the server

			if(simplecmd_get_address(p->inst_pid, &inst_address) == 0) continue;

			int address_match = strcmp(address, inst_address);
			free(inst_address);
			if(address_match) continue;
		}

		if(port)
		{
			unsigned short inst_port; // Port the server is listening on

			inst_port = simplecmd_get_port(p->inst_pid);
			if(port != inst_port) continue;
		}

		lowest_pid = p->inst_pid;
	}
	simplecmd_list_free(sclp);

	return lowest_pid;
}

/*****************************************************************************
 *                       SimpleCommand Server Private                        *
 *****************************************************************************/

/*!
 * \brief SimplePost command handler structure
 */
struct simplecmd_handler
{
	/// Request sent by the client
	const char* request;

	/// Function to process the command
	bool (*handler) (simplecmd_t, int);
};

/**************************************
 * Prototypes of the command handlers *
 **************************************/
static bool __command_send_address(simplecmd_t scp, int sock);
static bool __command_send_port(simplecmd_t scp, int sock);
static bool __command_send_version(simplecmd_t scp, int sock);
static bool __command_send_files(simplecmd_t scp, int sock);
static bool __command_recv_file(simplecmd_t scp, int sock);

/*!
 * \brief SimplePost commands to handle
 */
static struct simplecmd_handler __command_handlers[] =
{
	{"GetAddress", &__command_send_address},
	{"GetPort", &__command_send_port},
	{"GetVersion", &__command_send_version},
	{"GetFiles", &__command_send_files},
	{"SetFile", &__command_recv_file}
};

/***************************************************
 * Indexes of the commands in __command_handlers[] *
 ***************************************************/
#define SP_COMMAND_GET_ADDRESS  0
#define SP_COMMAND_GET_PORT     1
#define SP_COMMAND_GET_VERSION  2
#define SP_COMMAND_GET_FILES    3
#define SP_COMMAND_SET_FILE     4

#define SP_COMMAND_MIN          0
#define SP_COMMAND_MAX          4

/**********************************************************
 * Names of the fields transferred from simplepost_file_t *
 **********************************************************/
#define SP_COMMAND_FILE_INDEX "Index"
#define SP_COMMAND_FILE_FILE  "File"
#define SP_COMMAND_FILE_URI   "URI"
#define SP_COMMAND_FILE_URL   "URL"
#define SP_COMMAND_FILE_COUNT "Count"

/*!
 * \brief SimplePost container for processing client requests
 */
struct simplecmd_request
{
	/// SimplePost command instance to act on
	struct simplecmd* scp;

	/// Socket the client connected on
	int client_sock;
};

/*!
 * \brief SimplePost command server status structure
 */
struct simplecmd
{
	/******************
	 * Initialization *
	 ******************/

	/// Socket for local commands
	int sock;

	/// Absolute file name of the socket
	char* sock_name;

	/// Handle of the primary thread
	pthread_t accept_thread;

	/***********
	 * Clients *
	 ***********/

	/// Are we accepting client connections?
	bool accpeting_clients;

	/// Number of clients currently being served
	size_t client_count;

	/// SimplePost handle
	simplepost_t spp;
};

/*!
 * \brief Send the primary address our web server is bound to to the client.
 *
 * \note If the web server is not running, a zero-length string will be sent.
 * See the related comments in __sock_send() and __sock_recv() to get a better
 * understanding of how this contingency is handled.
 *
 * \param[in] scp  Instance to act on
 * \param[in] sock Client socket
 *
 * \retval true the requested information was sent successfully
 * \retval false failed to respond to the request
 */
static bool __command_send_address(simplecmd_t scp, int sock)
{
	char* address; // Address of the web server
	size_t length; // Length of the address string

	length = simplepost_get_address(scp->spp, &address);
	if(address == NULL || length == 0) return false;

	__sock_send(sock, NULL, address ? address : "");
	free(address);

	return true;
}

/*!
 * \brief Send the port our web server is listening on to the client.
 *
 * \note If the web server is not running, zero will be sent as the port
 * number.
 *
 * \param[in] scp  Instance to act on
 * \param[in] sock Client socket
 *
 * \retval true the requested information was sent successfully
 * \retval false failed to respond to the request
 */
static bool __command_send_port(simplecmd_t scp, int sock)
{
	char buffer[30];     // Port as a string
	unsigned short port; // Port the web server is listening on

	port = simplepost_get_port(scp->spp);
	if(port == 0) return false;

	if(sprintf(buffer, "%hu", port) <= 0) return false;
	__sock_send(sock, NULL, buffer);

	return true;
}

/*!
 * \brief Send the current program version to the client.
 *
 * \param[in] scp  Instance to act on
 * \param[in] sock Client socket
 *
 * \retval true the requested information was sent successfully
 * \retval false failed to respond to the request
 */
static bool __command_send_version(simplecmd_t scp, int sock)
{
	// Unused parameters
	(void) scp;

	__sock_send(sock, NULL, SP_MAIN_VERSION);
	return 1;
}

/*!
 * \brief Send the list of files that we are serving to the client.
 *
 * \param[in] scp  Instance to act on
 * \param[in] sock Client socket
 *
 * \retval true the requested information was sent successfully
 * \retval false failed to respond to the request
 */
static bool __command_send_files(simplecmd_t scp, int sock)
{
	char buffer[30];         // File count or index as a string
	simplepost_file_t files; // List of files being served
	size_t count;            // Number of files being served
	size_t i = 0;            // Index of the current file being sent

	count = simplepost_get_files(scp->spp, &files);

	impact(3, "%s: %s: Sending list of %zu files\n",
		SP_COMMAND_HEADER_NAMESPACE,
		__func__, count);
	if(sprintf(buffer, "%zu", count) <= 0)
	{
		simplepost_file_free(files);
		return false;
	}
	__sock_send(sock, NULL, buffer);

	for(simplepost_file_t p = files; p; p = p->next)
	{
		impact(3, "%s: %s: Sending %s %zu\n",
			SP_COMMAND_HEADER_NAMESPACE, __func__,
			SP_COMMAND_FILE_INDEX, i);
		if(sprintf(buffer, "%zu", i++) <= 0)
		{
			simplepost_file_free(files);
			return false;
		}
		__sock_send(sock, SP_COMMAND_FILE_INDEX, buffer);

		if(p->file)
		{
			impact(3, "%s: %s: Sending %s %s\n",
				SP_COMMAND_HEADER_NAMESPACE, __func__,
				SP_COMMAND_FILE_FILE, p->file);
			__sock_send(sock, SP_COMMAND_FILE_FILE, p->file);
		}

		if(p->count)
		{
			if(sprintf(buffer, "%u", p->count) <= 0)
			{
				impact(0, "%s: %s: Failed to buffer %s %u\n",
					SP_COMMAND_HEADER_NAMESPACE, __func__,
					SP_COMMAND_FILE_COUNT, p->count);
				simplepost_file_free(files);
				return false;
			}

			impact(3, "%s: %s: Sending %s %s\n",
				SP_COMMAND_HEADER_NAMESPACE, __func__,
				SP_COMMAND_FILE_COUNT, buffer);
			__sock_send(sock, SP_COMMAND_FILE_COUNT, buffer);
		}

		/* Always send the URL last. The reason for this is that only the FILE
		 * and URL fields and required per-file. All others are optional.
		 * Therefore to make sure that the optional fields are not skipped on
		 * the client side, always send a required field last.
		 */
		if(p->url)
		{
			impact(3, "%s: %s: Sending %s %s\n",
				SP_COMMAND_HEADER_NAMESPACE, __func__,
				SP_COMMAND_FILE_URL, p->url);
			__sock_send(sock, SP_COMMAND_FILE_URL, p->url);
		}
	}

	simplepost_file_free(files);

	return true;
}

/*!
 * \brief Receive a file and count from the client and add it our web server.
 *
 * \param[in] scp  Instance to act on
 * \param[in] sock Client socket
 *
 * \retval true the requested information was sent successfully
 * \retval false failed to respond to the request
 */
static bool __command_recv_file(simplecmd_t scp, int sock)
{
	char* url = NULL;    // URL of the file being served
	char* file = NULL;   // Name and path of the file to serve
	char* uri = NULL;    // URI of the file to serve
	char* buffer = NULL; // Count or identifier string from the client
	unsigned int count;  // Number of times the file should be served

	while(__sock_recv(sock, NULL, &buffer))
	{
		if(buffer == NULL) goto error;

		impact(3, "%s: %s: Receiving %s\n",
			SP_COMMAND_HEADER_NAMESPACE, __func__,
			buffer);

		if(strcmp(buffer, SP_COMMAND_FILE_FILE) == 0)
		{
			free(buffer);
			buffer = NULL;

			if(file)
			{
				impact(0, "%s: %s: Received a second FILE\n",
					SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR);
				goto error;
			}
			else if(__sock_recv(sock, NULL, &file) == 0)
			{
				impact(0, "%s: %s: Did not receive a FILE as expected\n",
					SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR);
				goto error;
			}
		}
		else if(strcmp(buffer, SP_COMMAND_FILE_URI) == 0)
		{
			free(buffer);
			buffer = NULL;

			if(uri)
			{
				impact(0, "%s: %s: Received a second URI\n",
					SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR);
				goto error;
			}
			else if(__sock_recv(sock, NULL, &uri) == 0)
			{
				impact(0, "%s: %s: Did not receive a URI as expected\n",
					SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR);
				goto error;
			}
		}
		else if(strcmp(buffer, SP_COMMAND_FILE_COUNT) == 0)
		{
			free(buffer);
			buffer = NULL;

			if(__sock_recv(sock, NULL, &buffer) == 0)
			{
				impact(0, "%s: %s: Did not receive the COUNT as expected\n",
					SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR);
				goto error;
			}

			if(sscanf(buffer, "%u", &count) == EOF)
			{
				impact(0, "%s: %s: %s is not a valid COUNT\n",
					SP_COMMAND_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC,
					buffer);
				goto error;
			}

			free(buffer);
			buffer = NULL;
		}
		else
		{
			impact(3, "%s: %s: Invalid file identifier \"%s\"\n",
				SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR,
				buffer);
			goto error;
		}
	}

	if(file == NULL)
	{
		impact(0, "%s: %s: Did not receive a FILE to serve\n",
			SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR);
		goto error;
	}

	if(simplepost_serve_file(scp->spp, &url, file, uri, count) == 0) return false;

	free(buffer);
	free(uri);
	free(file);
	free(url);
	return true;

error:
	free(buffer);
	free(uri);
	free(file);
	free(url);
	return false;
}

/*!
 * \brief Process a request accepted by the server.
 *
 * \param[in] p SimplePost command + client socket wrapper
 *
 * \return NULL
 */
static void* __process_request(void* p)
{
	struct simplecmd_request* scrp = (struct simplecmd_request*) p; // Properly cast SimplePost command request handle

	simplecmd_t scp = scrp->scp;  // SimplePost command instance to act on
	int sock = scrp->client_sock; // Socket the client connected on

	free(scrp);
	scrp = p = NULL;

	char* command; // Command to process
	size_t length; // Length of the command string
	bool response; // Command handler return value

	++(scp->client_count);

	length = __sock_recv(sock, NULL, &command);
	if(command == NULL || length == 0) goto error;
	response = -1;

	for(unsigned int i = SP_COMMAND_MIN; i <= SP_COMMAND_MAX; ++i)
	{
		if(strcmp(command, __command_handlers[i].request) == 0)
		{
			impact(2, "%s: Request 0x%lx: Responding to %s command\n",
				SP_COMMAND_HEADER_NAMESPACE, pthread_self(),
				__command_handlers[i].request);
			response = (*__command_handlers[i].handler)(scp, sock);
			break;
		}
	}

	#ifdef DEBUG
	if(response)
	{
		impact(2, "%s: Request 0x%lx: Successfully processed %s command\n",
			SP_COMMAND_HEADER_NAMESPACE, pthread_self(),
			command);
	}
	else
	{
		impact(2, "%s: Request 0x%lx: Failed to process %s command\n",
			SP_COMMAND_HEADER_NAMESPACE, pthread_self(),
			command);
	}
	#endif // DEBUG

error:
	impact(4, "%s: Request 0x%lx: Closing client %d\n",
		SP_COMMAND_HEADER_NAMESPACE, pthread_self(),
		sock);
	close(sock);

	if(command) free(command);
	--(scp->client_count);

	return NULL;
}

/*!
 * \brief Start accepting requests from clients.
 *
 * \param[in] p Instance to act on
 *
 * \return NULL
 */
static void* __accept_requests(void* p)
{
	simplecmd_t scp = (simplecmd_t) p; // Properly cast SimplePost command handle

	int client_sock;                // Socket the client connected on
	struct sockaddr_in client_name; // Name of the client
	socklen_t client_name_len;      // Length of the client name structure
	pthread_t client_thread;        // Handle of the client thread

	struct timespec timeout; // Maximum time to wait for a connection before checking the shutdown sentinel
	fd_set fds;              // File descriptor set (for pselect() socket monitoring)

	scp->client_count = 0;
	scp->accpeting_clients = true;

	while(scp->accpeting_clients)
	{
		FD_ZERO(&fds);
		FD_SET(scp->sock, &fds);

		timeout.tv_sec = 2;
		timeout.tv_nsec = 0;

		switch(pselect(scp->sock + 1, &fds, NULL, NULL, &timeout, NULL))
		{
			case -1:
				impact(0, "%s: Cannot accept connections on socket %d\n",
					SP_COMMAND_HEADER_NAMESPACE,
					scp->sock);
				scp->accpeting_clients = false;
				continue;

			case 0:
				continue;
		}

		client_name_len = sizeof(client_name);
		client_sock = accept(scp->sock, (struct sockaddr*) &client_name, &client_name_len);
		if(client_sock == -1) continue;

		struct simplecmd_request* scrp = (struct simplecmd_request*) malloc(sizeof(struct simplecmd_request));
		if(scrp == NULL)
		{
			impact(0, "%s: %s: Failed to allocate memory for a new command request thread\n",
				SP_COMMAND_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC);
			scp->accpeting_clients = false;
			continue;
		}
		scrp->scp = scp;
		scrp->client_sock = client_sock;

		if(pthread_create(&client_thread, NULL, &__process_request, (void*) scrp) == 0)
		{
			impact(2, "%s: Launched request processing thread 0x%lx for client %d\n",
				SP_COMMAND_HEADER_NAMESPACE,
				client_thread, client_sock);
			pthread_detach(client_thread);
		}
		else
		{
			impact(2, "%s: Failed to launch request processing thread for client %d\n",
				SP_COMMAND_HEADER_NAMESPACE,
				client_sock);
			close(scrp->client_sock);
			free(scrp);
		}
	}

	impact(2, "%s: Waiting for %zu clients to finish processing ...\n",
		SP_COMMAND_HEADER_NAMESPACE,
		scp->client_count);
	while(scp->client_count) usleep(1000);

	impact(4, "%s: Closing socket %d\n",
		SP_COMMAND_HEADER_NAMESPACE,
		scp->sock);
	close(scp->sock);
	scp->sock = -1;
	remove(scp->sock_name);

	return NULL;
}

/*****************************************************************************
 *                       SimpleCommand Server Public                         *
 *****************************************************************************/

/*!
 * \brief Initialize a SimplePost command instance.
 *
 * \return an initialized SimplePost command instance on success, NULL
 * otherwise.
 */
simplecmd_t simplecmd_init()
{
	simplecmd_t scp = (simplecmd_t) malloc(sizeof(struct simplecmd));
	if(scp == NULL) return NULL;

	scp->sock = -1;
	scp->sock_name = NULL;
	scp->accept_thread = -1;

	scp->accpeting_clients = false;
	scp->client_count = 0;
	scp->spp = NULL;

	return scp;
}

/*!
 * \brief Free the given SimplePost command instance.
 *
 * \param[in] scp Instance to act on
 */
void simplecmd_free(simplecmd_t scp)
{
	if(scp == NULL) return;

	if(scp->accpeting_clients) simplecmd_deactivate(scp);

	if(scp->sock != -1)
	{
		impact(2, "%s:%d: BUG! %s should have (directly or indirectly) closed the socket already!\n",
			__FILE__, __LINE__, __func__);
		close(scp->sock);
	}

	if(scp->sock_name)
	{
		remove(scp->sock_name);
		free(scp->sock_name);
	}
}

/*!
 * \brief Start accepting client commands.
 *
 * \param[in] scp Instance to act on
 * \param[in] spp SimplePost instance to back our client requests
 *
 * \retval true the command server was successfully started
 * \retval false the command server is already running or could not be started
 */
bool simplecmd_activate(simplecmd_t scp, simplepost_t spp)
{
	if(scp->sock != -1)
	{
		impact(0, "%s: Server is already activated\n",
			SP_COMMAND_HEADER_NAMESPACE);
		return false;
	}

	if(spp == NULL)
	{
		impact(0, "%s: Cannot activate command server without a %s instance\n",
			SP_COMMAND_HEADER_NAMESPACE,
			SP_MAIN_DESCRIPTION);
		return false;
	}
	scp->spp = spp;

	if(scp->sock_name == NULL)
	{
		char buffer[2048]; // Buffer for the the socket name string

		sprintf(buffer, "%s/%s_sock_%d", SP_COMMAND_SOCK_DIR, SP_MAIN_SHORT_NAME, getpid());

		scp->sock_name = (char*) malloc(sizeof(char) * (strlen(buffer) + 1));
		if(scp->sock_name == NULL)
		{
			impact(0, "%s: %s: Failed to allocate memory for the socket name\n",
				SP_COMMAND_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC);
			return false;
		}
		strcpy(scp->sock_name, buffer);
	}

	scp->sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if(scp->sock == -1)
	{
		impact(0, "%s: Socket could not be created\n",
			SP_COMMAND_HEADER_NAMESPACE);
		return false;
	}

	struct sockaddr_un sock_addr; // Address to assign to the socket
	memset(&sock_addr, 0, sizeof(struct sockaddr_un));
	sock_addr.sun_family = AF_UNIX;
	strncpy(sock_addr.sun_path, scp->sock_name, sizeof(sock_addr.sun_path) - 1);

	if(bind(scp->sock, (struct sockaddr*) &sock_addr, sizeof(struct sockaddr_un)) == -1)
	{
		impact(0, "%s: Failed to bind %s to socket %d\n",
			SP_COMMAND_HEADER_NAMESPACE,
			scp->sock_name, scp->sock);
		goto error;
	}

	if(listen(scp->sock, 30) == -1)
	{
		impact(0, "%s: Cannot listen on socket %d\n",
			SP_COMMAND_HEADER_NAMESPACE,
			scp->sock);
		goto error;
	}

	if(pthread_create(&scp->accept_thread, NULL, &__accept_requests, (void*) scp) != 0)
	{
		impact(0, "%s: Failed to create listen thread for %s\n",
			SP_COMMAND_HEADER_NAMESPACE,
			scp->sock_name);
		goto error;
	}

	impact(1, "%s: Now accepting commands on %s\n",
		SP_COMMAND_HEADER_NAMESPACE,
		scp->sock_name);

	return true;

error:
	close(scp->sock);
	scp->sock = -1;

	remove(scp->sock_name);
	free(scp->sock_name);
	scp->sock_name = NULL;

	return false;
}

/*!
 * \brief Stop accepting client commands.
 *
 * \param[in] scp Instance to act on
 *
 * \retval true the command server has been successfully killed
 * \retval false the command server is not running or could not be shut down
 */
bool simplecmd_deactivate(simplecmd_t scp)
{
	if(scp->sock == -1)
	{
		impact(0, "%s: Server is not active\n", SP_COMMAND_HEADER_NAMESPACE);
		return false;
	}

	impact(1, "%s: Shutting down ...\n", SP_COMMAND_HEADER_NAMESPACE);

	scp->accpeting_clients = false;
	pthread_join(scp->accept_thread, NULL);

	#ifdef DEBUG
	impact(2, "%s: 0x%tu cleanup complete\n",
		SP_COMMAND_HEADER_NAMESPACE, scp->accept_thread);
	#endif // DEBUG

	return true;
}

/*!
 * \brief Are we listening for client connections?
 *
 * \param[in] scp Instance to act on
 *
 * \retval true the server is online
 * \retval false the server is not running
 */
bool simplecmd_is_alive(const simplecmd_t scp)
{
	return (scp->sock == -1) ? false : true;
}

/*****************************************************************************
 *                       SimpleCommand Client Private                        *
 *****************************************************************************/

/*!
 * \brief Open a socket to the server with the specified process identifier.
 *
 * \note This functions prints error messages so its consumers don't have to.
 *
 * \param[in] server_pid Process identifier of the server
 *
 * \return an open socket descriptor if the specified PID belongs to a
 * compatible SimplePost server with an open local socket, -1 if POSIX open()
 * encounters an error, or -2 if this function encounters an error
 */
static int __open_sock_by_pid(pid_t server_pid)
{
	int sock;                     // Socket descriptor
	struct sockaddr_un sock_addr; // Address to assign to the socket
	char sock_name[512];          // Name of the socket
	struct stat sock_status;      // Status of the socket

	sprintf(sock_name, "%s/%s_sock_%d", SP_COMMAND_SOCK_DIR, SP_MAIN_SHORT_NAME, server_pid);
	if(stat(sock_name, &sock_status) == -1)
	{
		impact(0, "%s: Socket %s does not exist\n",
			SP_COMMAND_HEADER_NAMESPACE,
			sock_name);
		return -2;
	}
	if(S_ISSOCK(sock_status.st_mode) == 0)
	{
		impact(0, "%s: %s is not a socket\n",
			SP_COMMAND_HEADER_NAMESPACE,
			sock_name);
		return -2;
	}

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if(sock == -1)
	{
		impact(0, "%s: Failed to open socket %s\n",
			SP_COMMAND_HEADER_NAMESPACE,
			sock_name);
		return -1;
	}

	memset(&sock_addr, 0, sizeof(struct sockaddr_un));
	sock_addr.sun_family = AF_UNIX;
	strncpy(sock_addr.sun_path, sock_name, sizeof(sock_addr.sun_path) - 1);

	if(connect(sock, (struct sockaddr*) &sock_addr, sizeof(sock_addr)) == -1)
	{
		impact(0, "%s: Failed to connect to socket %s\n",
			SP_COMMAND_HEADER_NAMESPACE,
			sock_name);
		close(sock);
		return -1;
	}

	return sock;
}

/*****************************************************************************
 *                       SimpleCommand Client Public                         *
 *****************************************************************************/

/*!
 * \brief Get the address the specified server is bound to.
 *
 * \param[in]  server_pid Process identifier of the server to query
 * \param[out] address
 * \parblock
 * Address of the server
 *
 * The storage for this string will be dynamically allocated. You are
 * responsible for freeing it (unless it is NULL, in which case an error
 * occurred).
 * \endparblock
 *
 * \return the number of characters written to the address (excluding the NULL-
 * terminating character)
 */
size_t simplecmd_get_address(pid_t server_pid, char** address)
{
	int sock;          // Socket descriptor
	size_t length = 0; // Length of the address
	*address = NULL;   // Failsafe

	sock = __open_sock_by_pid(server_pid);
	if(sock < 0) return 0;

	length = __sock_recv(sock, __command_handlers[SP_COMMAND_GET_ADDRESS].request, address);

	close(sock);

	return length;
}

/*!
 * \brief Get the port the specified server is listening on.
 *
 * \param[in] server_pid Process identifier of the server to query
 *
 * \return the server's port number
 */
unsigned short simplecmd_get_port(pid_t server_pid)
{
	int sock;            // Socket descriptor
	unsigned short port; // Port the specified server is listening on
	char* buffer;        // Port as a string (directly from the server)

	sock = __open_sock_by_pid(server_pid);
	if(sock < 0) return 0;

	__sock_recv(sock, __command_handlers[SP_COMMAND_GET_PORT].request, &buffer);
	if(buffer)
	{
		if(sscanf(buffer, "%hu", &port) != 1)
		{
			impact(0, "%s: %s: %s is not a port number\n",
				SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR,
				buffer);
			port = 0;
		}
		free(buffer);
	}
	else
	{
		port = 0;
	}

	close(sock);

	return port;
}

/*!
 * \brief Get the version of the specified server.
 *
 * \param[in]  server_pid Process identifier of the server to query
 * \param[out] version
 * \parblock
 * Version of the server
 *
 * The storage for this string will be dynamically allocated. You are
 * responsible for freeing it (unless it is NULL, in which case an error
 * occurred).
 * \endparblock
 *
 * \return the number of characters written to the version string (excluding
 * the NULL-terminating character)
 */
size_t simplecmd_get_version(pid_t server_pid, char** version)
{
	int sock;          // Socket descriptor
	size_t length = 0; // Length of the version string
	*version = NULL;   // Failsafe

	sock = __open_sock_by_pid(server_pid);
	if(sock < 0) return 0;

	length = __sock_recv(sock, __command_handlers[SP_COMMAND_GET_VERSION].request, version);

	close(sock);

	return length;
}

/*!
 * \brief Get the list of files being served by the specified server.
 *
 * \param[in] server_pid Process identifier of the server to act on
 * \param[out] files     List of files currently being served
 *
 * \return the number of files hosted by the server, or -1 if the list could
 * not be retrieved
 */
ssize_t simplecmd_get_files(pid_t server_pid, simplepost_file_t* files)
{
	int sock;               // Socket descriptor
	char* buffer = NULL;    // Count, index, or identifier string from the server
	size_t count;           // Number of files being served
	size_t i = 0;           // Index of the current file being received
	size_t t = 0;           // Temporary file index converted from the buffer
	simplepost_file_t tail; // Last file in the *files list
	tail = *files = NULL;   // Failsafe

	sock = __open_sock_by_pid(server_pid);
	if(sock < 0) return -1;

	__sock_recv(sock, __command_handlers[SP_COMMAND_GET_FILES].request, &buffer);
	if(buffer == NULL) goto error;

	if(sscanf(buffer, "%zu", &count) != 1)
	{
		impact(0, "%s: %s: %s is not a number\n",
			SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR,
			buffer);
		goto error;
	}
	free(buffer);
	buffer = NULL;

	impact(3, "%s: %s: Receiving list of %zu files\n",
		SP_COMMAND_HEADER_NAMESPACE, __func__,
		count);
	if(count == 0) goto no_error;

	while((i + 1) < count || tail == NULL || tail->file == NULL || tail->url == NULL)
	{
		__sock_recv(sock, NULL, &buffer);
		if(buffer == NULL) goto error;

		impact(3, "%s: %s: Receiving %s\n",
			SP_COMMAND_HEADER_NAMESPACE, __func__,
			buffer);

		if(strcmp(buffer, SP_COMMAND_FILE_INDEX) == 0)
		{
			free(buffer);
			buffer = NULL;

			__sock_recv(sock, NULL, &buffer);
			if(buffer == NULL)
			{
				impact(0, "%s: %s: Did not receive the next file index as expected\n",
					SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR);
				goto error;
			}

			if(sscanf(buffer, "%zu", &t) != 1)
			{
				impact(0, "%s: %s: %s is not a number\n",
					SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR,
					buffer);
				goto error;
			}
			free(buffer);
			buffer = NULL;

			if(tail == NULL)
			{
				if(t != i)
				{
					impact(0, "%s: %s: Expected the next file index to be %zu, not %zu\n",
						SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR,
						i, t);
					goto error;
				}

				tail = *files = simplepost_file_init();
				if(tail == NULL) goto error;
			}
			else
			{
				if(t != ++i)
				{
					impact(0, "%s: %s: Expected the next file index to be %zu, not %zu\n",
						SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR,
						i, t);
					goto error;
				}

				simplepost_file_t prev = tail;

				tail = simplepost_file_init();
				if(tail == NULL) goto error;

				tail->prev = prev;
				prev->next = tail;
			}
		}
		else if(tail == NULL)
		{
			impact(0, "%s: %s: Received \"%s\" before the first file index\n",
				SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR,
				buffer);
			goto error;
		}
		else if(strcmp(buffer, SP_COMMAND_FILE_FILE) == 0)
		{
			free(buffer);
			buffer = NULL;

			__sock_recv(sock, NULL, &buffer);
			if(buffer == NULL)
			{
				impact(0, "%s: %s: Did not receive the file[%zu] location as expected\n",
					SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR,
					i);
				goto error;
			}

			if(tail->file)
			{
				impact(0, "%s: %s: Received new file[%zu] location \"%s\", but it is already set to \"%s\"\n",
					SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR,
					i, buffer, tail->file);
				goto error;
			}

			tail->file = buffer;
		}
		else if(strcmp(buffer, SP_COMMAND_FILE_URL) == 0)
		{
			free(buffer);
			buffer = NULL;

			__sock_recv(sock, NULL, &buffer);
			if(buffer == NULL)
			{
				impact(0, "%s: %s: Did not receive the file[%zu] URL as expected\n",
					SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR,
					i);
				goto error;
			}

			if(tail->url)
			{
				impact(0, "%s: %s: Received new file[%zu] URL \"%s\", but it is already set to \"%s\"\n",
					SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR,
					i, buffer, tail->url);
				goto error;
			}

			tail->url = buffer;
		}
		else if(strcmp(buffer, SP_COMMAND_FILE_COUNT) == 0)
		{
			free(buffer);
			buffer = NULL;

			__sock_recv(sock, NULL, &buffer);
			if(buffer == NULL)
			{
				impact(0, "%s: %s: Did not receive the file[%zu] count as expected\n",
					SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR,
					i);
				goto error;
			}

			if(sscanf(buffer, "%u", &tail->count) != 1)
			{
				impact(0, "%s: %s: Received new file[%zu] count \"%s\", but it is not a positive integer as expected!\n",
					SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR,
					i, buffer);
				goto error;
			}

			free(buffer);
			buffer = NULL;
		}
		else
		{
			/* We probably should not have run into this condition in the
			 * first place. It most likely means that we are talking to an
			 * older (or newer) version of this program, and the command
			 * protocol has changed. If not, it is probably a bug.
			 */
			impact(3, "%s: %s: Skipping unsupported file identifier \"%s\"\n",
				SP_COMMAND_HEADER_NAMESPACE, SP_COMMAND_HEADER_PROTOCOL_ERROR,
				buffer);
			free(buffer);
			buffer = NULL;

			/* Read and discard the argument that presumably comes after the
			 * unsupported file identifier that we encountered.
			 */
			__sock_recv(sock, NULL, &buffer);
			free(buffer);
			buffer = NULL;
		}
	}

	for(tail = *files, i = 0; tail; tail = tail->next, ++i);
	if(i != count)
	{
		impact(0, "%s: BUG! Only received %zu of %zu files\n",
			__PRETTY_FUNCTION__,
			i, count);
		goto error;
	}

no_error:
	close(sock);

	return count;

error:
	simplepost_file_free(*files);
	*files = NULL;

	free(buffer);
	close(sock);

	return -1;
}

/*!
 * \brief Add a file to the specified server.
 *
 * \param[in] server_pid Process identifier of the server to act on
 * \param[in] file       Name and path of the file to serve
 * \param[in] uri        URI of the file to serve
 * \param[in] count      Number of times the file should be served
 *
 * \return true if the file was successfully added to the server, false if
 * something went wrong (and the file was not added to the server)
 */
bool simplecmd_set_file(
	pid_t server_pid,
	const char* file,
	const char* uri,
	unsigned int count)
{
	int sock;         // Socket descriptor
	char buffer[512]; // Count as a string

	sock = __open_sock_by_pid(server_pid);
	if(sock < 0) return false;

	__sock_send(sock, __command_handlers[SP_COMMAND_SET_FILE].request, NULL);

	if(file)
	{
		impact(3, "%s: %s: Sending %s %s\n",
			SP_COMMAND_HEADER_NAMESPACE, __func__,
			SP_COMMAND_FILE_FILE, file);
		__sock_send(sock, SP_COMMAND_FILE_FILE, file);
	}

	if(count)
	{
		sprintf(buffer, "%u", count);

		impact(3, "%s: %s: Sending %s %s\n",
			SP_COMMAND_HEADER_NAMESPACE, __func__,
			SP_COMMAND_FILE_COUNT, buffer);
		__sock_send(sock, SP_COMMAND_FILE_COUNT, buffer);
	}

	if(uri)
	{
		impact(3, "%s: %s: Sending %s %s\n",
			SP_COMMAND_HEADER_NAMESPACE, __func__,
			SP_COMMAND_FILE_URI, uri);
		__sock_send(sock, SP_COMMAND_FILE_URI, uri);
	}

	close(sock);

	return true;
}
