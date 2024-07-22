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

/*
 * This example initializes a logical interface, allocates an event queue, and performs
 * a LEAppend to expose to the network a portion of the process address space.
 */

int main(void)
{
	int ret;
	int res;
	ptl_handle_ni_t nih;
	ptl_handle_eq_t eqh;
	ptl_index_t pti;
	ptl_handle_le_t leh;
	ptl_event_t ev;
	char msg[PTL_EV_STR_SIZE];
	ptl_le_t le = { .start = NULL,
			.length = 0,
			.ct_handle = PTL_CT_NONE,
			.uid = PTL_UID_ANY,
			.options = PTL_LE_OP_PUT };

	ret = PtlInit();
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/* Initalize the interface handle to use */
	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_NO_MATCHING | PTL_NI_PHYSICAL, PTL_PID_ANY, NULL,
			NULL, &nih);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlNIInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto fini;
	}

	/* Allocate a buffer for incoming events */
	ret = PtlEQAlloc(nih, 10, &eqh);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto ni_fini;
	}

	/* Create a portals table to handle communications */
	ret = PtlPTAlloc(nih, 0, eqh, PTL_PT_ANY, &pti);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto eq_free;
	}

	/* Expose to the network a portion of the process address space */
	ret = PtlLEAppend(nih, pti, &le, PTL_PRIORITY_LIST, NULL, &leh);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlLEAppend failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto pt_free;
	}
	printf("A portion of the process address space have been exposed to the network\n");

	/* Read and remove the event from the allocated events queue */
	ret = PtlEQWait(eqh, &ev);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		res = 1;
		goto le_unlink;
	}
	res = 0;
	PtlEvToStr(0, &ev, msg);
	printf("Event : %s\n", msg);

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
