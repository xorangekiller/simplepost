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
#include "simplestr.h"
#include "impact.h"
#include "config.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <microhttpd.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>

#if defined(HAVE_IFADDRS_H) && \
    defined(HAVE_NET_IF_H)  && \
    defined(HAVE_SYS_IOCTL_H)
#define HAVE_IFADDRS_SUPPORT
#else
#undef HAVE_IFADDRS_SUPPORT
#endif

#ifdef HAVE_IFADDRS_SUPPORT
#include <sys/ioctl.h>
#include <net/if.h>
#include <ifaddrs.h>
#endif

#ifdef HAVE_GETLINE
#include <ctype.h>
#endif

#ifdef HAVE_LIBMAGIC
#include <magic.h>
#endif

/// SimplePost namespace header
#define SP_HTTP_HEADER_NAMESPACE  "SimplePost::HTTP"

/// libmicrohttpd error message header
#define SP_HTTP_HEADER_MICROHTTPD "microhttpd"

/*****************************************************************************
 *                              Address Support                              *
 *****************************************************************************/

/*!
 * \brief Response codes for the SimplePost address resolution methods
 */
enum simplepost_addrerr
{
	/// Failed to allocate memory for the response
	SP_AE_MEM_ALLOC = -2,

	/// Fatal error! No address could be retrieved.
	SP_AE_FATAL = -1,

	/// The address of the default network interface was retrieved
	/// successfully.
	SP_AE_DEFAULT_ADDR = 0,

	/// There is no default route. The address of another network interface
	/// was retrieved instead.
	SP_AE_OTHER_ADDR = 1,

	/// The address of the loopback interface was retrieved.
	SP_AE_LO_ADDR = 2
};

/*!
 * \brief Get the name of the "default" network interface.
 *
 * This function will retrieve the name of the network interface through which
 * the default route is directed.
 *
 * \param[out] ifname Name of the default network interface
 * \param[in]  size   Size (in bytes) of ifname
 *
 * \retval true  The network interface name was returned successfully.
 * \retval false There is no default route, or the interface associated with
 *               it could not be determined.
 */
static bool __get_default_ifname(char* ifname, size_t size)
{
	FILE* fp = fopen("/proc/net/route", "r");
	if(fp == NULL)
	{
		return false;
	}

	char* buf;            // Temporary buffer for processing a line
	char* line = NULL;    // Line read from the route list
	size_t line_size = 0; // Size of the line
	ssize_t line_len;     // Length of the line

	#ifdef HAVE_GETLINE
	while((line_len = getline(&line, &line_size, fp)) >= 0)
	{
		if(line_len == 0) continue;

		/* The interface name is always the first field on each line of the
		 * space-delimited route list. Find the end of it.
		 */
		buf = line;
		while(buf < (line + line_len) && isspace(*buf) == 0) ++buf;
		if(buf == line + line_len) continue;

		/* We found the end of the interface name field. Terminate it so that
		 * we can easily extract it later if this is indeed the default route.
		 */
		*buf++ = '\0';
		if(buf == line + line_len) continue;

		/* The destination address is the second field on each line of the
		 * space-delimited route list. Find the beginning of it.
		 */
		while(buf < (line + line_len) && isspace(*buf)) ++buf;
		if(buf == line + line_len) continue;

		/* The default route is identified by a destination address that
		 * consists of all zeros. Check to see if we found it.
		 */
		while(buf < (line + line_len) && *buf == '0') ++buf;
		if(buf == line + line_len || isspace(*buf) == 0) continue;

		if(ifname == NULL || size <= strlen(line)) goto error;

		strcpy(ifname, line);

		free(line);
		fclose(fp);
		return true;
	}
	#else // !HAVE_GETLINE
	(void) ifname;
	(void) size;
	(void) buf;
	(void) line_size;
	(void) line_len;
	goto error;
	#endif // HAVE_GETLINE

error:
	free(line);
	fclose(fp);
	return false;
}

/*!
 * \brief Get the name of any active network interface other than loopback.
 *
 * \param[out] ifname Name of an active network interface
 * \param[in]  size   Size (in bytes) of ifname
 *
 * \retval true  The network interface name was returned successfully.
 * \retval false There are no active network interfaces besides the loopback
 *               interface, or the list could not be retrieved.
 */
static bool __get_other_ifname(char* ifname, size_t size)
{
	#ifdef HAVE_IFADDRS_SUPPORT
	struct ifaddrs* addr_list; // Complete list of network interfaces

	if(getifaddrs(&addr_list) == -1) return false;

	for(struct ifaddrs* addr = addr_list; addr; addr = addr->ifa_next)
	{
		if(addr->ifa_addr->sa_family != AF_PACKET) continue;
		if(!(addr->ifa_flags & IFF_UP)) continue;
		if(addr->ifa_flags & IFF_LOOPBACK) continue;

		if(ifname == NULL || size <= strlen(addr->ifa_name))
		{
			freeifaddrs(addr_list);
			return false;
		}

		strcpy(ifname, addr->ifa_name);

		freeifaddrs(addr_list);
		return true;
	}

	freeifaddrs(addr_list);
	#else // !HAVE_IFADDRS_SUPPORT
	(void) ifname;
	(void) size;
	#endif // HAVE_IFADDRS_SUPPORT

	return false;
}

/*!
 * \brief Get the name of the loopback network interface.
 *
 * \param[out] ifname Name of the loopback network interface
 * \param[in]  size   Size (in bytes) of ifname
 *
 * \retval true  The network interface name was returned successfully.
 * \retval false There is no looback interfaces, or it could not be determined
 *               for some reason.
 */
static bool __get_lo_ifname(char* ifname, size_t size)
{
	#ifdef HAVE_IFADDRS_SUPPORT
	struct ifaddrs* addr_list; // Complete list of network interfaces

	if(getifaddrs(&addr_list) == -1) return false;

	for(struct ifaddrs* addr = addr_list; addr; addr = addr->ifa_next)
	{
		if(addr->ifa_addr->sa_family != AF_PACKET) continue;
		if(!(addr->ifa_flags & IFF_UP)) continue;
		if(!(addr->ifa_flags & IFF_LOOPBACK)) continue;

		if(ifname == NULL || size <= strlen(addr->ifa_name))
		{
			freeifaddrs(addr_list);
			return false;
		}

		strcpy(ifname, addr->ifa_name);

		freeifaddrs(addr_list);
		return true;
	}

	freeifaddrs(addr_list);
	#else // !HAVE_IFADDRS_SUPPORT
	(void) ifname;
	(void) size;
	#endif // HAVE_IFADDRS_SUPPORT

	return false;
}

/*!
 * \brief Get the address of the "default" network interface in network byte
 * order.
 *
 * This function will attempt to retrieve the IP address of the network
 * interface through which the default route is directed. If there is no
 * default route, it will choose another network interface that is up and has
 * an assigned IP address instead. If there is no such interface, it will fall
 * back to the loopback interface.
 *
 * \note This function's interface intentionally models getsockname(2),
 * although it is not completely compatible. The intent is that it can fairly
 * easily be used along side/instead of getsockname().
 *
 * \param[out]   addr
 * \parblock
 * Address belonging to the network interface indicated by the return value
 *
 * This address is truncated if the buffer provided is too small. In this
 * case, addrlen will return a value greater than was supplied to the call.
 * \endparblock
 * \param[inout] addrlen
 * \parblock
 * Amount of space (in bytes) pointed to by addr
 *
 * When this function returns, this value will be modified to indicate the
 * actual size of the socket address.
 * \endparblock
 *
 * \return the code indicating the address that was retrieved, if any
 */
