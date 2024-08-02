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

#include "get_me.h"

/*
 * this file represent a server preparing messages which can be get from a client
 *
 * Usage: ./server [max messages possible to get]
 *
 * The server will exit after the specified number of messages have been got.
 * By default, the server wait forever.
 */

/* used to perform a MEAppend & wait for the corresponding LINK event */
int add_me(ptl_handle_ni_t nih, ptl_index_t pti, ptl_me_t me, ptl_handle_me_t *meh,
	   ptl_handle_eq_t eqh)
{
	char msg[PTL_EV_STR_SIZE];
	ptl_event_t ev;

	int ret = PtlMEAppend(nih, pti, &me, PTL_PRIORITY_LIST, NULL, meh);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlMEAppend failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}
	ret = PtlEQWait(eqh, &ev);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		PtlMEUnlink(*meh);
		return 1;
	}
	PtlEvToStr(0, &ev, msg);
	printf("Event : %s \n", msg);
	return 0;
}

int main(int argc, char **argv)
{
	int res;
	int ret;
	ptl_handle_ni_t nih;
	ptl_index_t pti;
	ptl_handle_eq_t eqh;
	ptl_process_t id;
	ptl_event_t ev;
	ptl_handle_me_t meh;
	int n = 1;
	char msg[PTL_EV_STR_SIZE];
	ptl_process_t id_server = { .phys = { .pid = SERVER_PID, .nid = 1 } };
	char data[MESSAGELENGTH * 10];
	ptl_me_t me;
	bool wait_for_ever = false;
	long num_messages = 0;
	char *p;

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

	for (int i = 0; i < 10; i++)
		sprintf(&data[i * MESSAGELENGTH], "message%d !", i);

	printf(" \n--- Server interface --- \n \n");

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

	/* get server pid and server nid so the client know the server id */
	ret = PtlGetId(nih, &id);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto pt_free;
	}
	printf("Server Pid : %d \n", id.phys.pid);
	printf("Server Nid : %d \n\n", id.phys.nid);

	/* describe the memory used to send messages */
	me = (ptl_me_t){ .start = data,
			 .length = sizeof(data),
			 .ct_handle = PTL_CT_NONE,
			 .uid = PTL_UID_ANY,
			 .options = PTL_LE_OP_GET | PTL_ME_MANAGE_LOCAL,
			 .match_id = id_server,
			 .match_bits = 0,
			 .ignore_bits = 0,
			 .min_free = MESSAGELENGTH - 1 };

	res = add_me(nih, pti, me, &meh, eqh);
	if (res == 1) {
		goto pt_free;
	}

	while (num_messages > 0 || wait_for_ever) {
		ret = PtlEQWait(eqh, &ev);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			res = 1;
			goto me_unlink;
		}

		PtlEvToStr(0, &ev, msg);
		printf("Event : %s \n", msg);

		if (ev.type == PTL_EVENT_AUTO_UNLINK) {
			n = (n + 1) % 10;
			res = add_me(nih, pti, me, &meh, eqh);
			if (res == 1) {
				goto pt_free;
			}
		}

		if (ev.type == PTL_EVENT_GET) {
			num_messages--;
			printf("message %d has been fetched from a client \n\n", n);
		}
	}

me_unlink:
	PtlMEUnlink(meh);
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
