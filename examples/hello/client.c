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
#include "portals4_ext.h"

#include "hello.h"

/*
 * this file represent a client sendind a message to a server
 *
 * Usage: ./client <nid> <message>
 *
 */

int main(int argc, char **argv)
{
	int res;
	int ret;
	ptl_handle_ni_t nih;
	ptl_handle_eq_t eqh;
	ptl_handle_md_t mdh;
	ptl_event_t ev;
	ptl_md_t md;
	ptl_hdr_data_t header_data = 1234;
	ptl_process_t id_server = { .phys = { .pid = SERVER_PID, .nid = 0 } };
	long nid;
	char msg[PTL_EV_STR_SIZE];
	char *p;

	if (argc < 3) {
		fprintf(stderr, "Usage: client <nid> <message>\n not enough arguments\n");
		return 1;
	}

	nid = strtol(argv[1], &p, 10);
	if (*p != '\0') {
		fprintf(stderr, "An invalid character was found before the end of the string\n");
		return 1;
	}
	id_server.phys.nid = nid;

	printf("\n--- client interface --- \n \n");

	ret = PtlInit();
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_NO_MATCHING | PTL_NI_PHYSICAL, PTL_PID_ANY, NULL,
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

	/* describe the memory used to send the message */
	md = (ptl_md_t){ .start = argv[2],
			 .length = strlen(argv[2]) + 1,
			 .options = 0,
			 .eq_handle = eqh,
			 .ct_handle = PTL_CT_NONE };

	ret = PtlMDBind(nih, &md, &mdh);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlMDBind failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto eq_free;
	}

	/* send the message to the server specified by id_server */
	ret = PtlPut(mdh, 0, strlen(argv[2]) + 1, PTL_ACK_REQ, id_server, 0, 0, 0, NULL,
		     header_data);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPut failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto md_release;
	}

	/* wait to receive the send event message */
	ret = PtlEQWait(eqh, &ev);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto md_release;
	}
	PtlEvToStr(0, &ev, msg);
	printf("Event : %s", msg);
	printf("Message of length %ld sent ! \n", ev.mlength);

	/* wait to receive the ack from our recipient */
	ret = PtlEQWait(eqh, &ev);
	printf("\n");
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto md_release;
	}
	res = 0;
	PtlEvToStr(0, &ev, msg);
	printf("Event : %s", msg);
	printf("Message of length %ld received by the server ! \n\n", ev.mlength);

md_release:
	PtlMDRelease(mdh);
eq_free:
	PtlEQFree(eqh);
ni_fini:
	PtlNIFini(nih);
fini:
	PtlFini();

	return res;
}