static enum simplepost_addrerr __get_default_sockaddr(
	struct sockaddr* addr,
	socklen_t* addrlen)
{
	if(addr == NULL || addrlen == NULL) return SP_AE_MEM_ALLOC;

	#ifdef HAVE_IFADDRS_SUPPORT
	enum simplepost_addrerr ret; // Error code to return
	char ifname[IFNAMSIZ];       // Name of the default network interface

	if(__get_default_ifname(ifname, sizeof(ifname)/sizeof(ifname[0])))
	{
		ret = SP_AE_DEFAULT_ADDR;
	}
	else if(__get_other_ifname(ifname, sizeof(ifname)/sizeof(ifname[0])))
	{
		ret = SP_AE_OTHER_ADDR;
	}
	else if(__get_lo_ifname(ifname, sizeof(ifname)/sizeof(ifname[0])))
	{
		ret = SP_AE_LO_ADDR;
	}
	else
	{
		return SP_AE_FATAL;
	}

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == -1) return SP_AE_FATAL;

	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);

	if(ioctl(sock, SIOCGIFADDR, &ifr) == -1)
	{
		close(sock);
		return SP_AE_FATAL;
	}

	close(sock);
	sock = -1;

	if(*addrlen < sizeof(ifr.ifr_addr))
	{
		*addrlen = sizeof(ifr.ifr_addr);
		return SP_AE_MEM_ALLOC;
	}

	*addrlen = sizeof(ifr.ifr_addr);
	memcpy(addr, &ifr.ifr_addr, *addrlen);
	return ret;
	#else // !HAVE_IFADDRS_SUPPORT
	struct sockaddr_in lo_addr; // Default loopback address in network byte order
	char* lo_addr_str;          // Loopback address as a string

	memset(&lo_addr, 0, sizeof(lo_addr));
	lo_addr.sin_family = AF_INET;

	if(sizeof(lo_addr.sin_addr) != 4) return SP_AE_FATAL;
	lo_addr_str = (char*) &lo_addr.sin_addr;
	lo_addr_str[0] = 0x7F;
	lo_addr_str[1] = 0x00;
	lo_addr_str[2] = 0x00;
	lo_addr_str[3] = 0x01;

	if(*addrlen < sizeof(lo_addr))
	{
		*addrlen = sizeof(lo_addr);
		return SP_AE_MEM_ALLOC;
	}

	*addrlen = sizeof(lo_addr);
	memcpy(addr, &lo_addr, *addrlen);
	return SP_AE_LO_ADDR;
	#endif // HAVE_IFADDRS_SUPPORT
}

/*!
 * \brief Get the address of the "default" network interface as a string.
 *
 * \note This is a convenience method. It uses __get_default_sockaddr() to do
 * the "real work" of getting the IP address, then it merely converts it into
 * a string. If you need to get more information about the interface or need
 * the address in network byte order as well, call __get_default_sockaddr()
 * and use inet_ntoa() to get the address string yourself.
 *
 * \param[out] addr
 * \parblock
 * Address belonging to the network interface indicated by the return value
 *
 * This address MUST NOT point to a static buffer! It will be dynamically
 * allocated if it is NULL, or it will be reallocated to be large enough to
 * hold the address string if it is too small.
 * \endparblock
 * \param[inout] size
 * \parblock
 * Size (in bytes) of the string pointed to by addr
 *
 * If addr is reallocated to be larger, size will be modified when this
 * function returns to indicate its new size. If size is zero, addr will be
 * automatically allocated, NOT reallocated!
 * \endparblock
 *
 * \return the code indicating the address that was retrieved, if any
 */
static enum simplepost_addrerr __get_default_address(char** addr, size_t* size)
{
	if(addr == NULL || size == NULL) return SP_AE_MEM_ALLOC;

	enum simplepost_addrerr ret; // Error code to return
	struct sockaddr_in def_addr; // Default address in network byte order
	socklen_t def_addr_len;      // Length of the default address

	memset(&def_addr, 0, sizeof(def_addr));
	def_addr.sin_family = AF_INET;
	def_addr_len = sizeof(def_addr);

	ret = __get_default_sockaddr((struct sockaddr*) &def_addr, &def_addr_len);
	if(ret < 0) return ret;

	char* tmp_addr;      // Temporary pointer to the address string
	size_t tmp_addr_len; // Length of the temporary address string

	tmp_addr = inet_ntoa(def_addr.sin_addr);
	if(tmp_addr == NULL) return SP_AE_FATAL;

	tmp_addr_len = strlen(tmp_addr);

	if(*addr == NULL || *size == 0)
	{
		*addr = (char*) malloc(sizeof(char) * (tmp_addr_len + 1));
		if(*addr == NULL)
		{
			*size = 0;
			return SP_AE_MEM_ALLOC;
		}

		*size = tmp_addr_len + 1;
	}
	else if(*size <= tmp_addr_len)
	{
		char* realloc_addr = (char*) realloc(*addr, sizeof(char) * (tmp_addr_len + 1));
		if(realloc_addr == NULL)
		{
			return SP_AE_MEM_ALLOC;
		}

		*addr = realloc_addr;
		*size = tmp_addr_len + 1;
	}

	strncpy(*addr, tmp_addr, tmp_addr_len);
	(*addr)[tmp_addr_len] = '\0';

	return ret;
}

/*****************************************************************************
 *                               File Support                                *
 *****************************************************************************/

/*!
 * \brief SimplePost container of files being served
 */
struct simplepost_serve
{
	/// Name and path of the file on the filesystem
	char* file;

	/// Uniform Resource Identifier assigned to the file
	char* uri;

	/// Number of times the file may be downloaded
	unsigned int count;


	/// Next file in the doubly-linked list
	struct simplepost_serve* next;

	/// Previous file in the doubly-linked list
	struct simplepost_serve* prev;
};

/*!
 * \brief Initialize a blank SimpleServe instance.
 *
 * \return a new instance on success, or NULL if we failed to allocate the
 * requested memory
 */
static struct simplepost_serve* __simplepost_serve_init()
{
	struct simplepost_serve* spsp = (struct simplepost_serve*) malloc(sizeof(struct simplepost_serve));
	if(spsp == NULL) return NULL;

	memset(spsp, 0, sizeof(struct simplepost_serve));

	return spsp;
}

/*!
 * \brief Free the given SimpleServe instance.
 *
 * \warning This function will only free from spsp to the end of the list. If
 * spsp is not the first element in the list (spsp->prev != NULL), the
 * beginning of the list (the part this function won't free) will be properly
 * terminated.
 *
 * \param[in] spsp List to free
 */
static void __simplepost_serve_free(struct simplepost_serve* spsp)
{
	if(spsp->prev) spsp->prev->next = NULL;

	while(spsp)
	{
		struct simplepost_serve* p = spsp;
		spsp = spsp->next;

		if(p->file) free(p->file);
		if(p->uri) free(p->uri);
		free(p);
	}
}

/*!
 * \brief Insert the second list before the current element in the first list.
 *
 * \param[in] spsp1 List to insert into
 * \param[in] spsp2
 * \parblock
 * List to insert
 *
 * If this parameter is NULL, a single new element will be inserted.
 * \endparblock
 *
 * \return the modified list at the inserted element on success, or NULL if
 * something went wrong
 */
static struct simplepost_serve* __simplepost_serve_insert_before(
	struct simplepost_serve* spsp1,
	struct simplepost_serve* spsp2)
{
	if(spsp2 == NULL)
	{
		spsp2 = __simplepost_serve_init();
		if(spsp2 == NULL) return NULL;
	}

	spsp2->prev = spsp1->prev;
	if(spsp2->next)
	{
		struct simplepost_serve* p; // Last element in spsp2
		for(p = spsp2; p->next != NULL; p = p->next);
		p->next = spsp1;
		spsp1->prev = p;
	}
	else
	{
		spsp2->next = spsp1;
		spsp1->prev = spsp2;
	}

