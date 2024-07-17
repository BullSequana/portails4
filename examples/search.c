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
 * This example show how does PtlSearch work. We send a message to ourselves
 * and search for the message in the reception queue.
 */

int main(void)
{
	int res = 0;
	int ret;
	ptl_handle_ni_t nih;
	ptl_handle_eq_t eqh;
	ptl_index_t pti;
	ptl_handle_me_t meh;
	ptl_process_t id;
	ptl_event_t ev;
	ptl_handle_md_t mdh;
	ptl_me_t me;
	ptl_md_t md;
	char msg[PTL_EV_STR_SIZE];

	ret = PtlInit();
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_MATCHING | PTL_NI_PHYSICAL, PTL_PID_ANY, NULL,
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
	me = (ptl_me_t){ .start = NULL,
			 .length = 0,
			 .ct_handle = PTL_CT_NONE,
			 .uid = PTL_UID_ANY,
			 .options = PTL_ME_OP_PUT,
			 .match_id = PTL_NID_ANY,
			 .match_bits = 1,
			 .ignore_bits = 0,
			 .min_free = 0 };

	/* expose to the network the reception queue */
	ret = PtlMEAppend(nih, pti, &me, PTL_OVERFLOW_LIST, NULL, &meh);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlLEAppend failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto pt_free;
	}

	ret = PtlEQWait(eqh, &ev);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto me_unlink;
	} else {
		PtlEvToStr(0, &ev, msg);
		printf("Event : %s\n", msg);
	}

	/* describe the memory used to send the message */
	md = (ptl_md_t){
		.start = NULL, .length = 0, .options = 0, .eq_handle = eqh, .ct_handle = PTL_CT_NONE
	};

	ptl_hdr_data_t header_data = 0;

	ret = PtlMDBind(nih, &md, &mdh);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlMDBind failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto me_unlink;
	}

	/* send the message to ourself */
	ret = PtlPut(mdh, 0, 0, PTL_ACK_REQ, id, pti, 1, 0, NULL, header_data);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPut failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto md_release;
	}

	printf("The message has been sent !\n");

	for (int i = 0; i < 3; i++) {
		ret = PtlEQWait(eqh, &ev);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			res = 1;
			goto md_release;
		}
		PtlEvToStr(0, &ev, msg);
		printf("Event : %s\n", msg);
	}

	printf("Let's search the message in the reception queue with the wrong match bits...\n");
	me.match_bits = 0;
	/* try to find me */
	ret = PtlMESearch(nih, pti, &me, PTL_SEARCH_ONLY, NULL);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlMESearch failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto md_release;
	}

	ret = PtlEQWait(eqh, &ev);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto md_release;
	}
	PtlEvToStr(0, &ev, msg);
	printf("Event : %s\n", msg);

	printf("Let's search the message in the reception queue with the right match bits...\n");
	me.match_bits = 1;
	/* try to find me */
	ret = PtlMESearch(nih, pti, &me, PTL_SEARCH_ONLY, NULL);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlMESearch failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto md_release;
	}

	ret = PtlEQWait(eqh, &ev);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto md_release;
	}
	PtlEvToStr(0, &ev, msg);
	printf("Event : %s\n", msg);

md_release:
	PtlMDRelease(mdh);
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