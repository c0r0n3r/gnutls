/*
 * Copyright (C) 2004-2012 Free Software Foundation, Inc.
 * Copyright (C) 2013 Adam Sampson <ats@offog.org>
 *
 * Author: Simon Josefsson
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

/* Parts copied from GnuTLS example programs. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "cert-common.h"

#if defined(_WIN32)

/* socketpair isn't supported on Win32. */
int main(int argc, char **argv)
{
	exit(77);
}

#else

# include <string.h>
# include <sys/types.h>
# include <sys/socket.h>
# if !defined(_WIN32)
#  include <sys/wait.h>
# endif
# include <unistd.h>
# include <gnutls/gnutls.h>

# include "utils.h"

# include "ex-session-info.c"
# include "ex-x509-info.c"

pid_t child;

static void tls_log_func(int level, const char *str)
{
	fprintf(stderr, "%s |<%d>| %s", child ? "server" : "client", level,
		str);
}

# define MAX_BUF 1024
# define MSG "Hello TLS"

static void client(int sd, const char *prio)
{
	int ret, ii;
	gnutls_session_t session;
	char buffer[MAX_BUF + 1];
	gnutls_certificate_credentials_t xcred;
	gnutls_certificate_credentials_t tst_cred;

	global_init();

	gnutls_global_set_log_function(tls_log_func);
	if (debug)
		gnutls_global_set_log_level(6);

	gnutls_certificate_allocate_credentials(&xcred);

	/* sets the trusted cas file
	 */
	gnutls_certificate_set_x509_trust_mem(xcred, &ca3_cert,
					      GNUTLS_X509_FMT_PEM);
	gnutls_certificate_set_x509_key_mem(xcred, &cli_ca3_cert, &cli_ca3_key,
					    GNUTLS_X509_FMT_PEM);

	/* Initialize TLS session
	 */
	gnutls_init(&session, GNUTLS_CLIENT);

	assert(gnutls_priority_set_direct(session, prio, NULL) >= 0);

	/* put the x509 credentials to the current session
	 */
	gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE, xcred);

	gnutls_transport_set_int(session, sd);

	/* Perform the TLS handshake
	 */
	ret = gnutls_handshake(session);

	if (ret < 0) {
		fail("client: Handshake failed\n");
		gnutls_perror(ret);
		goto end;
	} else if (debug) {
		success("client: Handshake was completed\n");
	}

	if (debug)
		success("client: TLS version is: %s\n",
			gnutls_protocol_get_name
			(gnutls_protocol_get_version(session)));

	/* see the Getting peer's information example */
	if (debug)
		print_info(session);

	ret =
	    gnutls_credentials_get(session, GNUTLS_CRD_CERTIFICATE,
				   (void **)&tst_cred);
	if (ret < 0) {
		fail("client: gnutls_credentials_get failed: %s\n",
		     gnutls_strerror(ret));
	}
	if (tst_cred != xcred) {
		fail("client: gnutls_credentials_get returned invalid value\n");
	}

	ret = gnutls_record_send(session, MSG, strlen(MSG));

	if (ret == strlen(MSG)) {
		if (debug)
			success("client: sent record.\n");
	} else {
		fail("client: failed to send record.\n");
		gnutls_perror(ret);
		goto end;
	}

	do {
		ret = gnutls_record_recv(session, buffer, MAX_BUF);
	} while (ret == GNUTLS_E_AGAIN || ret == GNUTLS_E_INTERRUPTED);

	if (debug)
		success("client: recv returned %d.\n", ret);

	if (ret == GNUTLS_E_REHANDSHAKE) {
		if (debug)
			success("client: doing handshake!\n");
		ret = gnutls_handshake(session);
		if (ret == 0) {
			if (debug)
				success
				    ("client: handshake complete, reading again.\n");
			ret = gnutls_record_recv(session, buffer, MAX_BUF);
		} else {
			fail("client: handshake failed.\n");
		}
	}

	if (ret == 0) {
		if (debug)
			success("client: Peer has closed the TLS connection\n");
		goto end;
	} else if (ret < 0) {
		fail("client: Error: %s\n", gnutls_strerror(ret));
		goto end;
	}

	if (debug) {
		printf("- Received %d bytes: ", ret);
		for (ii = 0; ii < ret; ii++) {
			fputc(buffer[ii], stdout);
		}
		fputs("\n", stdout);
	}

	gnutls_bye(session, GNUTLS_SHUT_RDWR);

 end:

	close(sd);

	gnutls_deinit(session);

	gnutls_certificate_free_credentials(xcred);

	gnutls_global_deinit();
}