	return spsp2;
}

/*!
 * \brief Insert the second list after the current element in the first list.
 *
 * \param[in] spsp1 List to insert into
 * \param[in] spsp2
 * \parblock
 * List to insert
 *
 * If this parameter is NULL, a single new element will be inserted.
 * \endparblock
 *
 * \return the modified list at the inserted element on success, or NULL if
 * something went wrong
 */
static struct simplepost_serve* __simplepost_serve_insert_after(
	struct simplepost_serve* spsp1,
	struct simplepost_serve* spsp2)
{
	if(spsp2 == NULL)
	{
		spsp2 = __simplepost_serve_init();
		if(spsp2 == NULL) return NULL;
	}

	spsp2->prev = spsp1;
	if(spsp2->next)
	{
		struct simplepost_serve* p; // Last element in spsp2
		for(p = spsp2; p->next != NULL; p = p->next);
		p->next = spsp1->next;
	}
	else
	{
		spsp2->next = spsp1->next;
	}
	spsp1->next = spsp2;

	return spsp2;
}

/*!
 * \brief Remove the specified number of elements from the list.
 *
 * \param[in] spsp
 * \parblock
 * List to modify
 *
 * The element assigned to this parameter will be the first element in the
 * list to be removed. THIS FUNCTION ONLY TRAVERSES THE LIST FORWARD, NEVER
 * BACKWARD.
 * \endparblock
 * \param[in] n
 * \parblock
 * Number of elements to remove
 *
 * If the list has fewer than n elements (from spsp forward), every element
 * will be removed from spsp to the end of the list. n = 0 invokes the same
 * behavior.
 * \endparblock
 *
 * \return the new first element in the list, which is ordinarily the element
 * before spsp. However if there are no more elements in the list (either
 * before spsp or after spsp + n), return NULL instead.
 */
static struct simplepost_serve* __simplepost_serve_remove(
	struct simplepost_serve* spsp,
	size_t n)
{
	struct simplepost_serve* prev = spsp->prev; // Last element in the original list

	if(n == 0)
	{
		__simplepost_serve_free(spsp);
	}
	else
	{
		for(size_t i = 0; i < n && spsp; ++i)
		{
			struct simplepost_serve* p = spsp;
			spsp = spsp->next;

			if(p->file) free(p->file);
			if(p->uri) free(p->uri);
			free(p);
		}

		if(prev)
		{
			prev->next = spsp;
			if(spsp) spsp->prev = prev;
		}
		else if(spsp)
		{
			prev = spsp;
			spsp->prev = NULL;
		}
	}

	return prev;
}

/*!
 * \brief Calculate the number of elements in the list (from the current
 * element forward).
 *
 * \param[in] spsp First element in the list to count
 *
 * \return the number of elements in the list
 */
static size_t __simplepost_serve_length(struct simplepost_serve* spsp)
{
	size_t n = 0; // Number of elements in the list

	for(struct simplepost_serve* p = spsp; p != NULL; p = p->next) ++n;

	return n;
}

/*****************************************************************************
 *                              HTTP Responses                               *
 *****************************************************************************/

/*************************
 * HTTP response strings *
 *************************/
#define SP_HTTP_RESPONSE_BAD_REQUEST "<html><head><title>Bad Request\r\n</title></head>\r\n<body><p>HTTP request method not supported.\r\n</body></html>\r\n"
#define SP_HTTP_RESPONSE_FORBIDDEN "<html><head><title>Forbidden\r\n</title></head>\r\n<body><p>The request CANNOT be fulfilled.\r\n</body></html>\r\n"
#define SP_HTTP_RESPONSE_NOT_FOUND "<html><head><title>Not Found\r\n</title></head>\r\n<body><p>There is no resource matching the specified URI.\r\n</body></html>\r\n"
#define SP_HTTP_RESPONSE_NOT_ACCEPTABLE "<html><head><title>Not Acceptable\r\n</title></head>\r\n<body><p>HTTP headers request a resource we cannot satisfy.\r\n</body></html>\r\n"
#define SP_HTTP_RESPONSE_GONE "<html><head><title>Not Available\r\n</title></head>\r\n<body><p>The requested resource is no longer available.\r\n</body></html>\r\n"
#define SP_HTTP_RESPONSE_UNSUPPORTED_MEDIA_TYPE "<html><head><title>Unsupported Media Type\r\n</title></head>\r\n<body><p>The requested resource is not valid for the requested method.\r\n</body></html>\r\n"
#define SP_HTTP_RESPONSE_INTERNAL_SERVER_ERROR "<html><head><title>Internal Server Error\r\n</title></head>\r\n<body><p>HTTP server encountered an unexpected condition which prevented it from fulfilling the request.\r\n</body></html>\r\n"
#define SP_HTTP_RESPONSE_NOT_IMPLEMENTED "<html><head><title>Method Not Implemented\r\n</title></head>\r\n<body><p>HTTP request method not supported.\r\n</body></html>\r\n"

/*!
 * \brief Prepare to send a response to the client from a data buffer.
 *
 * \note The data passed to this function MUST be available until the
 * MHD_Response instance returned by this function has been destroyed. DO NOT
 * DESTROY THE RESPONSE until AFTER libmicrohttpd has responded to the request!
 *
 * \param[in] connection  Connection identifying the client
 * \param[in] status_code HTTP status code to send
 * \param[in] size        Size of the data array to send
 * \param[in] data        Data to send
 *
 * \return a libmicrohttpd response instance if the specified data has been
 * queued for transmission to the client, or print an error message and return
 * NULL if an error occurs
 */
static struct MHD_Response* __response_prep_data(
	struct MHD_Connection* connection,
	unsigned int status_code,
	size_t size,
	void* data)
{
	struct MHD_Response* response; // Response to the request

	/* MHD_create_response_from_data() is deprecated and should only be used
	 * with old versions of libmicrohttpd that do not support
	 * MHD_create_response_from_buffer(). After a sensible amount of time we
	 * should really remove this backwards compatibility check and just make
	 * configure require the newer method.
	 */
	#ifdef HAVE_MHD_CREATE_RESPONSE_FROM_BUFFER
	response = MHD_create_response_from_buffer(size, data, MHD_RESPMEM_PERSISTENT);
	#else
	#ifdef HAVE_MHD_CREATE_RESPONSE_FROM_DATA
	response = MHD_create_response_from_data(size, data, MHD_NO, MHD_NO);
	#else
	#error "libmicrohttpd does not have a supported MHD_create_response_*() method"
	#endif // HAVE_MHD_CREATE_RESPONSE_FROM_DATA
	#endif // HAVE_MHD_CREATE_RESPONSE_FROM_BUFFER

	if(response == NULL)
	{
		impact(2, "%s:%d: %s: Failed to allocate memory for the HTTP response %u\n",
			__PRETTY_FUNCTION__, __LINE__, SP_MAIN_HEADER_MEMORY_ALLOC,
			status_code);
		return NULL;
	}

	if(MHD_queue_response(connection, status_code, response) == MHD_NO)
	{
		impact(0, "%s: Request 0x%lx: Cannot queue response with status %u\n",
			SP_HTTP_HEADER_NAMESPACE, pthread_self(),
			status_code);
		MHD_destroy_response(response);
		return NULL;
	}

	return response;
}

/*!
 * \brief Prepare to send a response to the client from a file.
 *
 * \note According to the MHD_create_response_from_fd() documentation, the
 * file descriptor this function opens for the sepecified file will be closed
 * when the MHD_Response instance returned by this function is destroyed. DO
 * NOT DESTROY THE RESPONSE until AFTER libmicrohttpd has responded to the
 * request!
 *
 * \param[in] connection  Connection identifying the client
 * \param[in] status_code HTTP status code to send
 * \param[in] size        Size of the data portion of the response
 * \param[in] file        Name and path of the file to send
 *
 * \return a libmicrohttpd response instance if the specified file has been
 * queued for transmission to the client, or print an error message and return
 * NULL if an error occurs
 */
