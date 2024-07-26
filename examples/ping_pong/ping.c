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
	int ret;
	ptl_process_t other_id;
	long other_nid = 0;
	long other_pid = 0;
	char msg[PTL_EV_STR_SIZE];
	ptl_process_t id;
	ptl_handle_ni_t nih1;
	ptl_handle_eq_t eqh1;
	ptl_index_t pti1;
	ptl_handle_md_t mdh1;
	ptl_handle_le_t leh1;
	ptl_event_t ev;
	ptl_process_t rank1;
	ptl_process_t mapping[2];
	ptl_le_t le1;
	ptl_md_t md1;
	char init_data[50];
	char *data1 = "Bonjour interface 2 !";

	rank1.rank = 1;

	ret = PtlInit();
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_NO_MATCHING | PTL_NI_LOGICAL, PTL_PID_ANY, NULL,
			NULL, &nih1);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlNIInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlGetPhysId(nih1, &id);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	printf("Nid ping : %d\n", id.phys.nid);
	printf("Pid ping : %d\n\n", id.phys.pid);
	printf("-------------------------------\n\n");
	printf("Nid pong : ");
	scanf("%ld", &other_nid);
	printf("Pid pong : ");
	scanf("%ld", &other_pid);

	other_id.phys.nid = other_nid;
	other_id.phys.pid = other_pid;

	mapping[0] = id;
	mapping[1] = other_id;

	printf("mapping is done \n");

	/*Set mapping from logical to physical id*/

	ret = PtlSetMap(nih1, 2, mapping);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlSetMap failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlEQAlloc(nih1, 2, &eqh1);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQAlloc 1 failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlPTAlloc(nih1, 0, eqh1, PTL_PT_ANY, &pti1);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	le1 = (ptl_le_t){ .start = init_data,
			  .length = strlen(data1) + 1,
			  .ct_handle = PTL_CT_NONE,
			  .uid = PTL_UID_ANY,
			  .options = PTL_LE_OP_PUT };

	ret = PtlLEAppend(nih1, pti1, &le1, PTL_PRIORITY_LIST, NULL, &leh1);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlLEAppend failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlEQWait(eqh1, &ev);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &ev, msg);
		printf("Event interface 1 : %s", msg);
	}

	md1 = (ptl_md_t){ .start = data1,
			  .length = strlen(data1) + 1,
			  .options = 0,
			  .eq_handle = eqh1,
			  .ct_handle = PTL_CT_NONE };

	ret = PtlMDBind(nih1, &md1, &mdh1);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlMDBind failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/*send the message to interface 2*/
	ret = PtlPut(mdh1, 0, strlen(data1) + 1, PTL_ACK_REQ, rank1, 0, 0, 0, NULL, 0);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPut failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/*wait to receive the send events message*/
	ret = PtlEQWait(eqh1, &ev);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQPoll 1 failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
	} else {
		PtlEvToStr(0, &ev, msg);
		printf("Event interface 1 : %s", msg);
	}

	/*wait to receive the ack from our recipent*/
	ret = PtlEQWait(eqh1, &ev);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &ev, msg);
		printf("Event interface 1 : %s", msg);
	}

	PtlEQPoll(&eqh1, 0, 1, &ev, 0);

	/*interface 1 wait to receive the response message*/

	ret = PtlEQWait(eqh1, &ev);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQPoll failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &ev, msg);
		printf("Event interface 1 : %s", msg);
	}
	if (ev.type == PTL_EVENT_PUT) {
		printf("Message received from interface 2: \n");
		printf("   header :  %ld \n", ev.hdr_data);
		printf("   message : %s \n", (char *)ev.start);
	}
}