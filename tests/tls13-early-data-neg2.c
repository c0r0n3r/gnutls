/*
 * Copyright (C) 2012-2018 Free Software Foundation, Inc.
 *
 * Author: Nikos Mavrogiannopoulos, Daiki Ueno
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>
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
# include <gnutls/crypto.h>
# include <gnutls/dtls.h>
# include <signal.h>
# include <sys/wait.h>
# include <assert.h>

# include "cert-common.h"
# include "utils.h"
# include "virt-time.h"

/* This program checks that early data is refused upon resumption failure.
 */

static void server_log_func(int level, const char *str)
{
	fprintf(stderr, "server|<%d>| %s", level, str);
}

static void client_log_func(int level, const char *str)
{
	fprintf(stderr, "client|<%d>| %s", level, str);
}

# define SESSIONS 2
# define MAX_BUF 1024
# define MSG "Hello TLS"
# define EARLY_MSG "Hello TLS, it's early"
# define PRIORITY "NORMAL:-VERS-ALL:+VERS-TLS1.3"

static void client(int sds[])
{
	int ret;
	char buffer[MAX_BUF + 1];
	gnutls_certificate_credentials_t x509_cred;
	gnutls_session_t session;
	int t;
	gnutls_datum_t session_data = { NULL, 0 };

	if (debug) {
		gnutls_global_set_log_function(client_log_func);
		gnutls_global_set_log_level(7);
	}

	/* Generate the same ob_ticket_age value, which affects the
	 * binder calculation.
	 */
	virt_time_init();

	gnutls_certificate_allocate_credentials(&x509_cred);

	for (t = 0; t < SESSIONS; t++) {
		int sd = sds[t];

		assert(gnutls_init(&session, GNUTLS_CLIENT) >= 0);
		assert(gnutls_priority_set_direct(session, PRIORITY, NULL) >=
		       0);

		gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE,
				       x509_cred);

		gnutls_transport_set_int(session, sd);

		if (t > 0) {
			assert(gnutls_session_set_data
			       (session, session_data.data,
				session_data.size) >= 0);
			assert(gnutls_record_send_early_data
			       (session, EARLY_MSG, sizeof(EARLY_MSG)) >= 0);
		}

		/* Perform the TLS handshake
		 */
		gnutls_handshake_set_timeout(session, get_timeout());
		do {
			ret = gnutls_handshake(session);
		}
		while (ret < 0 && gnutls_error_is_fatal(ret) == 0);

		if (ret < 0) {
			fail("client: Handshake failed: %s\n",
			     gnutls_strerror(ret));
		} else {
			if (debug)
				success("client: Handshake was completed\n");
		}

		if (t == 0) {
			/* get the session data size */
			ret = gnutls_session_get_data2(session, &session_data);
			if (ret < 0)
				fail("client: Getting resume data failed\n");
		}

		if (gnutls_session_is_resumed(session)) {
			fail("client: Session unexpectedly resumed (%d)\n", t);
		}

		gnutls_record_send(session, MSG, strlen(MSG));

		do {
			ret =
			    gnutls_record_recv(session, buffer, sizeof(buffer));
		} while (ret == GNUTLS_E_AGAIN);
		if (ret == 0) {
			if (debug)
				success
				    ("client: Peer has closed the TLS connection\n");
			goto end;
		} else if (ret < 0) {
			fail("client: Error: %s\n", gnutls_strerror(ret));
		}

		gnutls_bye(session, GNUTLS_SHUT_WR);

		close(sd);

		gnutls_deinit(session);
	}

 end:
	gnutls_free(session_data.data);
	gnutls_certificate_free_credentials(x509_cred);
}

static pid_t child;

# define MAX_CLIENT_HELLO_RECORDED 10

struct storage_st {
	gnutls_datum_t entries[MAX_CLIENT_HELLO_RECORDED];
	size_t num_entries;
};

static int
storage_add(void *ptr, time_t expires, const gnutls_datum_t * key,
	    const gnutls_datum_t * value)
{
	struct storage_st *storage = ptr;
	gnutls_datum_t *datum;
	size_t i;

	for (i = 0; i < storage->num_entries; i++) {
		if (key->size == storage->entries[i].size &&
		    memcmp(storage->entries[i].data, key->data,
			   key->size) == 0) {
			return GNUTLS_E_DB_ENTRY_EXISTS;
		}
	}

	/* If the maximum number of ClientHello exceeded, reject early
	 * data until next time.
	 */
	if (storage->num_entries == MAX_CLIENT_HELLO_RECORDED)
		return GNUTLS_E_DB_ERROR;

	datum = &storage->entries[storage->num_entries];
	datum->data = gnutls_malloc(key->size);
	if (!datum->data)
		return GNUTLS_E_MEMORY_ERROR;
	memcpy(datum->data, key->data, key->size);
	datum->size = key->size;

	storage->num_entries++;

	return 0;
}