static struct MHD_Response* __response_prep_file(
	struct MHD_Connection* connection,
	unsigned int status_code,
	size_t size,
	const char* file)
{
	struct MHD_Response* response; // Response to the request
	int fd;                        // File descriptor
	#ifdef HAVE_LIBMAGIC
	magic_t hmagic;                // Magic file handle
	#endif

	fd = open(file, O_RDONLY);
	if(fd == -1)
	{
		impact(0, "%s: Request 0x%lx: Cannot open FILE %s for reading\n",
			SP_HTTP_HEADER_NAMESPACE, pthread_self(),
			file);
		return NULL;
	}

	response = MHD_create_response_from_fd(size, fd);
	if(response == NULL)
	{
		impact(2, "%s:%d: %s: Failed to allocate memory for the HTTP response %u\n",
			__PRETTY_FUNCTION__, __LINE__, SP_MAIN_HEADER_MEMORY_ALLOC,
			status_code);
		close(fd);
		return NULL;
	}

	#ifdef HAVE_LIBMAGIC
	hmagic = magic_open(MAGIC_MIME_TYPE);
	if(hmagic)
	{
		magic_load(hmagic, NULL);
		const char* mime_type = magic_file(hmagic, file);

		/* According to RFC 2616 Section 7.2.1, the content type should only
		 * be sent if it can be determined. If not, the client should do its
		 * best to determine what to do with the content instead. Notably,
		 * Apache used to send application/octet-stream to indicate arbitrary
		 * binary data when it couldn't determine the file type, but that is
		 * not correct according to the HTTP/1.1 specification.
		 */
		if(mime_type) MHD_add_response_header(response, "Content-Type", mime_type);

		magic_close(hmagic);
	}
	#endif // HAVE_LIBMAGIC

	if(MHD_queue_response(connection, status_code, response) == MHD_NO)
	{
		impact(2, "%s: Request 0x%lx: Cannot queue FILE %s with status %u\n",
			SP_HTTP_HEADER_NAMESPACE, pthread_self(),
			file, status_code);
		close(fd);
		MHD_destroy_response(response);
		return NULL;
	}

	return response;
}

/*****************************************************************************
 *                            SimplePost Private                             *
 *****************************************************************************/

/// Maximum number of pending connections before clients start getting refused
#define SP_HTTP_BACKLOG   16

/// Milliseconds to sleep between shutdown checks while blocking
#define SP_HTTP_SLEEP     100

/// Maximum number of files which may be served simultaneously
#define SP_HTTP_FILES_MAX SIZE_MAX

/*!
 * \brief SimplePost request status structure
 */
struct simplepost_state
{
	/// Response to the request
	struct MHD_Response* response;


	/// Name and path of the file to serve
	char* file;

	/// Length of the file name and path
	size_t file_length;


	/// Data to serve
	char* data;

	/// Length of the data
	size_t data_length;
};

/*!
 * \brief SimplePost HTTP server status structure
 */
struct simplepost
{
	/******************
	 * Initialization *
	 ******************/

	/// HTTP server instance
	struct MHD_Daemon* httpd;

	/// Port for the HTTP server
	unsigned short port;

	/// Address of the HTTP server
	char* address;

	/// Mutex for port and address
	pthread_mutex_t master_lock;

	/*********
	 * Files *
	 *********/

	/// List of files being served
	struct simplepost_serve* files;

	/// Number of files being served
	size_t files_count;

	/// Mutex for files and files_count
	pthread_mutex_t files_lock;
};

/*!
 * \brief Do the given URIs match?
 *
 * \note This function is slightly more complicated than a simple strcmp() of
 * both input strings. It does NULL checks and takes into account partial or
 * malformed URIs that do no start with "/".
 *
 * \param[in] uri1 First URI to compare
 * \param[in] uri2 Second URI to compare
 *
 * \return true if both URIs are equivalent, false if not
 */
static bool __does_uri_match(const char* uri1, const char* uri2)
{
	if(uri1 == NULL || uri2 == NULL) return false;

	if(uri1[0] == '/') ++uri1;
	if(uri2[0] == '/') ++uri2;

	return (strcmp(uri1, uri2) == 0);
}

/*!
 * \brief Get the name and path of the file to serve from the given URI.
 *
 * \note This function compares the given URI to the list of files we are
 * serving. The URI does not necessarily correspond one-to-one to an actual
 * file on the filesystem, hence the need for this function.
 *
 * \warning The file count is taken into consideration by this function. It
 * will be appropriately decremented if a file is found matching the URI and
 * returned by this function. The file will also be removed from the list of
 * files being served and the instance file count decremented if the file
 * reaches the maximum allowable times it may be served.
 *
 * \param[in] spp   SimplePost instance to act on
 * \param[out] file
 * \parblock
 * Name and path of the file on the filesystem
 *
 * The storage for this string will be dynamically allocated. You are
 * responsible for freeing it (unless it is NULL, in which case an error
 * occurred).
 * \endparblock
 * \param[in] uri   Uniform Resource Identifier to parse
 *
 * \return the number of characters written to the output string. If the
 * return value is zero, either the URI does not specify a valid file, or
 * another (more serious) error occurred
 */
static size_t __get_filename_from_uri(
	simplepost_t spp,
	char** file,
	const char* uri)
{
	size_t file_length = 0; // Length of the file name and path
	*file = NULL;           // Failsafe

	pthread_mutex_lock(&spp->files_lock);

	if(spp->files)
	{
		for(struct simplepost_serve* p = spp->files; p; p = p->next)
		{
			if(strcmp(uri, p->uri) == 0)
			{
				*file = (char*) malloc(sizeof(char) * (strlen(p->file) + 1));
				if(*file == NULL) goto error;

				strcpy(*file, p->file);
				file_length = strlen(*file);

				if(p->count > 0 && --p->count == 0)
				{
					impact(2, "%s: FILE %s has reached its COUNT and will be removed\n",
						SP_HTTP_HEADER_NAMESPACE,
						p->file);

					if(p == spp->files) spp->files = __simplepost_serve_remove(p, 1);
					else __simplepost_serve_remove(p, 1);
					--(spp->files_count);
				}

				goto error;
			}
		}
	}

error:
	pthread_mutex_unlock(&spp->files_lock);
	return file_length;
}

/*!
 * \brief Panic! Cleanup the SimplePost instance after libmicrohttpd
 * encountered an unrecoverable error condition.
 *
 * \param[in] cls    SimplePost instance to act on
 * \param[in] file   C source file where the error occured
 * \param[in] line   Line of the C source file where the error occured
 * \param[in] reason Error message
 */
static void __panic(
	void* cls,
	const char* file,
	unsigned int line,
	const char* reason)
{
	simplepost_t spp = (simplepost_t) cls; // Instance to act on

	impact(2, "%s:%u: PANIC!\n", file, line);
	impact(0, "%s: %s: Emergency Shutdown: %s\n",
		SP_HTTP_HEADER_NAMESPACE, SP_HTTP_HEADER_MICROHTTPD,
		reason);

	simplepost_unbind(spp);
}

/*!
 * \brief Print error messages from libmicrohttpd.
 *
 * \param[in] cls    SimplePost instance to act on
 * \param[in] format Format string
 * \param[in] ap     List of format arguments
 */
