/********************************************************************\
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652       *
 * Boston, MA  02111-1307,  USA       gnu@gnu.org                   *
 *                                                                  *
 \********************************************************************/

/*
 * $Header$
 */
/**
  @file util.c
  @brief Misc utility functions
  @author Copyright (C) 2004 Philippe April <papril777@yahoo.com>
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <errno.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/unistd.h>
#include <netinet/in.h>
#include <sys/ioctl.h>

#ifdef __linux__
#include <net/if.h>
#endif

#include <string.h>
#include <pthread.h>
#include <netdb.h>

#include "client_list.h"
#include "safe.h"
#include "util.h"
#include "conf.h"
#include "debug.h"

static pthread_mutex_t ghbn_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Defined in ping_thread.c */
extern time_t started_time;

/* Defined in clientlist.c */
extern	pthread_mutex_t	client_list_mutex;
extern	pthread_mutex_t	config_mutex;

/* XXX Do these need to be locked ? */
static time_t last_online_time = 0;
static time_t last_offline_time = 0;
static time_t last_auth_online_time = 0;
static time_t last_auth_offline_time = 0;

/** Fork a child and execute a shell command, the parent
 * process waits for the child to return and returns the child's exit()
 * value.
 * @return Return code of the command
 */
int
execute(char *cmd_line, int quiet)
{
    int pid,
        status,
        rc;

    const char *new_argv[4];
    new_argv[0] = "/bin/sh";
    new_argv[1] = "-c";
    new_argv[2] = cmd_line;
    new_argv[3] = NULL;

    if ((pid = fork()) < 0) {    /* fork a child process           */
        debug(LOG_ERR, "fork(): %s", strerror(errno));
        exit(1);
    } else if (pid == 0) {    /* for the child process:         */
        /* We don't want to see any errors if quiet flag is on */
        if (quiet) close(2);
        if (execvp("/bin/sh", (char *const *)new_argv) < 0) {    /* execute the command  */
            debug(LOG_ERR, "execvp(): %s", strerror(errno));
            exit(1);
        }
    } else {        /* for the parent:      */
        do {
            rc = wait(&status);
        } while (rc != pid && rc != -1);    /* wait for completion  */
    }

    return (WEXITSTATUS(status));
}

struct in_addr *
wd_gethostbyname(const char *name)
{
	struct hostent *he;
	struct in_addr *h_addr, *in_addr_temp;

	/* XXX Calling function is reponsible for free() */

	h_addr = safe_malloc(sizeof(struct in_addr));
	
	LOCK_GHBN();

	he = gethostbyname(name);

	if (he == NULL) {
		free(h_addr);
		UNLOCK_GHBN();
		return NULL;
	}

	mark_online();

	in_addr_temp = (struct in_addr *)he->h_addr_list[0];
	h_addr->s_addr = in_addr_temp->s_addr;
	
	UNLOCK_GHBN();

	return h_addr;
}

char *get_iface_ip(char *ifname) {
#ifdef __linux__
    struct ifreq if_data;
#endif
    struct in_addr in;
    char *ip_str;
    int sockd;
    u_int32_t ip;

#ifdef __linux__
    
    /* Create a socket */
    if ((sockd = socket (AF_INET, SOCK_PACKET, htons(0x8086))) < 0) {
        debug(LOG_ERR, "socket(): %s", strerror(errno));
        return NULL;
    }

    /* Get IP of internal interface */
    strcpy (if_data.ifr_name, ifname);

    /* Get the IP address */
    if (ioctl (sockd, SIOCGIFADDR, &if_data) < 0) {
        debug(LOG_ERR, "ioctl(): SIOCGIFADDR %s", strerror(errno));
        return NULL;
    }
    memcpy ((void *) &ip, (void *) &if_data.ifr_addr.sa_data + 2, 4);
    in.s_addr = ip;

    ip_str = (char *)inet_ntoa(in);
    return safe_strdup(ip_str);
#else
    return safe_strdup("0.0.0.0");
#endif
}

void mark_online() {
	int before;
	int after;

	before = is_online();
	time(&last_online_time);
	after = is_online();

	if (before != after) {
		debug(LOG_INFO, "ONLINE status became %s", (after ? "ON" : "OFF"));
	}

}

void mark_offline() {
	int before;
	int after;

	before = is_online();
	time(&last_offline_time);
	after = is_online();

	if (before != after) {
		debug(LOG_INFO, "ONLINE status became %s", (after ? "ON" : "OFF"));
	}

	/* If we're offline it definately means the auth server is offline */
	mark_auth_offline();

}

