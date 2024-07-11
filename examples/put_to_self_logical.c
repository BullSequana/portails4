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
 * This example show the sending of a message to ourselves using a logical interface,
 * we get our Id, create a message and send it through a specified allocated buffer"
 */

int main(void)
{
	int ret;
	ptl_handle_ni_t retNIInit;
	ptl_handle_eq_t retEQAlloc;
	ptl_index_t retPTAlloc;
	ptl_handle_le_t retAppend;
	ptl_process_t id;
	ptl_process_t mapping[1];
	ptl_event_t retWait;
	ptl_handle_md_t retBind;
	ptl_process_t rank0;

	/*represent the memory used to receive the message*/
	ptl_le_t le = { .start = NULL,
			.length = 0,
			.ct_handle = PTL_CT_NONE,
			.uid = PTL_UID_ANY,
			.options = PTL_LE_OP_PUT };

	/*represent the memory used to send the message*/
	ptl_md_t md = { .start = NULL,
			.length = 0,
			.options = 0,
			.eq_handle = PTL_EQ_NONE,
			.ct_handle = PTL_CT_NONE };
	ptl_hdr_data_t header_data = 0;

	rank0.rank = 0;

	ret = PtlInit();
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_NO_MATCHING | PTL_NI_LOGICAL, PTL_PID_ANY, NULL,
			NULL, &retNIInit);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlNIInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlEQAlloc(retNIInit, 10, &retEQAlloc);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlPTAlloc(retNIInit, PTL_PT_FLOWCTRL, retEQAlloc, PTL_PT_ANY, &retPTAlloc);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlGetPhysId(retNIInit, &id);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		mapping[0] = id;
	}

	ret = PtlSetMap(retNIInit, 1, mapping);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlSetMap failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/*prepare our events buffer to receive a message represented by le*/
	ret = PtlLEAppend(retNIInit, retPTAlloc, &le, PTL_PRIORITY_LIST, NULL, &retAppend);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlLEAppend failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		char msg[50];
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	md.eq_handle = retEQAlloc;
	/*allocate a buffer for our message represented by md*/
	ret = PtlMDBind(retNIInit, &md, &retBind);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlMDBind failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/*send the message to ourself*/
	ret = PtlPut(retBind, 0, 0, PTL_ACK_REQ, rank0, retPTAlloc, 0, 0, NULL, header_data);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPut failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/*wait to receive the send events message*/
	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
	} else {
		char msg[50];
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	/*wait to receive the ack from our recipent*/
	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		char msg[100];
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		char msg[150];
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}
}