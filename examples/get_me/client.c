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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "portals4.h"
#include "portals4_bxiext.h"

#include "get_me.h"

/*
 * this file represent a client getting messages from a specified server
 */

int main(void)
{
	int ret;
	ptl_handle_ni_t retInit;
	ptl_handle_eq_t retEQAlloc;
	ptl_handle_md_t resBind;
	ptl_event_t retWait;
	ptl_event_t reseqpoll;
	char msg[150];
	ptl_process_t idServer = { .phys = { .pid = SERVER_PID, .nid = 1 } };
	char data[11]; /*memory region for the reply from the server*/
	ptl_md_t md = { .start = data,
			.length = sizeof(data) + 1,
			.options = 0,
			.eq_handle = PTL_EQ_NONE,
			.ct_handle = PTL_CT_NONE };

	printf("\n--- Client interface --- \n \n");

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
	/*allocate a buffer for the received message*/
	ret = PtlMDBind(retInit, &md, &resBind);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlMDBind failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/*try to get the first message from the specified server*/
	ret = PtlGet(resBind, 0, 11, idServer, 0, 0, 0, NULL);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGet failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/*wait to receive a message*/
	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
		printf("Message received : \n");
		printf("   message : %s \n\n", (char *)md.start);
	}

	/*try to get the second message from the specified server*/
	ret = PtlGet(resBind, 0, 11, idServer, 0, 0, 0, NULL);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGet failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/*wait to receive a message*/
	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
		printf("Message received : \n");
		printf("   message : %s \n\n", (char *)md.start);
	}

	PtlEQPoll(&retEQAlloc, 0, 1, &reseqpoll, 0);
}