int is_online() {
	if (last_online_time == 0 || (last_offline_time - last_online_time) >= (config_get_config()->checkinterval * 2) ) {
		/* We're probably offline */
		return (0);
	}
	else {
		/* We're probably online */
		return (1);
	}
}

void mark_auth_online() {
	int before;
	int after;

	before = is_auth_online();
	time(&last_auth_online_time);
	after = is_auth_online();

	if (before != after) {
		debug(LOG_INFO, "AUTH_ONLINE status became %s", (after ? "ON" : "OFF"));
	}

	/* If auth server is online it means we're definately online */
	mark_online();

}

void mark_auth_offline() {
	int before;
	int after;

	before = is_auth_online();
	time(&last_auth_offline_time);
	after = is_auth_online();

	if (before != after) {
		debug(LOG_INFO, "AUTH_ONLINE status became %s", (after ? "ON" : "OFF"));
	}

}

int is_auth_online() {
	if (!is_online()) {
		/* If we're not online auth is definately not online :) */
		return (0);
	}
	else if (last_auth_online_time == 0 || (last_auth_offline_time - last_auth_online_time) >= (config_get_config()->checkinterval * 2) ) {
		/* Auth is  probably offline */
		return (0);
	}
	else {
		/* Auth is probably online */
		return (1);
	}
}

/*
 * @return A string containing humn-readable status text. MUST BE free()d by caller
 */
char * get_status_text() {
	char buffer[STATUS_BUF_SIZ];
	ssize_t len;
	s_config *config;
	t_auth_serv *auth_server;
	t_client	*first;
	int		count;
	unsigned long int uptime = 0;
	unsigned int days = 0, hours = 0, minutes = 0, seconds = 0;

	config = config_get_config();
	
	len = 0;
	snprintf(buffer, (sizeof(buffer) - len), "WiFiDog status\n\n");
	len = strlen(buffer);

	uptime = time(NULL) - started_time;
	days    = uptime / (24 * 60 * 60);
	uptime -= days * (24 * 60 * 60);
	hours   = uptime / (60 * 60);
	uptime -= hours * (60 * 60);
	minutes = uptime / 60;
	uptime -= minutes * 60;
	seconds = uptime;

	snprintf((buffer + len), (sizeof(buffer) - len), "Uptime: %ud %uh %um %us\n", days, hours, minutes, seconds);
	len = strlen(buffer);
	
	snprintf((buffer + len), (sizeof(buffer) - len), "is_online: %s\n", (is_online() ? "yes" : "no"));
	len = strlen(buffer);
	
	snprintf((buffer + len), (sizeof(buffer) - len), "is_auth_online: %s\n\n", (is_auth_online() ? "yes" : "no"));
	len = strlen(buffer);

	LOCK_CLIENT_LIST();
	
	first = client_get_first_client();
	
	if (first == NULL) {
		count = 0;
	} else {
		count = 1;
		while (first->next != NULL) {
			first = first->next;
			count++;
		}
	}
	
	snprintf((buffer + len), (sizeof(buffer) - len), "%d clients "
			"connected.\n", count);
	len = strlen(buffer);

	first = client_get_first_client();

	count = 0;
	while (first != NULL) {
		snprintf((buffer + len), (sizeof(buffer) - len), "Client %d\t"
				"Ip: %s\tMac: %s\tToken: %s\n", count, 
				first->ip, first->mac, first->token);
		len = strlen(buffer);

		snprintf((buffer + len), (sizeof(buffer) - len), "\tIn: %llu\t"
				"Out: %llu\n" , first->counters.incoming,
				first->counters.outgoing);
		len = strlen(buffer);

		count++;
		first = first->next;
	}
	
	UNLOCK_CLIENT_LIST();

	LOCK_CONFIG();
    
	snprintf((buffer + len), (sizeof(buffer) - len), "\nAuthentication servers:\n");
	len = strlen(buffer);

	for (auth_server = config->auth_servers; auth_server != NULL; auth_server = auth_server->next) {
		snprintf((buffer + len), (sizeof(buffer) - len), "  Host: %s (%s)\n", auth_server->authserv_hostname, auth_server->last_ip);
		len = strlen(buffer);
	}

	UNLOCK_CONFIG();

	return safe_strdup(buffer);
}