static void storage_clear(struct storage_st *storage)
{
	size_t i;

	for (i = 0; i < storage->num_entries; i++)
		gnutls_free(storage->entries[i].data);
	storage->num_entries = 0;
}

static void server(int sds[])
{
	int ret;
	char buffer[MAX_BUF + 1];
	gnutls_session_t session;
	gnutls_certificate_credentials_t x509_cred;
	gnutls_datum_t session_ticket_key = { NULL, 0 };
	struct storage_st storage;
	gnutls_anti_replay_t anti_replay;
	int t;

	/* this must be called once in the program
	 */
	global_init();
	memset(buffer, 0, sizeof(buffer));
	memset(&storage, 0, sizeof(storage));

	if (debug) {
		gnutls_global_set_log_function(server_log_func);
		gnutls_global_set_log_level(4711);
	}

	gnutls_certificate_allocate_credentials(&x509_cred);
	gnutls_certificate_set_x509_key_mem(x509_cred, &server_cert,
					    &server_key, GNUTLS_X509_FMT_PEM);

	ret = gnutls_anti_replay_init(&anti_replay);
	if (ret < 0)
		fail("server: failed to initialize anti-replay\n");
	gnutls_anti_replay_set_add_function(anti_replay, storage_add);
	gnutls_anti_replay_set_ptr(anti_replay, &storage);

	for (t = 0; t < SESSIONS; t++) {
		int sd = sds[t];

		success("=== session %d ===\n", t);

		assert(gnutls_init
		       (&session,
			GNUTLS_SERVER | GNUTLS_ENABLE_EARLY_DATA) >= 0);

		assert(gnutls_priority_set_direct(session, PRIORITY, NULL) >=
		       0);

		gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE,
				       x509_cred);

		/* Intentionally overwrite the previous key to cause resumption
		 * failure. */
		gnutls_session_ticket_key_generate(&session_ticket_key);

		gnutls_session_ticket_enable_server(session,
						    &session_ticket_key);

		gnutls_anti_replay_enable(session, anti_replay);

		gnutls_transport_set_int(session, sd);

		do {
			ret = gnutls_handshake(session);
		} while (ret < 0 && gnutls_error_is_fatal(ret) == 0);

		if (ret < 0) {
			gnutls_deinit(session);
			fail("server[%d]: Handshake has failed (%s)\n\n",
			     t, gnutls_strerror(ret));
		}
		if (debug)
			success("server: Handshake was completed\n");

		if (gnutls_session_is_resumed(session)) {
			fail("server: Session unexpectedly resumed (%d)\n", t);
		}

		if (gnutls_session_get_flags(session) &
		    GNUTLS_SFLAGS_EARLY_DATA) {
			fail("server: Unexpected early data received (%d)\n",
			     t);
		}

		for (;;) {
			memset(buffer, 0, MAX_BUF + 1);
			ret = gnutls_record_recv(session, buffer, MAX_BUF);

			if (ret == 0) {
				if (debug)
					success
					    ("server: Peer has closed the GnuTLS connection\n");
				break;
			} else if (ret < 0) {
				kill(child, SIGTERM);
				fail("server: Received corrupted data(%d). Closing...\n", ret);
			} else if (ret > 0) {
				/* echo data back to the client
				 */
				gnutls_record_send(session, buffer,
						   strlen(buffer));
			}
		}
		/* do not wait for the peer to close the connection.
		 */
		gnutls_bye(session, GNUTLS_SHUT_WR);

		close(sd);

		gnutls_deinit(session);

		gnutls_free(session_ticket_key.data);
	}

	gnutls_anti_replay_deinit(anti_replay);

	storage_clear(&storage);

	gnutls_certificate_free_credentials(x509_cred);

	if (debug)
		success("server: finished\n");
}

void doit(void)
{
	int client_sds[SESSIONS], server_sds[SESSIONS];
	int i, status = 0;
	int ret;

	signal(SIGCHLD, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);

	for (i = 0; i < SESSIONS; i++) {
		int sockets[2];

		ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
		if (ret < 0) {
			perror("socketpair");
			exit(1);
		}

		server_sds[i] = sockets[0];
		client_sds[i] = sockets[1];
	}

	child = fork();
	if (child < 0) {
		perror("fork");
		fail("fork");
		exit(1);
	}

	if (child) {
		/* parent */
		for (i = 0; i < SESSIONS; i++)
			close(client_sds[i]);
		server(server_sds);
		wait(&status);
		check_wait_status(status);
	} else {
		for (i = 0; i < SESSIONS; i++)
			close(server_sds[i]);
		client(client_sds);
		exit(0);
	}
}

#endif				/* _WIN32 */
