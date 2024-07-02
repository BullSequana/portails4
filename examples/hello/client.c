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

#include "hello.h"

/*
 * this file represent a client sendind a message to a specified server
 */

int main(int argc, char **argv)
{
	int ret;
	ptl_handle_ni_t retInit;
	ptl_handle_eq_t retEQAlloc;
	ptl_handle_md_t resBind;
	ptl_event_t retWait;
	ptl_hdr_data_t header_data = 1234;
	ptl_event_t reseqpoll;
	char msg[PTL_EV_STR_SIZE];
	char *p;

	if (argc < 3) {
		fprintf(stderr, "Usage: client <nid> <message>\n not enough arguments");
		return 1;
	}

	int nid = strtol(argv[1], &p, 10);
	if (*p != '\0') {
		fprintf(stderr, "An invalid character was found before the end of the string");
	}
	ptl_process_t id_server = { .phys = { .pid = SERVER_PID, .nid = nid} };

	/*represent the memory used to send the message*/
	ptl_md_t md = { .start = argv[2],
			.length = strlen(argv[2]) + 1,
			.options = 0,
			.eq_handle = PTL_EQ_NONE,
			.ct_handle = PTL_CT_NONE };

	printf("\n--- client interface --- \n \n");

	ret = PtlInit();
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_NO_MATCHING | PTL_NI_PHYSICAL, PTL_PID_ANY, NULL,
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

	md.eq_handle = retEQAlloc;
	/*allocate a buffer for our message represented by md*/
	ret = PtlMDBind(retInit, &md, &resBind);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlMDBind failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/*send the message to the server specified by id_server*/
	ret = PtlPut(resBind, 0, strlen(argv[2]) + 1, PTL_ACK_REQ, id_server, 0, 0, 0, NULL,
		     header_data);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPut failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/*wait to receive the send event message*/
	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
		printf("Message of length %ld sent ! \n", retWait.mlength);
	}

	/*wait to receive the ack from our recipent*/
	ret = PtlEQWait(retEQAlloc, &retWait);
	printf("\n");
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
		printf("Message received by the server ! \n\n");
	}

	PtlEQPoll(&retEQAlloc, 0, 1, &reseqpoll, 0);
}