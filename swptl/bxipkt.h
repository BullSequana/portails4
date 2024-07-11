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

#ifndef BXIPKT_H
#define BXIPKT_H

#include <stdint.h>
#include <poll.h>

#include "include/swptl4_transport.h"

struct bxipkt_iface;

int bxipkt_common_init(struct bxipkt_options *opts, struct bxipkt_ctx *ctx);
void bxipkt_common_fini(struct bxipkt_ctx *ctx);

extern int bxipkt_debug;

#endif