static void __log_microhttpd_messages(void* cls, const char* format, va_list ap)
{
	// Unused parameters
	(void) cls;

	char buffer[2048]; // libmicrohttpd error message
	int length;        // Number of characters written to the buffer

	length = vsprintf(buffer, format, ap);
	if(length < 0)
	{
		impact(2, "%s:%d: BUG! Failed to process libmicrohttpd error message\n",
			__PRETTY_FUNCTION__, __LINE__);
	}
	else
	{
		impact(0, "%s: %s: %s\n",
			SP_HTTP_HEADER_NAMESPACE, SP_HTTP_HEADER_MICROHTTPD,
			buffer);
	}
}

/*!
 * \brief Process a request accepted by the server.
 *
 * \param[in] cls            SimplePost instance to act on
 * \param[in] connection     Connection handle
 * \param[in] uri            Uniform Resource Identifier of the request
 * \param[in] method         HTTP method
 * \param[in] version        HTTP version
 * \param[in] data           Data sent by the client (excluding HEADERS)
 * \param[inout] data_size
 * \parblock
 * Size of the client-provided data
 *
 * Initially this must be the size of the data provided. This function will
 * update it to the number of bytes NOT processed.
 * \endparblock
 * \param[out] state         Data preserved for future calls of this request
 *
 * \retval MHD_NO will be returned if the socket must be closed due to a
 *         serious error we encountered while handling the request
 * \retval MHD_YES will be returned if the connection was handled successfully
 */
static int __process_request(
	void* cls,
	struct MHD_Connection* connection,
	const char* uri,
	const char* method,
	const char* version,
	const char* data,
	size_t* data_size,
	void** state)
{
	// Unused parameters
	(void) data;

	simplepost_t spp = (simplepost_t) cls; // Instance to act on
	struct simplepost_state* spsp = NULL;  // Request state

	impact(2, "%s: Request 0x%lx: method: %s\n",
		SP_HTTP_HEADER_NAMESPACE, pthread_self(),
		method);
	impact(2, "%s: Request 0x%lx: URI: %s\n",
		SP_HTTP_HEADER_NAMESPACE, pthread_self(),
		uri);
	impact(2, "%s: Request 0x%lx: version: %s\n",
		SP_HTTP_HEADER_NAMESPACE, pthread_self(),
		version);

	#ifdef DEBUG
	if(*state)
	{
		impact(0, "%s: Request 0x%lx: Request should be stateless\n",
			SP_HTTP_HEADER_NAMESPACE, pthread_self());
		goto finalize_request;
	}
	#endif // DEBUG

	spsp = (struct simplepost_state*) malloc(sizeof(struct simplepost_state));
	if(spsp == NULL)
	{
		impact(2, "%s: Request 0x%lx: %s: Failed to allocate memory for the request state\n",
			SP_HTTP_HEADER_NAMESPACE, pthread_self(),
			SP_MAIN_HEADER_MEMORY_ALLOC);
		goto finalize_request;
	}
	*state = (void*) spsp;

	/* struct simplepost_state should probably gets its own initialization and
	 * destruction methods. There is also probably a better way to implement
	 * __response_prep_data() and __response_prep_file(), one that would allow
	 * them to directly handle struct simplepost_state and alleviate this
	 * function of more responsibility.
	 *
	 * I'm still undecided on exactly how much control this function should
	 * delegate and how that interface should look. For now we are just going
	 * to handle construction here and destruction in __finalize_request().
	 */
	spsp->response = NULL;
	spsp->file = NULL;
	spsp->file_length = 0;
	spsp->data = NULL;
	spsp->data_length = 0;

	/* We really don't care what data the client sent us. Nothing handled by
	 * SimplePost actually requires the client to send additional data.
	 */
	*data_size = 0;

	if(strcmp(method, MHD_HTTP_METHOD_GET) == 0)
	{
		struct stat file_status; // File status

		spsp->file_length = __get_filename_from_uri(spp, &spsp->file, uri);
		if(spsp->file_length == 0 || stat(spsp->file, &file_status) == -1)
		{
			impact(0, "%s: Request 0x%lx: Resource not found: %s\n",
				SP_HTTP_HEADER_NAMESPACE, pthread_self(),
				uri);
			spsp->response = __response_prep_data(connection,
				MHD_HTTP_NOT_FOUND,
				strlen(SP_HTTP_RESPONSE_NOT_FOUND),
				(void*) SP_HTTP_RESPONSE_NOT_FOUND);
			goto finalize_request;
		}

		if(S_ISDIR(file_status.st_mode))
		{
			const char* append_index = "/index.html";
			char* new_file = realloc(spsp->file, sizeof(char) * (spsp->file_length + strlen(append_index) + 1));
			if(new_file)
			{
				spsp->file = new_file;
				strcat(spsp->file, append_index);
				if(stat(spsp->file, &file_status) == -1)
				{
					impact(2, "%s: Request 0x%lx: File not found: %s\n",
						SP_HTTP_HEADER_NAMESPACE, pthread_self(),
						spsp->file);
					spsp->response = __response_prep_data(connection,
						MHD_HTTP_NOT_FOUND,
						strlen(SP_HTTP_RESPONSE_NOT_FOUND),
						(void*) SP_HTTP_RESPONSE_NOT_FOUND);
					goto finalize_request;
				}
			}
		}

		if(S_ISDIR(file_status.st_mode))
		{
			impact(0, "%s: Request 0x%lx: Directory not supported: %s\n",
				SP_HTTP_HEADER_NAMESPACE, pthread_self(),
				spsp->file);
			spsp->response = __response_prep_data(connection,
				MHD_HTTP_FORBIDDEN,
				strlen(SP_HTTP_RESPONSE_FORBIDDEN),
				(void*) SP_HTTP_RESPONSE_FORBIDDEN);
			goto finalize_request;
		}

		impact(2, "%s: Request 0x%lx: Serving FILE %s\n",
			SP_HTTP_HEADER_NAMESPACE, pthread_self(),
			spsp->file);
		spsp->response = __response_prep_file(connection,
			MHD_HTTP_OK,
			file_status.st_size,
			spsp->file);
	}
	else
	{
		impact(2, "%s: Request 0x%lx: %s is not a supported HTTP method\n",
			SP_HTTP_HEADER_NAMESPACE, pthread_self(),
			method);
		spsp->response = __response_prep_data(connection,
			MHD_HTTP_BAD_REQUEST,
			strlen(SP_HTTP_RESPONSE_BAD_REQUEST),
			(void*) SP_HTTP_RESPONSE_BAD_REQUEST);
	}

finalize_request:
	if(spsp == NULL)
	{
		impact(2, "%s: Request 0x%lx: Prematurely terminating response ...\n",
			SP_HTTP_HEADER_NAMESPACE, pthread_self());
		return MHD_NO;
	}

	if(spsp->response)
	{
		impact(2, "%s: Request 0x%lx: Sending response ...\n",
			SP_HTTP_HEADER_NAMESPACE, pthread_self());
		return MHD_YES;
	}

	impact(2, "%s: Request 0x%lx: Terminating response ...\n",
		SP_HTTP_HEADER_NAMESPACE, pthread_self());

	if(spsp->file) free(spsp->file);
	if(spsp->data) free(spsp->data);
	free(spsp);
	*state = spsp = NULL;

	return MHD_NO;
}

/*!
 * \brief Cleanup the resources allocated for the request.
 *
 * \param[in] cls        SimplePost instance to act on
 * \param[in] connection Connection handle
 * \param[in] state      Data preserved from the request handler
 * \param[in] toe        Reason the request was terminated
 */
void __finalize_request(
	void* cls,
	struct MHD_Connection* connection,
	void** state,
	enum MHD_RequestTerminationCode toe)
{
	// Unused parameters
	(void) cls;
	(void) connection;

	struct simplepost_state* spsp = (struct simplepost_state*) *state; // Request to cleanup

