/*
 * Copyright (C) 2015 Nikos Mavrogiannopoulos
 *
 * Author: Nikos Mavrogiannopoulos
 *
 * This file is part of GnuTLS.
 *
 * GnuTLS is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * GnuTLS is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GnuTLS.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)

int main(void)
{
	exit(77);
}

#else

# include <string.h>
# include <sys/types.h>
# include <netinet/in.h>
# include <sys/socket.h>
# include <sys/wait.h>
# include <arpa/inet.h>
# include <unistd.h>
# include <gnutls/gnutls.h>
# include <gnutls/dtls.h>
# include <signal.h>
# include <assert.h>

# include "utils.h"
# include "cert-common.h"

static void terminate(void);

/* This program tests whether EtM is negotiated as expected.
 */

static void server_log_func(int level, const char *str)
{
	fprintf(stderr, "server|<%d>| %s", level, str);
}

static void client_log_func(int level, const char *str)
{
	fprintf(stderr, "client|<%d>| %s", level, str);
}

/* This tests whether the fallback SCSV is working as intended.
 */

# define MAX_BUF 1024

static void client(int fd, const char *prio, unsigned expect_fail)
{
	int ret;
	char buffer[MAX_BUF + 1];
	gnutls_certificate_credentials_t x509_cred;
	gnutls_session_t session;

	global_init();

	if (debug) {
		gnutls_global_set_log_function(client_log_func);
		gnutls_global_set_log_level(7);
	}

	gnutls_certificate_allocate_credentials(&x509_cred);

	/* Initialize TLS session
	 */
	gnutls_init(&session, GNUTLS_CLIENT);

	/* Use default priorities */
	assert(gnutls_priority_set_direct(session, prio, NULL) >= 0);

	gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE, x509_cred);

	gnutls_transport_set_int(session, fd);

	/* Perform the TLS handshake
	 */
	do {
		ret = gnutls_handshake(session);
	}
	while (ret < 0 && gnutls_error_is_fatal(ret) == 0);

	if (expect_fail) {
		goto end;
	}

	if (ret < 0) {
		fail("client: Handshake failed\n");
		gnutls_perror(ret);
		exit(1);
	} else {
		if (debug)
			success("client: Handshake was completed\n");
	}

	if (debug)
		success("client: TLS version is: %s\n",
			gnutls_protocol_get_name
			(gnutls_protocol_get_version(session)));

	do {
		do {
			ret = gnutls_record_recv(session, buffer, MAX_BUF);
		} while (ret == GNUTLS_E_AGAIN || ret == GNUTLS_E_INTERRUPTED);
	} while (ret > 0);

	if (ret == 0) {
		if (debug)
			success("client: Peer has closed the TLS connection\n");
		goto end;
	} else if (ret < 0) {
		if (ret != 0) {
			fail("client: Error: %s\n", gnutls_strerror(ret));
			exit(1);
		}
	}

	gnutls_bye(session, GNUTLS_SHUT_WR);

 end:

	close(fd);

	gnutls_deinit(session);

	gnutls_certificate_free_credentials(x509_cred);

	gnutls_global_deinit();
}

/* These are global */
pid_t child;

static void terminate(void)
{
	assert(child);
	kill(child, SIGTERM);
	exit(1);
}

