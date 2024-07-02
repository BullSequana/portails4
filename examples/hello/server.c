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
#include <stdlib.h>
#include <string.h>

#include "portals4.h"
#include "portals4_bxiext.h"

#include "hello.h"

/*
 * this file represent a server receving a message from a client
 */

int main(int argc, char **argv)
{
	int ret;
	ptl_handle_ni_t retInit;
	ptl_pt_index_t retPTAlloc;
	ptl_handle_eq_t retEQAlloc;
	ptl_process_t id;
	ptl_event_t retWait;
	ptl_handle_le_t retAppend;
	int num_messages;
	char* p;
	char msg[PTL_EV_STR_SIZE];
	char data[50];
	/*represent the memory used to receive the message*/
	ptl_le_t le = { .start = data,
			.length = sizeof(data),
			.ct_handle = PTL_CT_NONE,
			.uid = PTL_UID_ANY,
			.options = PTL_LE_OP_PUT };

	if (argc < 2) {
		fprintf(stderr, "Usage: client <number of possible received messages>\n not enough arguments");
		return 1;
	}

	num_messages = strtol(argv[1], &p, 10);
	if (*p != '\0') {
		fprintf(stderr, "An invalid character was found before the end of the string");
	}

	printf(" \n--- server interface --- \n \n");

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

	/*prepare an events buffer to receive a message represented by le*/
	ret = PtlLEAppend(retInit, retPTAlloc, &le, PTL_PRIORITY_LIST, NULL, &retAppend);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlLEAppend failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/*get server pid and server nid so the client know the server id*/
	ret = PtlGetId(retInit, &id);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}
	printf("Serveur Pid : %d \n", id.phys.pid);
	printf("Serveur Nid : %d", id.phys.nid);

	while (num_messages >= 0) {
		/*wait until an event arrive*/
		printf("\n");
		ret = PtlEQWait(retEQAlloc, &retWait);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		} else {
			PtlEvToStr(0, &retWait, msg);
			printf("Event : %s", msg);
		}
		if (retWait.type == PTL_EVENT_PUT) {
			if (retWait.rlength +1 >= sizeof(data)) {
				fprintf(stderr, "The received message is too long to be entirely readed");
			}
			else {
				((char *)retWait.start)[retWait.rlength + 1] = 0;
			}
			printf("Message received : \n");
			printf("   header :  %ld \n", retWait.hdr_data);
			printf("   message : %s \n", (char *)retWait.start);
		}
		num_messages--;
	}
}