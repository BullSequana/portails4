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
#include "portals4_ext.h"

#include "hello.h"

/*
 * this file represents a server receving a message from a client
 *
 * Usage: ./server [number of messages to wait]
 *
 * The server will exit after the specified number of messages have been received.
 * By default, the server wait forever.
 */

int main(int argc, char **argv)
{
	int res;
	int ret;
	ptl_handle_ni_t nih;
	ptl_pt_index_t pti;
	ptl_handle_eq_t eqh;
	ptl_process_t id;
	ptl_event_t ev;
	ptl_handle_le_t leh;
	long num_messages;
	ptl_le_t le;
	char *p;
	char msg[PTL_EV_STR_SIZE];
	char data[50];
	bool wait_for_ever = false;

	if (argc < 2) {
		wait_for_ever = true;
	} else {
		num_messages = strtol(argv[1], &p, 10);
		if (*p != '\0') {
			fprintf(stderr,
				"An invalid character was found before the end of the string");
			return 1;
		}
	}

	printf(" \n--- server interface --- \n \n");

	ret = PtlInit();
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_NO_MATCHING | PTL_NI_PHYSICAL, SERVER_PID, NULL,
			NULL, &nih);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlNIInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto fini;
	}

	ret = PtlEQAlloc(nih, 10, &eqh);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto ni_fini;
	}

	ret = PtlPTAlloc(nih, 0, eqh, 0, &pti);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto eq_free;
	}

	/* display server pid and server nid so the client knows the server id */
	ret = PtlGetId(nih, &id);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto le_unlink;
	}
	printf("Server Pid : %d\n", id.phys.pid);
	printf("Server Nid : %d\n\n", id.phys.nid);

	/* describe the memory used to receive the message */
	le = (ptl_le_t){ .start = data,
			 .length = sizeof(data),
			 .ct_handle = PTL_CT_NONE,
			 .uid = PTL_UID_ANY,
			 .options = PTL_LE_OP_PUT };

	/* expose receive buffer to the network */
	ret = PtlLEAppend(nih, pti, &le, PTL_PRIORITY_LIST, NULL, &leh);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlLEAppend failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto pt_free;
	}

	/* wait to receive the LINK event*/
	ret = PtlEQWait(eqh, &ev);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto le_unlink;
	}
	PtlEvToStr(0, &ev, msg);
	printf("Event : %s", msg);
	printf("Server is ready\n");

	while (num_messages > 0 || wait_for_ever) {
		/* wait until a message arrives */
		printf("\n");
		ret = PtlEQWait(eqh, &ev);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			res = 1;
			goto le_unlink;
		}
		data[sizeof(data) - 1] = '\0';
		PtlEvToStr(0, &ev, msg);
		printf("Event : %s", msg);
		if (ev.type == PTL_EVENT_PUT) {
			if (ev.rlength + 1 >= sizeof(data)) {
				fprintf(stderr,
					"The received message is too long to be entirely readed\n");
			}
			printf("Message received : \n");
			printf("   header :  %ld \n", ev.hdr_data);
			printf("   message : %s \n", (char *)ev.start);
		}
		num_messages--;
	}
	res = 0;

le_unlink:
	PtlLEUnlink(leh);
pt_free:
	PtlPTFree(nih, pti);
eq_free:
	PtlEQFree(eqh);
ni_fini:
	PtlNIFini(nih);
fini:
	PtlFini();

	return res;
}