/* This is a sample TLS 1.0 echo server, using X.509 authentication.
 */

# define MAX_BUF 1024
# define DH_BITS 1024

static void server(int sd, const char *prio)
{
	int ret;
	gnutls_session_t session;
	char buffer[MAX_BUF + 1];
	gnutls_certificate_credentials_t x509_cred;

	global_init();

	gnutls_global_set_log_function(tls_log_func);
	if (debug)
		gnutls_global_set_log_level(6);

	gnutls_certificate_allocate_credentials(&x509_cred);
	gnutls_certificate_set_x509_trust_mem(x509_cred, &ca3_cert,
					      GNUTLS_X509_FMT_PEM);

	gnutls_certificate_set_x509_key_mem(x509_cred,
					    &server_ca3_localhost_cert,
					    &server_ca3_key,
					    GNUTLS_X509_FMT_PEM);

	if (debug)
		success("Launched, generating DH parameters...\n");

	gnutls_init(&session, GNUTLS_SERVER);

	assert(gnutls_priority_set_direct(session, prio, NULL) >= 0);

	gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE, x509_cred);

	/* request client certificate if any.
	   Moved to later on to be able to test re-handshakes.
	   gnutls_certificate_server_set_request (session, GNUTLS_CERT_REQUEST);
	 */

	gnutls_dh_set_prime_bits(session, DH_BITS);

	gnutls_transport_set_int(session, sd);
	ret = gnutls_handshake(session);
	if (ret < 0) {
		close(sd);
		gnutls_deinit(session);
		fail("server: Handshake has failed (%s)\n\n",
		     gnutls_strerror(ret));
		return;
	}
	if (debug) {
		success("server: Handshake was completed\n");
		success("server: TLS version is: %s\n",
			gnutls_protocol_get_name
			(gnutls_protocol_get_version(session)));
	}

	/* see the Getting peer's information example */
	if (debug)
		print_info(session);

	for (;;) {
		memset(buffer, 0, MAX_BUF + 1);
		do {
			ret = gnutls_record_recv(session, buffer, MAX_BUF);
		} while (ret == GNUTLS_E_AGAIN || ret == GNUTLS_E_INTERRUPTED);

		if (ret == 0) {
			if (debug)
				success
				    ("server: Peer has closed the GnuTLS connection\n");
			break;
		} else if (ret < 0) {
			fail("server: Received corrupted data(%s). Closing...\n", gnutls_strerror(ret));
			break;
		} else if (ret > 0) {
			gnutls_certificate_server_set_request(session,
							      GNUTLS_CERT_REQUEST);

			if (debug)
				success
				    ("server: got data, forcing rehandshake.\n");

			ret = gnutls_rehandshake(session);
			if (ret < 0) {
				fail("server: rehandshake failed\n");
				gnutls_perror(ret);
				break;
			}

			ret = gnutls_handshake(session);
			if (ret < 0) {
				fail("server: (re)handshake failed\n");
				gnutls_perror(ret);
				break;
			}

			if (debug)
				success("server: rehandshake complete.\n");

			/* echo data back to the client
			 */
			gnutls_record_send(session, buffer, strlen(buffer));
		}
	}
	/* do not wait for the peer to close the connection.
	 */
	gnutls_bye(session, GNUTLS_SHUT_WR);

	close(sd);
	gnutls_deinit(session);

	gnutls_certificate_free_credentials(x509_cred);

	gnutls_global_deinit();

	if (debug)
		success("server: finished\n");
}

static
void start(const char *prio)
{
	int sockets[2];
	int err;

	success("trying %s\n", prio);

	signal(SIGPIPE, SIG_IGN);

	err = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
	if (err == -1) {
		perror("socketpair");
		fail("socketpair failed\n");
		return;
	}

	child = fork();
	if (child < 0) {
		perror("fork");
		fail("fork");
		return;
	}

	if (child) {
		int status;

		close(sockets[1]);
		server(sockets[0], prio);
		wait(&status);
		check_wait_status(status);
	} else {
		close(sockets[0]);
		client(sockets[1], prio);
		exit(0);
	}
}

void doit(void)
{
	start("NORMAL:-VERS-ALL:+VERS-TLS1.3");
	start("NORMAL:-VERS-ALL:+VERS-TLS1.2");
	start("NORMAL");
}

#endif				/* _WIN32 */
