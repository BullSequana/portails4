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
#include <stdlib.h>
#include <string.h>
#include "portals4.h"
#include "portals4_bxiext.h"

/*
 * Example how to get our different id
 */

int main(void)
{
	int ret;
	ptl_handle_ni_t retNIInitPhys;
	ptl_handle_ni_t retNIInitLog;
	ptl_process_t id;
	ptl_process_t mapping[1];
	ptl_uid_t uid;

	ret = PtlInit();
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_MATCHING | PTL_NI_PHYSICAL, PTL_PID_ANY, NULL,
			NULL, &retNIInitPhys);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlNIInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_MATCHING | PTL_NI_LOGICAL, PTL_PID_ANY, NULL,
			NULL, &retNIInitLog);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlNIInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlGetId(retNIInitPhys, &id);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		printf("\nGetId as the interface is physical :\n");
		printf("Pid : %d \n", id.phys.pid);
		printf("Nid : %d \n\n", id.phys.nid);
	}

	ret = PtlGetPhysId(retNIInitLog, &id);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetPhysId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		mapping[0].phys.pid = id.phys.pid;
		mapping[0].phys.nid = id.phys.nid;
		printf("GetPhysId, the interface is logical and we get our physical id : \n");
		printf("Pid : %d \n", id.phys.pid);
		printf("Nid : %d \n\n", id.phys.nid);
	}

	ret = PtlSetMap(retNIInitLog, 1, mapping);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlGetId(retNIInitLog, &id);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		printf("GetId as the interface is logical :\n");
		printf("Rank : %d \n\n", id.rank);
	}

	ret = PtlGetUid(retNIInitPhys, &uid);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetUid failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		printf("Get the physical interface uid :\n");
		printf("Uid : %d \n\n", uid);
	}
}
