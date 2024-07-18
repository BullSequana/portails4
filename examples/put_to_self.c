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
 * This example shows a PUT data transfer to ourselves:
 * 	 - 	prepare target Portals resources: Event Queue, Portals Table Entry and an List Entry covering the receive buffer
 * 	 -  prepare initiator Portals resources: Event Queue (same than target), Memory Descriptor covering message buffer.
 * 	 -  data transfer with PUT operation and wait for completion events
 */

int main(void)
{
	int res = 0;
	int ret;
	ptl_handle_ni_t nih;
	ptl_handle_eq_t eqh;
	ptl_index_t pti;
	ptl_handle_le_t leh;
	ptl_process_t id;
	ptl_event_t ev;
	ptl_le_t le;
	ptl_md_t md;
	ptl_handle_md_t mdh;
	char msg[PTL_EV_STR_SIZE];
	char data[] = "Hello, put_to_self !";
	char receive_data[sizeof(data)];

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

	ret = PtlGetId(nih, &id);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto ni_fini;
	}

	ret = PtlEQAlloc(nih, 10, &eqh);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto ni_fini;
	}

	ret = PtlPTAlloc(nih, 0, eqh, PTL_PT_ANY, &pti);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto eq_free;
	}

	/* describe the memory used to receive the message */
	le = (ptl_le_t){ .start = receive_data,
			.length = sizeof(data),
			.ct_handle = PTL_CT_NONE,
			.uid = PTL_UID_ANY,
			.options = PTL_LE_OP_PUT, };

	/* expose to the network the receive buffer */
	ret = PtlLEAppend(nih, pti, &le, PTL_PRIORITY_LIST, NULL, &leh);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlLEAppend failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto pt_free;
	}

	/* wait for the LINK event that confirms we are ready for message reception*/
	ret = PtlEQWait(eqh, &ev);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto le_unlink;
	}
	PtlEvToStr(0, &ev, msg);
	printf("Event : %s \n", msg);

	/* describe the memory used to send the message */
	md = (ptl_md_t){ .start = data,
			.length = sizeof(data),
			.options = 0,
			.eq_handle = eqh,
			.ct_handle = PTL_CT_NONE };

	/* create a memory descriptor to be used to send message */
	ret = PtlMDBind(nih, &md, &mdh);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlMDBind failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto le_unlink;
	}

	/* send the message to ourself */
	ret = PtlPut(mdh, 0, sizeof(data), PTL_ACK_REQ, id, pti, 0, 0, NULL, 0);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPut failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto md_release;
	}

	/* Wait for the send, ack & put events */
	for (int i = 0; i < 3; i++) {
		ret = PtlEQWait(eqh, &ev);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			res = 1;
			goto md_release;
		}
		PtlEvToStr(0, &ev, msg);
		printf("Event : %s \n", msg);
	}

	printf("Message received : %s \n", (char *)md.start);

md_release:
	PtlMDRelease(mdh);
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