	#ifdef DEBUG
	if(spsp == NULL)
	{
		impact(2, "%s: Request 0x%lx: Cannot cleanup stateless request\n",
			SP_HTTP_HEADER_NAMESPACE, pthread_self());
		return;
	}
	#endif // DEBUG

	if(spsp->response)
	{
		MHD_destroy_response(spsp->response);
	}
	else
	{
		impact(2, "%s:%d: BUG! __process_request() should have returned MHD_NO if it failed to queue a response!\n",
			__PRETTY_FUNCTION__, __LINE__);
	}

	#ifdef DEBUG
	if(spsp->file == NULL && spsp->file_length)
	{
		impact(2, "%s:%d: BUG! simplepost_state::file should NEVER be NULL while simplepost_state::file_length is non-zero\n",
			__PRETTY_FUNCTION__, __LINE__);
	}
	#endif // DEBUG
	if(spsp->file) free(spsp->file);

	#ifdef DEBUG
	if(spsp->data == NULL && spsp->data_length)
	{
		impact(2, "%s:%d: BUG! simplepost_state::data should NEVER be NULL while simplepost_state::data_length is non-zero\n",
			__PRETTY_FUNCTION__, __LINE__);
	}
	#endif // DEBUG
	if(spsp->data) free(spsp->data);

	#ifdef DEBUG
	impact(2, "%s: Request 0x%lx: ", SP_HTTP_HEADER_NAMESPACE, pthread_self());
	switch(toe)
	{
		case MHD_REQUEST_TERMINATED_COMPLETED_OK:
			impact(2, "Successfully sent the response\n");
			break;

		case MHD_REQUEST_TERMINATED_WITH_ERROR:
			impact(2, "Error handling the connection\n");
			break;

		case MHD_REQUEST_TERMINATED_TIMEOUT_REACHED:
			impact(2, "No activity on the connection until the timeout was reached\n");
			break;

		case MHD_REQUEST_TERMINATED_DAEMON_SHUTDOWN:
			impact(2, "Connection terminated because the server is shutting down\n");
			break;

		case MHD_REQUEST_TERMINATED_READ_ERROR:
			impact(2, "Connection died because the client did not send the expected data\n");
			break;

		case MHD_REQUEST_TERMINATED_CLIENT_ABORT:
			impact(2, "The client terminated the connection by closing the socket for writing (TCP half-closed)\n");
			break;

		default:
			impact(2, "Invalid Termination Code: %d\n", toe);
			break;
	}
	#endif // DEBUG
}

/*****************************************************************************
 *                            SimplePost Public                              *
 *****************************************************************************/

/*!
 * \brief Initialize a new SimplePost instance.
 *
 * \return a pointer to the new instance on success, or NULL if we failed to
 * allocate the requested memory
 */
simplepost_t simplepost_init()
{
	simplepost_t spp = (simplepost_t) malloc(sizeof(struct simplepost));
	if(spp == NULL) return NULL;

	memset(spp, 0, sizeof(struct simplepost));

	pthread_mutex_init(&spp->master_lock, NULL);
	pthread_mutex_init(&spp->files_lock, NULL);

	return spp;
}

/*!
 * \brief Free the given SimplePost instance.
 *
 * \param[in] spp SimplePost instance to act on
 */
void simplepost_free(simplepost_t spp)
{
	if(spp == NULL) return;

	if(spp->httpd) simplepost_unbind(spp);
	if(spp->address) free(spp->address);

	if(spp->files) __simplepost_serve_free(spp->files);

	pthread_mutex_destroy(&spp->master_lock);
	pthread_mutex_destroy(&spp->files_lock);

	free(spp);
}

/*!
 * \brief Start the web server on the specified port.
 *
 * \param[in] spp     SimplePost instance to act on
 * \param[in] port
 * \parblock
 * Port to initialize the server on
 * 
 * If the port is 0, a port will be dynamically allocated.
 * \endparblock
 * \param[in] address
 * \parblock
 * Network address to bind the server to
 *
 * If the address is NULL, the server will be bound to all local interfaces
 * (0.0.0.0 in netstat parlance).
 * \endparblock
 *
 * \return the port the server is bound to. If the return value is 0, an error
 * occurred.
 */
unsigned short simplepost_bind(
	simplepost_t spp,
	const char* address,
	unsigned short port)
{
	pthread_mutex_lock(&spp->master_lock);

	if(spp->httpd)
	{
		impact(0, "%s: Server is already initialized\n",
			SP_HTTP_HEADER_NAMESPACE);
		goto error;
	}

	struct sockaddr_in source; // Source address and port the server should be bound to
	memset(&source, 0, sizeof(source));

	source.sin_family = AF_INET;
	source.sin_port = htons(port);

	if(address)
	{
		struct in_addr sin_addr; // Source address in network byte order

		if(inet_pton(AF_INET, address, (void*) &sin_addr) != 1)
		{
			impact(0, "%s: Invalid source address specified\n",
				SP_HTTP_HEADER_NAMESPACE);
			goto error;
		}
		source.sin_addr = sin_addr;

		if(spp->address) free(spp->address);
		spp->address = (char*) malloc(sizeof(char) * (strlen(address) + 1));
		if(spp->address == NULL)
		{
			impact(0, "%s: %s: Failed to allocate memory for the source address\n",
				SP_HTTP_HEADER_NAMESPACE, SP_MAIN_HEADER_MEMORY_ALLOC);
			goto error;
		}
		strcpy(spp->address, address);
	}
	else
	{
		source.sin_addr.s_addr = htonl(INADDR_ANY);

		char* def_addr = NULL;    // Default source IP address as a string
		size_t def_addr_size = 0; // Size (in bytes) of def_addr

		if(__get_default_address(&def_addr, &def_addr_size) < 0)
		{
			impact(0, "%s: Failed to get the default source address that the server is bound to\n",
				SP_HTTP_HEADER_NAMESPACE);
			goto error;
		}

		if(def_addr == NULL || def_addr_size == 0)
		{
			impact(0, "%s:%d: BUG! __get_default_address succeeded but did not return an address\n",
				__PRETTY_FUNCTION__, __LINE__);
			goto error;
		}

		if(spp->address) free(spp->address);
		spp->address = def_addr;
	}

	MHD_set_panic_func(&__panic, (void*) spp);
	spp->httpd = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION, port,
		NULL, NULL,
		&__process_request, (void*) spp,
		MHD_OPTION_NOTIFY_COMPLETED, &__finalize_request, (void*) spp,
		MHD_OPTION_CONNECTION_LIMIT, SP_HTTP_BACKLOG,
		MHD_OPTION_SOCK_ADDR, &source,
		MHD_OPTION_EXTERNAL_LOGGER, &__log_microhttpd_messages, (void*) spp,
		MHD_OPTION_END);
	if(spp->httpd == NULL)
	{
		impact(0, "%s: Failed to initialize the server on port %u\n",
			SP_HTTP_HEADER_NAMESPACE, port);
		goto error;
	}

	if(port == 0)
	{
		socklen_t source_len = sizeof(source);  // Length of the socket's source address
		const union MHD_DaemonInfo* httpd_sock; // Socket the server is listening on

		httpd_sock = MHD_get_daemon_info(spp->httpd, MHD_DAEMON_INFO_LISTEN_FD);
		if(httpd_sock == NULL)
		{
			impact(0, "%s: Failed to get the socket the server is listening on\n",
				SP_HTTP_HEADER_NAMESPACE);
			goto error;
		}

		if(getsockname(httpd_sock->listen_fd, (struct sockaddr*) &source, &source_len) == -1)
		{
			impact(0, "%s: Socket could not be allocated: %s\n",
				SP_HTTP_HEADER_NAMESPACE,
				strerror(errno));
			goto error;
		}

		port = ntohs(source.sin_port);
	}

	spp->port = port;

	impact(1, "%s: Bound HTTP server to ADDRESS %s listening on PORT %u with PID %d\n",
		SP_HTTP_HEADER_NAMESPACE,
		spp->address, spp->port, getpid());
	pthread_mutex_unlock(&spp->master_lock);

	return port;