static void server(int fd, const char *prio, unsigned expect_fail)
{
	int ret;
	char buffer[MAX_BUF + 1];
	gnutls_session_t session;
	gnutls_certificate_credentials_t x509_cred;
	unsigned to_send = sizeof(buffer) / 4;

	/* this must be called once in the program
	 */
	global_init();
	memset(buffer, 0, sizeof(buffer));

	if (debug) {
		gnutls_global_set_log_function(server_log_func);
		gnutls_global_set_log_level(4711);
	}

	gnutls_certificate_allocate_credentials(&x509_cred);
	gnutls_certificate_set_x509_key_mem(x509_cred, &server_cert,
					    &server_key, GNUTLS_X509_FMT_PEM);

	gnutls_init(&session, GNUTLS_SERVER);

	/* avoid calling all the priority functions, since the defaults
	 * are adequate.
	 */
	assert(gnutls_priority_set_direct(session, prio, NULL) >= 0);

	gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE, x509_cred);

	gnutls_transport_set_int(session, fd);

	do {
		ret = gnutls_handshake(session);
	} while (ret < 0 && gnutls_error_is_fatal(ret) == 0);

	if (expect_fail) {
		if (ret == GNUTLS_E_INAPPROPRIATE_FALLBACK) {
			if (debug)
				success
				    ("server: received inappropriate fallback error\n");
			goto cleanup;
		} else {
			fail("server: received unexpected error: %s\n",
			     gnutls_strerror(ret));
		}
	}

	if (ret < 0) {
		close(fd);
		gnutls_deinit(session);
		fail("server: Handshake has failed (%s)\n\n",
		     gnutls_strerror(ret));
		terminate();
	}

	if (debug)
		success("server: Handshake was completed\n");

	if (debug)
		success("server: TLS version is: %s\n",
			gnutls_protocol_get_name
			(gnutls_protocol_get_version(session)));

	do {
		do {
			ret =
			    gnutls_record_send(session, buffer, sizeof(buffer));
		} while (ret == GNUTLS_E_AGAIN || ret == GNUTLS_E_INTERRUPTED);

		if (ret < 0) {
			fail("Error sending %d byte packet: %s\n", to_send,
			     gnutls_strerror(ret));
			terminate();
		}
		to_send++;
	}
	while (to_send < 64);

	to_send = -1;
	/* do not wait for the peer to close the connection.
	 */
	gnutls_bye(session, GNUTLS_SHUT_WR);
 cleanup:
	close(fd);
	gnutls_deinit(session);

	gnutls_certificate_free_credentials(x509_cred);

	gnutls_global_deinit();

	if (debug)
		success("server: finished\n");
}

static void start(const char *server_prio, const char *cli_prio,
		  unsigned expect_fail)
{
	int fd[2];
	int ret, status = 0;

	ret = socketpair(AF_UNIX, SOCK_STREAM, 0, fd);
	if (ret < 0) {
		perror("socketpair");
		exit(1);
	}

	child = fork();
	if (child < 0) {
		perror("fork");
		fail("fork");
		exit(1);
	}

	if (child) {
		/* parent */
		close(fd[1]);
		server(fd[0], server_prio, expect_fail);
		waitpid(child, &status, 0);
		check_wait_status(status);
	} else {
		close(fd[0]);
		client(fd[1], cli_prio, expect_fail);
		exit(0);
	}
}

static void ch_handler(int sig)
{
	return;
}

void doit(void)
{
	signal(SIGCHLD, ch_handler);
	signal(SIGPIPE, SIG_IGN);

	start("NORMAL", "NORMAL", 0);
	start("NORMAL:-VERS-TLS-ALL:+VERS-TLS1.0:+VERS-TLS1.1:+VERS-TLS1.2",
	      "NORMAL:-VERS-TLS-ALL:+VERS-TLS1.0:+VERS-TLS1.1:+VERS-TLS1.2", 0);
	start("NORMAL:-VERS-TLS-ALL:+VERS-TLS1.0:+VERS-TLS1.1:+VERS-TLS1.2",
	      "NORMAL:-VERS-TLS-ALL:+VERS-TLS1.0:+VERS-TLS1.1:+VERS-TLS1.2:%FALLBACK_SCSV",
	      0);
	start("NORMAL", "NORMAL:%FALLBACK_SCSV", 0);
	start("NORMAL:-VERS-TLS-ALL:+VERS-TLS1.1",
	      "NORMAL:-VERS-TLS-ALL:+VERS-TLS1.1:%FALLBACK_SCSV", 0);
	start("NORMAL", "NORMAL:-VERS-TLS-ALL:+VERS-TLS1.1:%FALLBACK_SCSV", 1);
	/* Check whether a TLS1.3 server rejects a TLS1.2 client which includes the SCSV */
	start("NORMAL:+VERS-TLS1.3:+VERS-TLS1.2",
	      "NORMAL:-VERS-TLS-ALL:+VERS-TLS1.2:%FALLBACK_SCSV", 1);
	start("NORMAL:+VERS-TLS1.3:+VERS-TLS1.2",
	      "NORMAL:-VERS-TLS-ALL:+VERS-TLS1.3:+VERS-TLS1.2:%FALLBACK_SCSV",
	      0);
}

#endif				/* _WIN32 */
