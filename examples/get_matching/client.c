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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "portals4.h"
#include "portals4_ext.h"

#include "get_me.h"

/*
 * this file represent a client getting messages from a specified server
 *
 * Usage: ./client <server nid> [number of messages we want to get]
 *
 * By default we get one message
 */

int main(int argc, char **argv)
{
	int res;
	int ret;
	ptl_handle_ni_t nih;
	ptl_handle_eq_t eqh;
	ptl_handle_md_t mdh;
	ptl_event_t ev;
	long nid;
	char *p;
	ptl_md_t md;
	int get_messages = 0;
	long wanted_get;
	char msg[PTL_EV_STR_SIZE];
	ptl_process_t id_server = { .phys = { .pid = SERVER_PID, .nid = 0 } };
	char data[MESSAGELENGTH];

	if (argc < 2) {
		fprintf(stderr,
			"Usage: client <nid> [number of messages we want to get]\n not enough arguments\n");
		return 1;
	}

	if (argc > 2) {
		wanted_get = strtol(argv[2], &p, 10);
		if (*p != '\0') {
			fprintf(stderr,
				"An invalid character was found before the end of the string\n");
			return 1;
		}
	} else {
		wanted_get = 1;
	}

	nid = strtol(argv[1], &p, 10);
	if (*p != '\0') {
		fprintf(stderr, "An invalid character was found before the end of the string\n");
		return 1;
	}
	id_server.phys.nid = nid;

	printf("\n--- Client interface --- \n \n");

	ret = PtlInit();
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_NO_MATCHING | PTL_NI_PHYSICAL, PTL_PID_ANY, NULL,
			NULL, &nih);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlNIInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 0;
		goto fini;
	}

	ret = PtlEQAlloc(nih, 10, &eqh);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 0;
		goto ni_fini;
	}

	md = (ptl_md_t){ .start = data,
			 .length = MESSAGELENGTH,
			 .options = 0,
			 .eq_handle = eqh,
			 .ct_handle = PTL_CT_NONE };

	ret = PtlMDBind(nih, &md, &mdh);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlMDBind failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 0;
		goto eq_free;
	}

	while (get_messages < wanted_get) {
		/* get message from the specified server */
		ret = PtlGet(mdh, 0, MESSAGELENGTH, id_server, SERVER_PT, 0, 0, NULL);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlGet failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			res = 0;
			goto md_release;
		}

		/* wait to receive a message */
		ret = PtlEQWait(eqh, &ev);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			res = 0;
			goto md_release;
		}
		PtlEvToStr(0, &ev, msg);
		printf("Event : %s\n", msg);
		if (ev.ni_fail_type == PTL_NI_OK) {
			get_messages++;
			printf("Message received : \n");
			printf("   message : %s \n\n", (char *)md.start);
		}
	}

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