error:
	if(spp->httpd)
	{
		MHD_stop_daemon(spp->httpd);
		spp->httpd = NULL;
	}

	pthread_mutex_unlock(&spp->master_lock);

	return 0;
}

/*!
 * \brief Shut down the web server.
 *
 * \param[in] spp SimplePost instance to act on
 *
 * \retval true The server has been successfully killed.
 * \retval false The server is not running or could not be shut down.
 */
bool simplepost_unbind(simplepost_t spp)
{
	if(spp->httpd == NULL)
	{
		impact(0, "%s: Server is not running\n", SP_HTTP_HEADER_NAMESPACE);
		return false;
	}

	impact(1, "%s: Shutting down ...\n", SP_HTTP_HEADER_NAMESPACE);
	MHD_stop_daemon(spp->httpd);

	#ifdef DEBUG
	impact(2, "%s: %p cleanup complete\n",
		SP_HTTP_HEADER_NAMESPACE, spp->httpd);
	#endif // DEBUG

	spp->httpd = NULL;

	return true;
}

/*!
 * \brief Don't return until the server is shut down.
 *
 * \param[in] spp SimplePost instance to act on
 */
void simplepost_block(const simplepost_t spp)
{
	while(spp->httpd) usleep(SP_HTTP_SLEEP * 1000);
}

/*!
 * \brief Don't return until the server has no more files to serve.
 *
 * \param[in] spp SimplePost instance to act on
 */
void simplepost_block_files(const simplepost_t spp)
{
	while(spp->files_count > 0) usleep(SP_HTTP_SLEEP * 1000);
}

/*!
 * \brief Is the server running?
 *
 * \param[in] spp SimplePost instance to act on
 *
 * \retval true The server is alive!
 * \retval false The server is not running.
 */
bool simplepost_is_alive(const simplepost_t spp)
{
	return (spp->httpd == NULL) ? false : true;
}

/*!
 * \brief Add a file to the list of files being served.
 *
 * \note If and only if url != NULL the final status of this operation will be
 * printed to STDOUT upon successful completion.
 *
 * \param[in] spp   SimplePost instance to act on
 * \param[out] url
 * \parblock
 * Address of the file being served
 *
 * Although you probably need this information, it is generated in a
 * predictable manner. Therefore this argument is technically optional; you
 * may safely make it NULL.
 *
 * The storage for this string will be dynamically allocated. You are
 * responsible for freeing it (unless it is NULL, in which case an error
 * occurred).
 * \endparblock
 * \param[in] file  Name and path of the file to serve
 * \param[in] uri
 * \parblock
 * Uniform Resource Identifier of the file to serve
 *
 * This argument is completely optional. If it is NULL, the name of the file
 * will be used. For example, if file = "/usr/bin/simplepost", the default uri
 * (if uri = NULL) would be "/simplepost". If you specify a uri, it must not
 * already be in use, and it must start with a "/". See the HTTP/1.1
 * specification (RFC 2616) for the requirements of valid URIs.
 * \endparblock
 * \param[in] count
 * \parblock
 * Number of times the file should be served
 *
 * If the count is zero, the number of times will be unlimited.
 * \endparblock
 *
 * \return the number of characters written to the url (excluding the NULL-
 * terminating character)
 */
size_t simplepost_serve_file(
	simplepost_t spp,
	char** url,
	const char* file,
	const char* uri,
	unsigned int count)
{
	struct stat file_status;                   // Status of the input file
	struct simplepost_serve* this_file = NULL; // File to serve
	bool is_file_new = false;                  // Are we adding a new file to serve?
	size_t url_length = 0;                     // Length of the URL
	if(url) *url = NULL;                       // Failsafe

	pthread_mutex_lock(&spp->files_lock);

	if(file == NULL)
	{
		impact(2, "%s:%d: BUG! An input FILE is required\n",
			__PRETTY_FUNCTION__, __LINE__);
		goto abort_insert;
	}

	if(stat(file, &file_status) == -1)
	{
		impact(0, "%s: Cannot serve nonexistent FILE: %s\n",
			SP_HTTP_HEADER_NAMESPACE,
			file);
		goto abort_insert;
	}

	if(!(S_ISREG(file_status.st_mode) || S_ISLNK(file_status.st_mode)))
	{
		impact(0, "%s: FILE not supported: %s\n",
			SP_HTTP_HEADER_NAMESPACE,
			file);
		goto abort_insert;
	}

	#if (defined SP_HTTP_FILES_MAX) && (SP_HTTP_FILES_MAX > 0)
	if(spp->files_count == SP_HTTP_FILES_MAX)
	{
		impact(0, "%s: Cannot serve more than %zu files simultaneously\n",
			SP_HTTP_HEADER_NAMESPACE,
			(size_t) SP_HTTP_FILES_MAX);
		goto abort_insert;
	}
	#else
	#warning "SP_HTTP_FILES_MAX not set - simplepost::files_count may overflow!"
	#endif

	if(uri)
	{
		if(uri[0] != '/')
		{
			impact(0, "%s: Invalid URI: %s\n",
				SP_HTTP_HEADER_NAMESPACE,
				uri);
			goto abort_insert;
		}
		else if(uri[1] == '\0' || strstr(uri, "//"))
		{
			impact(0, "%s: Missing path in URI: %s\n",
				SP_HTTP_HEADER_NAMESPACE,
				uri);
			goto abort_insert;
		}
	}
	else
	{
		uri = file;
		if(strchr(uri, '/'))
		{
			while(*uri != '\0') ++uri;
			while(*uri != '/') --uri;
		}
	}
	if(uri == NULL || uri[0] == '\0')
	{
		impact(0, "%s:%d: BUG! No URI to insert FILE %s\n",
			__PRETTY_FUNCTION__, __LINE__,
			file);
		goto abort_insert;
	}

	if(spp->files)
	{
		for(this_file = spp->files; this_file->next; this_file = this_file->next)
		{
			if(__does_uri_match(this_file->uri, uri)) break;
		}
		if(this_file == NULL)
		{
			impact(0, "%s:%d: BUG! No last file?\n",
				__PRETTY_FUNCTION__, __LINE__);
			goto abort_insert;
		}
		else if(__does_uri_match(this_file->uri, uri) == false)
		{
			this_file = __simplepost_serve_insert_after(this_file, NULL);
			++(spp->files_count);
			is_file_new = true;
		}
		else if(this_file->file && strcmp(this_file->file, file) != 0)
		{
			impact(0, "%s: URI %s is already in use serving FILE %s, not %s\n",
				SP_HTTP_HEADER_NAMESPACE,
				this_file->uri, this_file->file, file);
			goto abort_insert;
		}
	}
	else
	{
		this_file = spp->files = __simplepost_serve_init();
		++(spp->files_count);
		is_file_new = true;
	}
	if(this_file == NULL) goto cannot_insert_file;

	if(this_file->uri == NULL)
	{
		if(uri[0] == '/')
		{
			this_file->uri = (char*) malloc(sizeof(char) * (strlen(uri) + 1));
			if(this_file->uri == NULL) goto cannot_insert_file;
			strcpy(this_file->uri, uri);
		}
		else
		{
			this_file->uri = (char*) malloc(sizeof(char) * (strlen(uri) + 2));
			if(this_file->uri == NULL) goto cannot_insert_file;
			this_file->uri[0] = '/';
			this_file->uri[1] = '\0';
			strcat(this_file->uri, uri);
		}
	}

	if(url)
	{
		size_t url_size; // Size of the URL buffer

		url_size = strlen(spp->address) + strlen(uri) + 50;
		*url = (char*) malloc(sizeof(char) * url_size);
		if(*url == NULL) goto cannot_insert_file;

		url_length = simplestr_get_url(*url, url_size,
			file, spp->address, spp->port, this_file->uri);
		if(url_length == 0) goto cannot_insert_file;
	}

	if(this_file->file == NULL)
	{
		this_file->file = (char*) malloc(sizeof(char) * (strlen(file) + 1));
		if(this_file->file == NULL) goto cannot_insert_file;
		strcpy(this_file->file, file);
	}

	if(is_file_new == false)
	{
		impact(2, "%s: Changing URI %s COUNT from %u to %u\n",
			SP_HTTP_HEADER_NAMESPACE,
			this_file->uri, this_file->count, count);
	}
	this_file->count = count;

	pthread_mutex_unlock(&spp->files_lock);

	if(url_length)
	{
		char count_buf[1024]; // COUNT string of the file being served

		if(simplestr_count_to_str(count_buf, sizeof(count_buf)/sizeof(count_buf[0]), count) == 0)
		{
			impact(1, "%s: Serving %s on %s\n",
				SP_HTTP_HEADER_NAMESPACE,
				file, *url);
		}
		else
		{
			impact(1, "%s: Serving %s on %s %s\n",
				SP_HTTP_HEADER_NAMESPACE,
				file, *url, count_buf);
		}
	}

	return url_length;

cannot_insert_file:
	impact(0, "%s: Cannot insert FILE: %s\n",
		SP_HTTP_HEADER_NAMESPACE,
		file);

abort_insert:
	if(url)
	{
		free(*url);
		*url = NULL;
	}
	if(is_file_new)
	{
		__simplepost_serve_remove(this_file, 1);
		--(spp->files_count);
	}
	pthread_mutex_unlock(&spp->files_lock);

	return 0;
}

