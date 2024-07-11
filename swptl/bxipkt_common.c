/*
 * Copyright (C) Bull S.A.S - 2024
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program; if
 * not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * BXI Low Level Team
 *
 */

#include <stdio.h>

#include "portals4.h"
#include "bxipkt.h"
#include "ptl_log.h"

#ifdef DEBUG
/*
 * log to stderr, with the give debug level
 */
#define LOGN(n, ...)                                                                               \
	do {                                                                                       \
		if (bxipkt_debug >= (n))                                                           \
			ptl_log(__VA_ARGS__);                                                      \
	} while (0)

#define LOG(...) LOGN(1, __VA_ARGS__)
#else
#define LOGN(n, ...)                                                                               \
	do {                                                                                       \
	} while (0)
#define LOG(...)                                                                                   \
	do {                                                                                       \
	} while (0)
#endif

#include "ptl_getenv.h"

int bxipkt_debug;

void bxipkt_options_set_default(struct bxipkt_options *opts)
{
	opts->debug = 0;
	opts->stats = 0;
}

/* Library initialization. */
int bxipkt_common_init(struct bxipkt_options *opts, struct bxipkt_ctx *ctx)
{
	ctx->opts = *opts;

	if (opts->debug > bxipkt_debug)
		bxipkt_debug = opts->debug;

	return PTL_OK;
}

void bxipkt_common_fini(struct bxipkt_ctx *ctx)
{
}
