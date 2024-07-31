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
#include <stdbool.h>

#include "portals4.h"
#include "portals4_ext.h"

/*
 * Example how to use logical interface to communicate within two interfaces
 */

int ping(atomic_int* ni_initialized, atomic_int* set_map_done, ptl_process_t *mapping, int max_tours)
{
	int ret;
	char msg[PTL_EV_STR_SIZE];
	ptl_process_t id;
	ptl_handle_ni_t nih1;
	ptl_handle_eq_t eqh1;
	ptl_index_t pti1;
	ptl_handle_md_t mdh1;
	ptl_handle_le_t leh1;
	ptl_event_t ev;
	ptl_process_t rank1;
	ptl_le_t le1;
	ptl_md_t md1;
	char init_data[50];
	char *data1 = "Hello pong !";

	rank1.rank = 1;

	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_NO_MATCHING | PTL_NI_LOGICAL, PTL_PID_ANY,
				NULL, NULL, &nih1);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlNIInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlGetPhysId(nih1, &id);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		mapping[0] = id;
		*ni_initialized += 1;
	}

	while (atomic_load(ni_initialized) < 2) {
	}

	/* Set mapping from logical to physical id */

	ret = PtlSetMap(nih1, 2, mapping);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlSetMap failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		*set_map_done += 1;
	}

	ret = PtlEQAlloc(nih1, 2, &eqh1);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQAlloc 1 failed : %s \n",
			PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlPTAlloc(nih1, 0, eqh1, PTL_PT_ANY, &pti1);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	while (atomic_load(set_map_done) < 2) {
	}

	le1 = (ptl_le_t){ .start = init_data,
				.length = strlen(data1) + 1,
				.ct_handle = PTL_CT_NONE,
				.uid = PTL_UID_ANY,
				.options = PTL_LE_OP_PUT };

	while((max_tours > 0) || (max_tours == -1)) {
		printf("MAXTOURSMAXTOURSMAXTOURSMAXTOURS : %d\n", max_tours);
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
			printf("Event interface 1 : %s \n", msg);
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

		/* send the message to interface 2 */
		ret = PtlPut(mdh1, 0, strlen(data1) + 1, PTL_ACK_REQ, rank1, 0, 0, 0, NULL, 0);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlPut failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}

		/* wait to receive the send events message */
		ret = PtlEQWait(eqh1, &ev);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQPoll 1 failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		} else {
			PtlEvToStr(0, &ev, msg);
			printf("Event interface 1 : %s \n", msg);
		}

		/* wait to receive the ack from our recipent */
		ret = PtlEQWait(eqh1, &ev);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		} else {
			PtlEvToStr(0, &ev, msg);
			printf("Event interface 1 : %s \n", msg);
		}

		PtlEQPoll(&eqh1, 0, 1, &ev, 0);

		/* interface 1 wait to receive the response message */

		ret = PtlEQWait(eqh1, &ev);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQPoll failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		} else {
			PtlEvToStr(0, &ev, msg);
			printf("Event interface 1 : %s \n", msg);
		}
		if (ev.type == PTL_EVENT_PUT) {
			printf("ping received message from pong: \n");
			printf("     header :  %ld \n", ev.hdr_data);
			printf("     message : %s \n", (char *)ev.start);
		}
        sleep(1.5);
		max_tours --;
	}
	return 0;
}

int pong(atomic_int* ni_initialized, atomic_int* set_map_done, ptl_process_t* mapping, int max_tours)
{
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
	ptl_le_t le2;
	ptl_md_t md2;
	char init_data[50];
	char *data2 = "Hello ping !";

	rank0.rank = 0;

	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_NO_MATCHING | PTL_NI_LOGICAL, PTL_PID_ANY,
				NULL, NULL, &nih2);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlNIInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlGetPhysId(nih2, &id);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetPhysId failed : %s \n",
			PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		mapping[1] = id;
		*ni_initialized += 1;
	}

	while (atomic_load(ni_initialized) < 2) {
	}

	/* Set mapping from logical to physical id */

	ret = PtlSetMap(nih2, 2, mapping);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlSetMap failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		*set_map_done += 1;
	}

	while (atomic_load(set_map_done) < 2) {
	}

	/* Interface 2 prepare to receive a message */

	ret = PtlEQAlloc(nih2, 2, &eqh2);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQAlloc 2 failed : %s \n",
			PtlToStr(ret, PTL_STR_ERROR));
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

	while((max_tours > 0) || (max_tours == -1)) {
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
			printf("Event interface 2 : %s \n", msg);
		}

		/* Interface 2 receive the message and reply */

		ret = PtlEQWait(eqh2, &ev);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQWait 2 failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		} else {
			PtlEvToStr(0, &ev, msg);
			printf("Event interface 2 : %s \n", msg);
		}
		if (ev.type == PTL_EVENT_PUT) {
			printf("            pong received message from ping : \n");
			printf("                 header :  %ld \n", ev.hdr_data);
			printf("                 message : %s \n", (char *)ev.start);
		}
        sleep(1.5);

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
			printf("Event interface 2 : %s \n", msg);
		}

		ret = PtlEQWait(eqh2, &ev);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		} else {
			PtlEvToStr(0, &ev, msg);
			printf("Event interface 2 : %s \n", msg);
		}

		PtlEQPoll(&eqh2, 0, 1, &ev, 0);
		max_tours --;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int ret;
	ptl_process_t *mapping;
	char* p;
	int max_tours;
	
	if (argc < 2) {
		max_tours = -1;
	}
	else {
		max_tours = strtol(argv[1], &p, 10);
		if (*p != '\0') {
			fprintf(stderr, "An invalid character was found before the end of the string\n");
			return 1;
		}
	}
	
	mapping = (ptl_process_t *)mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_SHARED, 0, 0);
	atomic_int *ni_initialized =
		&((atomic_int *)(mapping))[2 * (sizeof(ptl_process_t) / sizeof(int)) + 1];
	*ni_initialized = ATOMIC_VAR_INIT(0);

	atomic_int *set_map_done =
		&((atomic_int *)(mapping))[2 * (sizeof(ptl_process_t) / sizeof(int)) +
					   sizeof(ni_initialized) + 1];
	*set_map_done = ATOMIC_VAR_INIT(0);

	/*
	 * Fork in two process for our two interfaces.
	 * Note that this must be called before PtlInit, as required by the Portals4 specification
	 */
	int res = fork();

	ret = PtlInit();
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/* interface 1 */
	if (res != 0) {
		ret = ping(ni_initialized, set_map_done, mapping, max_tours);
		if (ret != 0) {
			fprintf(stderr, "ping failed\n");
			return 1;
		}
	}

	/* interface 2 */
	else {
		ret = pong(ni_initialized, set_map_done, mapping, max_tours);
		if (ret != 0) {
			fprintf(stderr, "pong failed\n");
			return 1;
		}
	}
}