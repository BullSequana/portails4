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
#include <stdbool.h>
#include <string.h>

#include "portals4.h"
#include "portals4_bxiext.h"

#include "get_me.h"

/*
 * this file represent a server preparing messages which can be get from a client
 */

int main(void)
{
	int ret;
	ptl_handle_ni_t retInit;
	ptl_index_t retPTAlloc;
	ptl_handle_eq_t retEQAlloc;
	ptl_process_t id;
	ptl_event_t retWait;
	ptl_handle_le_t retAppend;
	char msg[150];
	ptl_process_t idServer = { .phys = { .pid = SERVER_PID, .nid = 1 } };
	char data[22];
	memcpy(&data[0], "message1 !", 11);
	memcpy(&data[11], "message2 !", 11);
	/*represent the memory used to send messages*/
	ptl_me_t me = { .start = data,
			.length = sizeof(data) + 1,
			.ct_handle = PTL_CT_NONE,
			.uid = PTL_UID_ANY,
			.options = PTL_LE_OP_GET | PTL_ME_MANAGE_LOCAL,
			.match_id = idServer,
			.match_bits = 0,
			.ignore_bits = 0,
			.min_free = 0 };

	printf(" \n--- Serveur interface --- \n \n");

	ret = PtlInit();
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_NO_MATCHING | PTL_NI_PHYSICAL, SERVER_PID, NULL,
			NULL, &retInit);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlNIInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlEQAlloc(retInit, 10, &retEQAlloc);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlPTAlloc(retInit, 0, retEQAlloc, 0, &retPTAlloc);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/*get server pid and server nid so the client know the server id*/
	ret = PtlGetId(retInit, &id);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}
	printf("Serveur Pid : %d \n", id.phys.pid);
	printf("Serveur Nid : %d \n\n", id.phys.nid);

	/*prepare an events buffer to send first message represented by me*/
	ret = PtlMEAppend(retInit, retPTAlloc, &me, PTL_PRIORITY_LIST, NULL, &retAppend);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlLEAppend failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s \n", msg);
	}

	/*wait the event get from the client*/
	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
		printf("First message has been get from a client \n\n");
	}

	/*
	 * prepare an events buffer to send the second message. The offset used to access me
	 * memory has been incremented by the length of the first message
	 */
	ret = PtlMEAppend(retInit, retPTAlloc, &me, PTL_PRIORITY_LIST, NULL, &retAppend);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlLEAppend failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s \n", msg);
	}

	/*wait the event get from the client*/
	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
		printf("Second message has been get from a client \n\n");
	}
}