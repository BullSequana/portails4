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

#pragma once
#include "portals4.h"

enum ptl_str_type {
	PTL_STR_ERROR, /* Return codes */
	PTL_STR_EVENT, /* Events */
	PTL_STR_FAIL_TYPE, /* Failure type */
};

typedef enum ptl_str_type ptl_str_type_t;

#define PTL_ME_MANAGE_LOCAL_STOP_IF_UH 0
#define PTL_ME_OV_RDV_PUT_ONLY 0
#define PTL_ME_OV_RDV_PUT_DISABLE 0

#define PTL_ME_UH_LOCAL_OFFSET_INC_MANIPULATED 0x200000

const char *PtlToStr(int rc, ptl_str_type_t type);

#define PTL_EV_STR_SIZE 256
int PtlEvToStr(unsigned int ni_options, ptl_event_t *e, char *msg);