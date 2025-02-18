/* gostdsa-verify.c

  Copyright (C) 2018 Dmitry Eremin-Solenikov

  This file is part of GNU Nettle.

  GNU Nettle is free software: you can redistribute it and/or
  modify it under the terms of either:

   * the GNU Lesser General Public License as published by the Free
    Software Foundation; either version 3 of the License, or (at your
    option) any later version.

  or

   * the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your
    option) any later version.

  or both in parallel, as here.

  GNU Nettle is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received copies of the GNU General Public License and
  the GNU Lesser General Public License along with this program. If
  not, see https://www.gnu.org/licenses/.
*/

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <gnutls_int.h>

#include <stdlib.h>

#include <nettle/ecc-curve.h>
#include "gostdsa2.h"

#define GOST_GC256B_Q "ffffffffffffffffffffffffffffffff" \
	"6c611070995ad10045841b09b761b893"
#define GOST_GC512A_Q "ffffffffffffffffffffffffffffffff" \
	"ffffffffffffffffffffffffffffffff" \
	"27e69532f48d89116ff22b8d4e056060" \
	"9b4b38abfad2b85dcacdb1411f10b275"

/* Key comes in form .... M_2 M_1 K_0,
  unmask is K_i = K_i-1 * M_i mod Q */
int gostdsa_unmask_key(const struct ecc_curve *ecc, mpz_t key)
{
	unsigned bits = ecc_bit_size(ecc);
	unsigned keybits = mpz_sizeinbase(key, 2);
	mpz_t unmasked, temp, temp2, q;

	if (keybits <= bits)
		return 0;

	mpz_init(unmasked);
	mpz_init(temp);
	mpz_init(temp2);

	if (ecc == nettle_get_gost_gc256b())
		mpz_init_set_str(q, GOST_GC256B_Q, 16);
	else if (ecc == nettle_get_gost_gc512a())
		mpz_init_set_str(q, GOST_GC512A_Q, 16);
	else
		abort();

	mpz_tdiv_r_2exp(unmasked, key, bits);
	mpz_tdiv_q_2exp(key, key, bits);
	keybits -= bits;
	while (keybits > bits) {
		mpz_tdiv_r_2exp(temp2, key, bits);
		mpz_tdiv_q_2exp(key, key, bits);
		keybits -= bits;
		mpz_mul(temp, unmasked, temp2);
		mpz_mod(unmasked, temp, q);
	}
	mpz_mul(temp, unmasked, key);
	mpz_mod(key, temp, q);

	mpz_clear(q);
	mpz_clear(temp2);
	mpz_clear(temp);
	mpz_clear(unmasked);

	return 0;
}
