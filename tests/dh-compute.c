/*
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * Author: Simo Sorce
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

/* This program tests functionality of DH exchanges */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <gnutls/gnutls.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "utils.h"

#ifdef ENABLE_FIPS140
int _gnutls_dh_generate_key(gnutls_dh_params_t dh_params,
			    gnutls_datum_t * priv_key,
			    gnutls_datum_t * pub_key);

int _gnutls_dh_compute_key(gnutls_dh_params_t dh_params,
			   const gnutls_datum_t * priv_key,
			   const gnutls_datum_t * pub_key,
			   const gnutls_datum_t * peer_key, gnutls_datum_t * Z);

static void params(gnutls_dh_params_t * dh_params, const gnutls_datum_t * p,
		   const gnutls_datum_t * q, const gnutls_datum_t * g)
{
	int ret;

	ret = gnutls_dh_params_init(dh_params);
	if (ret != 0)
		fail("error\n");

	ret = gnutls_dh_params_import_raw3(*dh_params, p, q, g);
	if (ret != 0)
		fail("error\n");
}

static void genkey(const gnutls_dh_params_t dh_params,
		   gnutls_datum_t * priv_key, gnutls_datum_t * pub_key)
{
	int ret;

	ret = _gnutls_dh_generate_key(dh_params, priv_key, pub_key);
	if (ret != 0)
		fail("error\n");
}

static void compute_key(const char *name, const gnutls_dh_params_t dh_params,
			const gnutls_datum_t * priv_key,
			const gnutls_datum_t * pub_key,
			const gnutls_datum_t * peer_key, int expect_error,
			gnutls_datum_t * result, bool expect_success)
{
	gnutls_datum_t Z = { 0 };
	bool success;
	int ret;

	ret = _gnutls_dh_compute_key(dh_params, priv_key, pub_key,
				     peer_key, &Z);
	if (expect_error != ret)
		fail("%s: error %d (expected %d)\n", name, ret, expect_error);

	if (result) {
		success = (Z.size != result->size &&
			   memcmp(Z.data, result->data, Z.size));
		if (success != expect_success)
			fail("%s: failed to match result\n", name);
	}
	gnutls_free(Z.data);
}

struct dh_test_data {
	const char *name;
	const gnutls_datum_t prime;
	const gnutls_datum_t q;
	const gnutls_datum_t generator;
	const gnutls_datum_t peer_key;
	int expected_error;
	gnutls_fips140_operation_state_t fips_state_genkey;
	gnutls_fips140_operation_state_t fips_state_compute_key;
};

void doit(void)
{
	struct dh_test_data test_data[] = {
		{
		 "[y == 0]",
		 gnutls_ffdhe_2048_group_prime,
		 gnutls_ffdhe_2048_group_q,
		 gnutls_ffdhe_2048_group_generator,
		 {(void *)"\x00", 1},
		 GNUTLS_E_MPI_SCAN_FAILED,
		 GNUTLS_FIPS140_OP_APPROVED,
		 /* does not reach _wrap_nettle_pk_derive */
		 GNUTLS_FIPS140_OP_INITIAL,
		 },
		{
		 "[y < 2]",
		 gnutls_ffdhe_2048_group_prime,
		 gnutls_ffdhe_2048_group_q,
		 gnutls_ffdhe_2048_group_generator,
		 {(void *)"\x01", 1},
		 GNUTLS_E_RECEIVED_ILLEGAL_PARAMETER,
		 GNUTLS_FIPS140_OP_APPROVED,
		 GNUTLS_FIPS140_OP_ERROR,
		 },
		{
		 "[y > p - 2]",
		 gnutls_ffdhe_2048_group_prime,
		 gnutls_ffdhe_2048_group_q,
		 gnutls_ffdhe_2048_group_generator,
		 gnutls_ffdhe_2048_group_prime,
		 GNUTLS_E_RECEIVED_ILLEGAL_PARAMETER,
		 GNUTLS_FIPS140_OP_APPROVED,
		 GNUTLS_FIPS140_OP_ERROR,
		 },
		{
		 "[y ^ q mod p == 1]",
		 gnutls_ffdhe_2048_group_prime,
		 gnutls_ffdhe_2048_group_q,
		 gnutls_ffdhe_2048_group_generator,
		 gnutls_ffdhe_2048_group_q,
		 GNUTLS_E_RECEIVED_ILLEGAL_PARAMETER,
		 GNUTLS_FIPS140_OP_APPROVED,
		 GNUTLS_FIPS140_OP_ERROR,
		 },
		{
		 "Legal Input",
		 gnutls_ffdhe_2048_group_prime,
		 gnutls_ffdhe_2048_group_q,
		 gnutls_ffdhe_2048_group_generator,
		 {(void *)"\x02", 1},
		 0,
		 GNUTLS_FIPS140_OP_APPROVED,
		 GNUTLS_FIPS140_OP_APPROVED,
		 },
		{NULL}
	};

	for (int i = 0; test_data[i].name != NULL; i++) {
		gnutls_datum_t priv_key, pub_key;
		gnutls_dh_params_t dh_params;
		gnutls_fips140_context_t fips_context;
		int ret;

		if (gnutls_fips140_mode_enabled()) {
			ret = gnutls_fips140_context_init(&fips_context);
			if (ret < 0) {
				fail("Cannot initialize FIPS context\n");
			}
		}

		fips_push_context(fips_context);
		params(&dh_params, &test_data[i].prime, &test_data[i].q,
		       &test_data[i].generator);
		fips_pop_context(fips_context, GNUTLS_FIPS140_OP_INITIAL);

		success("%s genkey\n", test_data[i].name);

		fips_push_context(fips_context);
		genkey(dh_params, &priv_key, &pub_key);
		fips_pop_context(fips_context, test_data[i].fips_state_genkey);

		success("%s compute_key\n", test_data[i].name);

		fips_push_context(fips_context);
		compute_key(test_data[i].name, dh_params, &priv_key,
			    &pub_key, &test_data[i].peer_key,
			    test_data[i].expected_error, NULL, 0);
		fips_pop_context(fips_context,
				 test_data[i].fips_state_compute_key);

		gnutls_dh_params_deinit(dh_params);
		gnutls_free(priv_key.data);
		gnutls_free(pub_key.data);

		if (gnutls_fips140_mode_enabled()) {
			gnutls_fips140_context_deinit(fips_context);
		}
	}

	success("all ok\n");
}
#else
void doit(void)
{
	return;
}
#endif
