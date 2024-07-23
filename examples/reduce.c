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
 * This example show how to make a sum calculus with more than two knots (here four knots).
 *
 * ┌──────────────────┐   ┌──────────────────┐     ┌──────────────────┐   ┌──────────────────┐
 * │                  │   │                  │     │                  │   │                  │
 * │   md = {1,2,3}   │   │   md = {4,5,6}   │     │   md = {7,8,9}   │   │ md = {10,11,12}  │
 * │                  │   │                  │     │                  │   │                  │
 * └──────────────────┴─┬─┴──────────────────┘     └──────────────────┴─┬─┴──────────────────┘
 *                      │                                               │
 *                      │ PtlAtomic(_, PTL_SUM, _)                      │ PtlAtomic(_, PTL_SUM, _)
 *                      │                                               │
 *                ┌─────▼──────┐                                  ┌─────▼──────┐
 *                │    me0 =   │                                  │    me0 =   │
 *                │   {5,7,9}  │                                  │ {17,19,21} │
 *                └─────┬──────┘                                  └─────┬──────┘
 *                      │ copy to a md_handle                           │ copy to a md_handle
 *            ┌─────────▼──────────┐                          ┌─────────▼──────────┐
 *            │                    │                          │                    │
 *            │    md = {5,7,9}    │                          │  md = {17,19,21}   │
 *            │                    │                          │                    │
 *            └────────────────────┴─────────────┬────────────┴────────────────────┘
 *                                               │
 *                                               │ PtlTriggeredAtomic(_, PTL_SUM, _)
 *                                               │
 *                                    ┌──────────▼──────────┐
 *                                    │                     │
 *                                    │   me1 = {22,26,30}  │
 *                                    │                     │
 *                                    └─────────────────────┘
 *
 */

struct rank_info {
	ptl_handle_ct_t counter;
	int size;
	int rank[3];
	int *data;
	int *final_data;
};

/* used to link rank number to his rank_info */
struct rank_info ranks[4];

int rank_main(int rank, ptl_handle_ni_t nih, ptl_process_t id)
{
	char *side;
	ptl_handle_le_t leh;
	ptl_ct_event_t increment = { .success = 1, .failure = -1 };
	ptl_handle_md_t mdh;
	char msg[PTL_EV_STR_SIZE];
	ptl_event_t ev;
	ptl_handle_eq_t eqh;
	ptl_index_t pti0;
	ptl_index_t pti1;

	int ret = PtlEQAlloc(nih, 10, &eqh);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlPTAlloc(nih, PTL_PT_FLOWCTRL, eqh, PTL_PT_ANY, &pti0);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/* init me0 */

	ptl_me_t me0 = { .start = ranks[rank].data,
			 .length = ranks[rank].size,
			 .ct_handle = PTL_CT_NONE,
			 .uid = PTL_UID_ANY,
			 .options = PTL_ME_OP_PUT,
			 .match_id = PTL_NID_ANY,
			 .match_bits = 0,
			 .ignore_bits = 0,
			 .min_free = 0 };

	ret = PtlLEAppend(nih, pti0, &me0, PTL_PRIORITY_LIST, NULL, &leh);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlLEAppend failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlEQWait(eqh, &ev);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}
	PtlEvToStr(0, &ev, msg);
	printf("Event : %s", msg);

	ptl_md_t md = { .start = ranks[rank].rank,
			.length = ranks[rank].size,
			.options = 0,
			.eq_handle = eqh,
			.ct_handle = PTL_CT_NONE };

	ret = PtlMDBind(nih, &md, &mdh);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlMDBind failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/* sending the content of md to me0 */
	ret = PtlAtomic(mdh, 0, ranks[rank].size, PTL_ACK_REQ, id, pti0, 0, 0, NULL, 0, PTL_SUM,
			PTL_UINT32_T);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlAtomic failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		ret = PtlCTInc(ranks[rank].counter, increment);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlCTInc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}
	}

	ret = PtlEQWait(eqh, &ev);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}
	PtlEvToStr(0, &ev, msg);
	printf("Event : %s", msg);

	ret = PtlEQWait(eqh, &ev);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}
	PtlEvToStr(0, &ev, msg);
	printf("Event : %s", msg);

	ret = PtlEQWait(eqh, &ev);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}
	PtlEvToStr(0, &ev, msg);
	printf("Event : %s", msg);

	if (rank < 2) {
		side = "left";
	} else {
		side = "right";
	}
	int *num0 = me0.start;
	printf("content of me0 %s: {%d,%d,%d} \n", side, num0[0], num0[1], num0[2]);

	/* if we have completed me0 with both md */
	ret = PtlEQAlloc(nih, 10, &eqh);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlPTAlloc(nih, PTL_PT_FLOWCTRL, eqh, PTL_PT_ANY, &pti1);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	if (rank % 2 == 1) {
		printf("counter is ready, let's perform triggered operation ! \n");

		ptl_me_t me1 = { .start = ranks[rank].final_data,
				 .length = ranks[rank].size,
				 .ct_handle = ranks[rank].counter,
				 .uid = PTL_UID_ANY,
				 .options = PTL_ME_OP_PUT,
				 .match_id = PTL_NID_ANY,
				 .match_bits = 0,
				 .ignore_bits = 0,
				 .min_free = 0 };

		ret = PtlLEAppend(nih, pti1, &me1, PTL_PRIORITY_LIST, NULL, &leh);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlLEAppend failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}

		ret = PtlEQWait(eqh, &ev);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}
		PtlEvToStr(0, &ev, msg);
		printf("Event : %s", msg);

		/* we copy the start of me0 to the start of md */
		md.start = me0.start;

		ret = PtlMDBind(nih, &md, &mdh);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlMDBind failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}

		ret = PtlTriggeredAtomic(mdh, 0, ranks[rank].size, PTL_ACK_REQ, id, pti1, 0, 0,
					 NULL, 0, PTL_SUM, PTL_UINT32_T, ranks[rank].counter, 2);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlAtomic failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}

		ret = PtlEQWait(eqh, &ev);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}
		PtlEvToStr(0, &ev, msg);
		printf("Event : %s", msg);

		int *num0 = me1.start;
		printf("content of me1 : {%d,%d,%d} \n", num0[0], num0[1], num0[2]);
	}
	return 0;
}