/*!
 * \brief Remove a file from the list of files being served.
 *
 * \param[in] spp  SimplePost instance to act on
 * \param[out] uri
 * \parblock
 * Uniform Resource Identifier (URI) or Uniform Resource Locator (URL) of the
 * file to remove
 * \endparblock
 *
 * \retval -1 Internal error
 * \retval  0 The file was not being served
 * \retval  1 The file was successfully removed from the list
 */
short simplepost_purge_file(simplepost_t spp, const char* uri)
{
	if(uri == NULL)
	{
		impact(2, "%s:%d: BUG! An input URI is required\n",
			__PRETTY_FUNCTION__, __LINE__);
		return -1;
	}

	if(strncmp("http://", uri, 7) == 0)
	{
		unsigned short i = 0; // '/' count
		while(i < 3)
		{
			if(*uri == '\0') return 0;
			if(*uri++ == '/') ++i;
		}
	}

	pthread_mutex_lock(&spp->files_lock);
	for(struct simplepost_serve* p = spp->files; p; p = p->next)
	{
		if(p->uri && strcmp(p->uri, uri) == 0)
		{
			impact(1, "%s: Removing URI %s from service ...\n",
				SP_HTTP_HEADER_NAMESPACE,
				uri);

			if(p == spp->files) spp->files = __simplepost_serve_remove(p, 1);
			else __simplepost_serve_remove(p, 1);
			--(spp->files_count);

			pthread_mutex_unlock(&spp->files_lock);
			return 1;
		}
	}
	pthread_mutex_unlock(&spp->files_lock);

	impact(0, "%s: Cannot purge nonexistent URI %s\n",
		SP_HTTP_HEADER_NAMESPACE,
		uri);

	return 0;
}

/*!
 * \brief Initialize a new SimplePost File instance.
 *
 * \return a pointer to the new instance will be returned on success, a NULL
 * pointer if we failed to allocate the requested memory
*/
simplepost_file_t simplepost_file_init()
{
	simplepost_file_t spfp = (simplepost_file_t) malloc(sizeof(struct simplepost_file));
	if(spfp == NULL) return NULL;

	memset(spfp, 0, sizeof(struct simplepost_file));

	return spfp;
}

/*!
 * \brief Free the given SimplePost File instance.
 *
 * \param[in] spfp SimplePost File instance to act on
 */
void simplepost_file_free(simplepost_file_t spfp)
{
	if(spfp == NULL) return;

	if(spfp->prev) spfp->prev->next = NULL;

	while(spfp)
	{
		simplepost_file_t p = spfp;
		spfp = spfp->next;

		if(p->file) free(p->file);
		if(p->url) free(p->url);
		free(p);
	}
}

/*!
 * \brief Get the address the server is bound to.
 *
 * \param[in] spp      SimplePost instance to act on
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
size_t simplepost_get_address(const simplepost_t spp, char** address)
{
	size_t address_length = 0; // Length of the server address
	*address = NULL;           // Failsafe

	if(spp->address == NULL || spp->httpd == NULL) return 0;

	address_length = strlen(spp->address);
	*address = (char*) malloc(sizeof(char) * (address_length + 1));
	if(*address == NULL) return 0;

	strcpy(*address, spp->address);

	return address_length;
}

/*!
 * \brief Get the port the server is listening on.
 *
 * \param[in] spp SimplePost instance to act on
 *
 * \return the server's port number. If this number is zero, an error occurred
 * or (more likely) the server is not running.
 */
unsigned short simplepost_get_port(const simplepost_t spp)
{
	return spp->port;
}

/*!
 * \brief Get a list of the files currently being served.
 *
 * \param[in] spp    SimplePost instance to act on
 * \param[out] files
 * \parblock
 * List of files we are currently hosting
 *
 * This argument may be NULL.
 *
 * The storage for this string will be dynamically allocated. You are
 * responsible for freeing it (unless it is NULL, in which case an error
 * occurred).
 * \endparblock
 *
 * \return the number of files currently being served (or, more accurately,
 * unique URIs)
 */
size_t simplepost_get_files(simplepost_t spp, simplepost_file_t* files)
{
	simplepost_file_t tail; // Last file in the *files list
	size_t files_count = 0; // Number of unique URIs

	if(files == NULL) return spp->files_count;
	tail = *files = NULL;

	pthread_mutex_lock(&spp->files_lock);
	for(const struct simplepost_serve* p = spp->files; p; p = p->next)
	{
		if(tail == NULL)
		{
			tail = *files = simplepost_file_init();
			if(tail == NULL) goto abort_count;
		}
		else
		{
			simplepost_file_t prev = tail;

			tail = simplepost_file_init();
			if(tail == NULL) goto abort_count;

			tail->prev = prev;
			prev->next = tail;
		}

		if(p->file == NULL) goto abort_count;
		tail->file = (char*) malloc(sizeof(char) * (strlen(p->file) + 1));
		if(tail->file == NULL) goto abort_count;
		strcpy(tail->file, p->file);

		if(p->uri == NULL) goto abort_count;
		tail->url = (char*) malloc(sizeof(char) * (strlen(spp->address) + strlen(p->uri) + 50));
		if(tail->url == NULL) goto abort_count;
		if(spp->port == 80) sprintf(tail->url, "http://%s%s", spp->address, p->uri);
		else sprintf(tail->url, "http://%s:%u%s", spp->address, spp->port, p->uri);

		tail->count = p->count;

		++files_count;
	}
	pthread_mutex_unlock(&spp->files_lock);

	return files_count;

abort_count:
	if(*files)
	{
		simplepost_file_free(*files);
		*files = NULL;
	}
	pthread_mutex_unlock(&spp->files_lock);

	return 0;
}
