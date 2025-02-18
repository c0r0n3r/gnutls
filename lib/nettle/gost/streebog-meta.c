/* streebog-meta.c

   Copyright (C) 2012 Nikos Mavrogiannopoulos, Niels Möller

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
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received copies of the GNU General Public License and
   the GNU Lesser General Public License along with this program.  If
   not, see https://www.gnu.org/licenses/.
*/

#if HAVE_CONFIG_H
# include "config.h"
#endif

#ifndef HAVE_NETTLE_STREEBOG512_UPDATE
# include <gnutls_int.h>

# include <nettle/nettle-meta.h>

# include "streebog.h"

const struct nettle_hash nettle_streebog512
    = _NETTLE_HASH(streebog512, STREEBOG512);

const struct nettle_hash nettle_streebog256
    = _NETTLE_HASH(streebog256, STREEBOG256);
#endif
