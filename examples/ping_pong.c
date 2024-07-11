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
#include "portals4_bxiext.h"

/*
 * Example how to use logical interface to communicate within two interfaces
 */

int main(void)
{
	int ret;
	char msg[150];
	ptl_process_t id;
	ptl_handle_ni_t retNIInit1;
	ptl_handle_ni_t retNIInit2;
	ptl_handle_eq_t retEQAlloc1;
	ptl_index_t retPTAlloc1;
	ptl_handle_md_t retBind1;
	ptl_handle_le_t retAppend1;
	ptl_handle_eq_t retEQAlloc2;
	ptl_index_t retPTAlloc2;
	ptl_handle_md_t retBind2;
	ptl_handle_le_t retAppend2;
	ptl_event_t retWait;
	ptl_process_t rank0;
	ptl_process_t rank1;
	ptl_process_t *mapping;
	ptl_event_t reseqpoll;
	char init_data[50];
	char *data1 = "Bonjour interface 2 !";
	char *data2 = "Bonjour interface 1 !";

	ptl_le_t le1 = { .start = init_data,
			 .length = strlen(data1) + 1,
			 .ct_handle = PTL_CT_NONE,
			 .uid = PTL_UID_ANY,
			 .options = PTL_LE_OP_PUT };

	ptl_md_t md1 = { .start = data1,
			 .length = strlen(data1) + 1,
			 .options = 0,
			 .eq_handle = PTL_EQ_NONE,
			 .ct_handle = PTL_CT_NONE };
	ptl_hdr_data_t header_data = 0;

	ptl_le_t le2 = { .start = init_data,
			 .length = strlen(data2) + 1,
			 .ct_handle = PTL_CT_NONE,
			 .uid = PTL_UID_ANY,
			 .options = PTL_LE_OP_PUT };

	ptl_md_t md2 = { .start = data2,
			 .length = strlen(data2) + 1,
			 .options = 0,
			 .eq_handle = PTL_EQ_NONE,
			 .ct_handle = PTL_CT_NONE };

	mapping = (ptl_process_t *)mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_SHARED, 0, 0);
	atomic_int *is_ready =
		&((atomic_int *)(mapping))[2 * (sizeof(ptl_process_t) / sizeof(int)) + 1];
	*is_ready = ATOMIC_VAR_INIT(0);

	atomic_int *is_ready_bis =
		&((atomic_int *)(mapping))[2 * (sizeof(ptl_process_t) / sizeof(int)) +
					   sizeof(is_ready) + 1];
	*is_ready_bis = ATOMIC_VAR_INIT(0);

	/*fork in two process for our two interfaces*/
	int res = fork();

	rank0.rank = 0;
	rank1.rank = 1;

	ret = PtlInit();
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	if (res != 0) {
		ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_NO_MATCHING | PTL_NI_LOGICAL, PTL_PID_ANY,
				NULL, NULL, &retNIInit1);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlNIInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}

		ret = PtlGetPhysId(retNIInit1, &id);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlGetId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		} else {
			mapping[0] = id;
			int val = atomic_load(is_ready) + 1;
			atomic_store(is_ready, val);
		}

		while (atomic_load(is_ready) < 2) {
		}

		/*Set mapping from logical to physical id*/

		ret = PtlSetMap(retNIInit1, 2, mapping);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlSetMap failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		} else {
			int val = atomic_load(is_ready_bis) + 1;
			atomic_store(is_ready_bis, val);
		}

		ret = PtlEQAlloc(retNIInit1, 2, &retEQAlloc1);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQAlloc 1 failed : %s \n",
				PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}

		ret = PtlPTAlloc(retNIInit1, 0, retEQAlloc1, PTL_PT_ANY, &retPTAlloc1);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlPTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}

		while (atomic_load(is_ready_bis) < 2) {
		}

		ret = PtlLEAppend(retNIInit1, retPTAlloc1, &le1, PTL_PRIORITY_LIST, NULL,
				  &retAppend1);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlLEAppend failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}

		ret = PtlEQWait(retEQAlloc1, &retWait);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		} else {
			PtlEvToStr(0, &retWait, msg);
			printf("Event interface 1 : %s", msg);
		}

		md1.eq_handle = retEQAlloc1;
		/*allocate a buffer for our message represented by md*/
		ret = PtlMDBind(retNIInit1, &md1, &retBind1);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlMDBind failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}

		/*send the message to interface 2*/
		ret = PtlPut(retBind1, 0, strlen(data1) + 1, PTL_ACK_REQ, rank1, 0, 0, 0, NULL,
			     header_data);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlPut failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}

		/*wait to receive the send events message*/
		ret = PtlEQWait(retEQAlloc1, &retWait);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQPoll 1 failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		} else {
			PtlEvToStr(0, &retWait, msg);
			printf("Event interface 1 : %s", msg);
		}

		/*wait to receive the ack from our recipent*/
		ret = PtlEQWait(retEQAlloc1, &retWait);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		} else {
			PtlEvToStr(0, &retWait, msg);
			printf("Event interface 1 : %s", msg);
		}

		PtlEQPoll(&retEQAlloc1, 0, 1, &reseqpoll, 0);

		/*interface 1 wait to receive the response message*/

		ret = PtlEQWait(retEQAlloc1, &retWait);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQPoll failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		} else {
			PtlEvToStr(0, &retWait, msg);
			printf("Event interface 1 : %s", msg);
		}
		if (retWait.type == PTL_EVENT_PUT) {
			printf("Message received from interface 2: \n");
			printf("   header :  %ld \n", retWait.hdr_data);
			printf("   message : %s \n", (char *)retWait.start);
		}
	}

	else {
		/*Init second interface*/

		ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_NO_MATCHING | PTL_NI_LOGICAL, PTL_PID_ANY,
				NULL, NULL, &retNIInit2);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlNIInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}

		ret = PtlGetPhysId(retNIInit2, &id);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlGetPhysId failed : %s \n",
				PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		} else {
			mapping[1] = id;
			int val = atomic_load(is_ready) + 1;
			atomic_store(is_ready, val);
		}

		while (atomic_load(is_ready) < 2) {
		}

		/*Set mapping from logical to physical id*/

		ret = PtlSetMap(retNIInit2, 2, mapping);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlSetMap failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		} else {
			int val = atomic_load(is_ready_bis) + 1;
			atomic_store(is_ready_bis, val);
		}

		while (atomic_load(is_ready_bis) < 2) {
		}

		/*Interface 2 prepare to receive a message*/

		ret = PtlEQAlloc(retNIInit2, 2, &retEQAlloc2);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQAlloc 2 failed : %s \n",
				PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}

		ret = PtlPTAlloc(retNIInit2, 0, retEQAlloc2, PTL_PT_ANY, &retPTAlloc2);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlPTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}

		ret = PtlLEAppend(retNIInit2, retPTAlloc2, &le2, PTL_PRIORITY_LIST, NULL,
				  &retAppend2);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlLEAppend failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}

		ret = PtlEQWait(retEQAlloc2, &retWait);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		} else {
			PtlEvToStr(0, &retWait, msg);
			printf("Event interface 2 : %s", msg);
		}

		/*Interface 2 receive the message and reply*/

		ret = PtlEQWait(retEQAlloc2, &retWait);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQWait 2 failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		} else {
			PtlEvToStr(0, &retWait, msg);
			printf("Event interface 2 : %s", msg);
		}
		if (retWait.type == PTL_EVENT_PUT) {
			printf("Message received from interface 1 : \n");
			printf("   header :  %ld \n", retWait.hdr_data);
			printf("   message : %s \n", (char *)retWait.start);
		}

		md2.eq_handle = retEQAlloc2;
		/*allocate a buffer for our message represented by md*/
		ret = PtlMDBind(retNIInit2, &md2, &retBind2);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlMDBind failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}

		ret = PtlPut(retBind2, 0, strlen(data2) + 1, PTL_ACK_REQ, rank0, 0, 0, 0, NULL,
			     header_data);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlPut failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}

		ret = PtlEQWait(retEQAlloc2, &retWait);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		} else {
			PtlEvToStr(0, &retWait, msg);
			printf("Event interface 2 : %s", msg);
		}

		ret = PtlEQWait(retEQAlloc2, &retWait);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		} else {
			PtlEvToStr(0, &retWait, msg);
			printf("Event interface 2 : %s", msg);
		}

		PtlEQPoll(&retEQAlloc1, 0, 1, &reseqpoll, 0);
	}
}