int main(void)
{
	int ret;
	ptl_handle_ni_t nih;
	ptl_process_t id;
	ptl_handle_ct_t counter0;
	ptl_handle_ct_t counter1;

	int rank0[3] = { 1, 2, 3 };
	int rank1[3] = { 4, 5, 6 };
	int rank2[3] = { 7, 8, 9 };
	int rank3[3] = { 10, 11, 12 };

	int data0[3] = { 0, 0, 0 };
	int data1[3] = { 0, 0, 0 };
	int data2[3] = { 0, 0, 0 };

	/* init interface and handles */
	ret = PtlInit();
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_NO_MATCHING | PTL_NI_PHYSICAL, PTL_PID_ANY, NULL,
			NULL, &nih);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlNIInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlCTAlloc(nih, &counter0);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlCTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlCTAlloc(nih, &counter1);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlCTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlGetId(nih, &id);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/* start our atomic/triggered operations */

	ranks[0].counter = counter0;
	ranks[0].size = sizeof(rank0);
	memcpy(ranks[0].rank, rank0, 3 * sizeof(int));
	ranks[0].data = data0;

	ret = rank_main(0, nih, id);
	if (ret == 1) {
		return 1;
	}

	ranks[1].counter = counter0;
	ranks[1].size = sizeof(rank1);
	memcpy(ranks[1].rank, rank1, 3 * sizeof(int));
	ranks[1].data = data0;
	ranks[1].final_data = data2;

	ret = rank_main(1, nih, id);
	if (ret == 1) {
		return 1;
	}

	ranks[2].counter = counter1;
	ranks[2].size = sizeof(rank2);
	memcpy(ranks[2].rank, rank2, 3 * sizeof(int));
	ranks[2].data = data1;

	ret = rank_main(2, nih, id);
	if (ret == 1) {
		return 1;
	}

	ranks[3].counter = counter1;
	ranks[3].size = sizeof(rank3);
	memcpy(ranks[3].rank, rank3, 3 * sizeof(int));
	ranks[3].data = data1;
	ranks[3].final_data = data2;

	ret = rank_main(3, nih, id);
	if (ret == 1) {
		return 1;
	}
}
