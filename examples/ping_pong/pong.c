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
#include <sys/mman.h>
#include <unistd.h>
#include <stdatomic.h>

#include "portals4.h"
#include "portals4_ext.h"

/*
 * Example how to use logical interface to communicate within two interfaces
 */

int main(void)
{
	ptl_process_t other_id;
	long other_nid = 0;
	long other_pid = 0;
	int ret;
	char msg[PTL_EV_STR_SIZE];
	ptl_process_t id;
	ptl_handle_ni_t nih2;
	ptl_handle_eq_t eqh2;
	ptl_index_t pti2;
	ptl_handle_md_t mdh2;
	ptl_handle_le_t leh2;
	ptl_event_t ev;
	ptl_process_t rank0;
	ptl_process_t mapping[2];
	ptl_le_t le2;
	ptl_md_t md2;
	char init_data[50];
	char *data2 = "Bonjour interface 1 !";

	rank0.rank = 0;

	ret = PtlInit();
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_NO_MATCHING | PTL_NI_LOGICAL, PTL_PID_ANY, NULL,
			NULL, &nih2);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlNIInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlGetPhysId(nih2, &id);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetPhysId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	printf("Nid pong : %d\n", id.phys.nid);
	printf("Pid pong : %d\n\n", id.phys.pid);
	printf("-------------------------------\n\n");
	printf("Nid ping : ");
	scanf("%ld", &other_nid);
	printf("Pid ping : ");
	scanf("%ld", &other_pid);

	other_id.phys.nid = other_nid;
	other_id.phys.pid = other_pid;

	mapping[1] = id;
	mapping[0] = other_id;

	printf("mapping is done \n");

	/*Set mapping from logical to physical id*/

	ret = PtlSetMap(nih2, 2, mapping);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlSetMap failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/*Interface 2 prepare to receive a message*/

	ret = PtlEQAlloc(nih2, 2, &eqh2);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQAlloc 2 failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlPTAlloc(nih2, 0, eqh2, PTL_PT_ANY, &pti2);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	le2 = (ptl_le_t){ .start = init_data,
			  .length = strlen(data2) + 1,
			  .ct_handle = PTL_CT_NONE,
			  .uid = PTL_UID_ANY,
			  .options = PTL_LE_OP_PUT };

	ret = PtlLEAppend(nih2, pti2, &le2, PTL_PRIORITY_LIST, NULL, &leh2);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlLEAppend failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlEQWait(eqh2, &ev);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &ev, msg);
		printf("Event interface 2 : %s", msg);
	}

	/*Interface 2 receive the message and reply*/

	ret = PtlEQWait(eqh2, &ev);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait 2 failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &ev, msg);
		printf("Event interface 2 : %s", msg);
	}
	if (ev.type == PTL_EVENT_PUT) {
		printf("Message received from interface 1 : \n");
		printf("   header :  %ld \n", ev.hdr_data);
		printf("   message : %s \n", (char *)ev.start);
	}

	md2 = (ptl_md_t){ .start = data2,
			  .length = strlen(data2) + 1,
			  .options = 0,
			  .eq_handle = eqh2,
			  .ct_handle = PTL_CT_NONE };

	ret = PtlMDBind(nih2, &md2, &mdh2);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlMDBind failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlPut(mdh2, 0, strlen(data2) + 1, PTL_ACK_REQ, rank0, 0, 0, 0, NULL, 0);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPut failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlEQWait(eqh2, &ev);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &ev, msg);
		printf("Event interface 2 : %s", msg);
	}

	ret = PtlEQWait(eqh2, &ev);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &ev, msg);
		printf("Event interface 2 : %s", msg);
	}

	PtlEQPoll(&eqh2, 0, 1, &ev, 0);
}