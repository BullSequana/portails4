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
#include "portals4_ext.h"

/*
 * Example how to get our different ids
 */

int main(void)
{
	int res = 0;
	int ret;
	ptl_handle_ni_t nih_phys;
	ptl_handle_ni_t nih_log;
	ptl_process_t id;
	ptl_process_t mapping[1];
	ptl_uid_t uid;

	ret = PtlInit();
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/* physical addressing interface */
	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_MATCHING | PTL_NI_PHYSICAL, PTL_PID_ANY, NULL,
			NULL, &nih_phys);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlNIInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto fini;
	}

	/* logical addressing interface */
	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_MATCHING | PTL_NI_LOGICAL, PTL_PID_ANY, NULL,
			NULL, &nih_log);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlNIInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto ni_fini_phys;
	}

	ret = PtlGetId(nih_phys, &id);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto ni_fini_log;
	}
	printf("\nGetId when the interface is physical :\n");
	printf("Pid : %d \n", id.phys.pid);
	printf("Nid : %d \n\n", id.phys.nid);

	ret = PtlGetPhysId(nih_log, &id);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetPhysId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto ni_fini_log;
	}
	mapping[0].phys.pid = id.phys.pid;
	mapping[0].phys.nid = id.phys.nid;
	printf("GetPhysId, the interface is logical and we get our physical id : \n");
	printf("Pid : %d \n", id.phys.pid);
	printf("Nid : %d \n\n", id.phys.nid);

	/* set mapping for logicial identifier to physical identifiers */
	ret = PtlSetMap(nih_log, 1, mapping);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto ni_fini_log;
	}

	ret = PtlGetId(nih_log, &id);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto ni_fini_log;
	}
	printf("GetId when the interface is logical :\n");
	printf("Rank : %d \n\n", id.rank);

ni_fini_log:
	PtlNIFini(nih_log);
ni_fini_phys:
	PtlNIFini(nih_phys);
fini:
	PtlFini();

	return res;
}