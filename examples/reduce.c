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
 * This example show how to make a sum calculus with more than two knots (here four knots).
 */

int main(void)
{
	int ret;
	ptl_handle_ni_t retNIInit;
	ptl_handle_eq_t retEQAlloc;
	ptl_index_t retPTAlloc0;
	ptl_index_t retPTAlloc1;
	ptl_index_t retPTAlloc2;
	ptl_handle_le_t retAppend;
	ptl_process_t id;
	ptl_event_t retWait;
	ptl_handle_md_t retBind;
	ptl_handle_ct_t counter0_bis;
	ptl_handle_ct_t counter1_bis;
	ptl_ct_event_t increment = { .success = 1, .failure = -1 };
	char msg[150];

	/*represent the memory used to receive messages*/
	int rang0[3] = { 1, 2, 3 };
	int rang1[3] = { 4, 5, 6 };
	int rang2[3] = { 7, 8, 9 };
	int rang3[3] = { 10, 11, 12 };
	int data0[3];
	int data1[3];
	int data2[3];

	ptl_me_t me0 = { .start = data0,
			 .length = sizeof(rang0),
			 .ct_handle = PTL_CT_NONE,
			 .uid = PTL_UID_ANY,
			 .options = PTL_ME_OP_PUT,
			 .match_id = PTL_NID_ANY,
			 .match_bits = 0,
			 .ignore_bits = 0,
			 .min_free = 0 };

	ptl_me_t me1 = { .start = data1,
			 .length = sizeof(rang0),
			 .ct_handle = PTL_CT_NONE,
			 .uid = PTL_UID_ANY,
			 .options = PTL_ME_OP_PUT,
			 .match_id = PTL_NID_ANY,
			 .match_bits = 0,
			 .ignore_bits = 0,
			 .min_free = 0 };

	ptl_me_t me2 = { .start = data2,
			 .length = sizeof(rang0),
			 .ct_handle = PTL_CT_NONE,
			 .uid = PTL_UID_ANY,
			 .options = PTL_ME_OP_PUT,
			 .match_id = PTL_NID_ANY,
			 .match_bits = 0,
			 .ignore_bits = 0,
			 .min_free = 0 };

	/*represent the memory used to send messages*/
	ptl_md_t md0 = { .start = rang0,
			 .length = sizeof(rang0),
			 .options = 0,
			 .eq_handle = PTL_EQ_NONE,
			 .ct_handle = PTL_CT_NONE };

	ptl_md_t md1 = { .start = rang1,
			 .length = sizeof(rang0),
			 .options = 0,
			 .eq_handle = PTL_EQ_NONE,
			 .ct_handle = PTL_CT_NONE };

	ptl_md_t md2 = { .start = rang2,
			 .length = sizeof(rang0),
			 .options = 0,
			 .eq_handle = PTL_EQ_NONE,
			 .ct_handle = PTL_CT_NONE };

	ptl_md_t md3 = { .start = rang3,
			 .length = sizeof(rang0),
			 .options = 0,
			 .eq_handle = PTL_EQ_NONE,
			 .ct_handle = PTL_CT_NONE };

	ptl_md_t md0_bis = { .start = data0,
			     .length = sizeof(rang0),
			     .options = 0,
			     .eq_handle = PTL_EQ_NONE,
			     .ct_handle = counter0_bis };

	ptl_md_t md1_bis = { .start = data1,
			     .length = sizeof(rang0),
			     .options = 0,
			     .eq_handle = PTL_EQ_NONE,
			     .ct_handle = counter1_bis };

	ptl_hdr_data_t header_data = 0;

	ret = PtlInit();
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_NO_MATCHING | PTL_NI_PHYSICAL, PTL_PID_ANY, NULL,
			NULL, &retNIInit);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlNIInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlEQAlloc(retNIInit, 10, &retEQAlloc);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlPTAlloc(retNIInit, PTL_PT_FLOWCTRL, retEQAlloc, PTL_PT_ANY, &retPTAlloc0);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlPTAlloc(retNIInit, PTL_PT_FLOWCTRL, retEQAlloc, PTL_PT_ANY, &retPTAlloc1);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlPTAlloc(retNIInit, PTL_PT_FLOWCTRL, retEQAlloc, PTL_PT_ANY, &retPTAlloc2);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlCTAlloc(retNIInit, &counter0_bis);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlCTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlCTAlloc(retNIInit, &counter1_bis);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlCTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/*prepare our events buffer to receive messages represented by me0, me1 and me2*/

	ret = PtlLEAppend(retNIInit, retPTAlloc0, &me0, PTL_PRIORITY_LIST, NULL, &retAppend);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlLEAppend failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	ret = PtlLEAppend(retNIInit, retPTAlloc1, &me1, PTL_PRIORITY_LIST, NULL, &retAppend);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlLEAppend failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	ret = PtlLEAppend(retNIInit, retPTAlloc2, &me2, PTL_PRIORITY_LIST, NULL, &retAppend);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlLEAppend failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	ret = PtlGetId(retNIInit, &id);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/*start our atomic/triggered operations*/

	md0.eq_handle = retEQAlloc;
	/*sending the content of md0 to me0*/
	ret = PtlMDBind(retNIInit, &md0, &retBind);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlMDBind failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlAtomic(retBind, 0, sizeof(rang0), PTL_ACK_REQ, id, retPTAlloc0, 0, 0, NULL,
			header_data, PTL_SUM, PTL_UINT32_T);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlAtomic failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		ret = PtlCTInc(counter0_bis, increment);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlCTInc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	md1.eq_handle = retEQAlloc;
	/*sending the content of md1 to me0*/
	ret = PtlMDBind(retNIInit, &md1, &retBind);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlMDBind failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlAtomic(retBind, 0, sizeof(rang0), PTL_ACK_REQ, id, retPTAlloc0, 0, 0, NULL,
			header_data, PTL_SUM, PTL_UINT32_T);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlAtomic failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		ret = PtlCTInc(counter0_bis, increment);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlCTInc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	md2.eq_handle = retEQAlloc;
	/*sending the content of md2 to me1*/
	ret = PtlMDBind(retNIInit, &md2, &retBind);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlMDBind failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlAtomic(retBind, 0, sizeof(rang0), PTL_ACK_REQ, id, retPTAlloc1, 0, 0, NULL,
			header_data, PTL_SUM, PTL_UINT32_T);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlAtomic failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		ret = PtlCTInc(counter1_bis, increment);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlCTInc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	md3.eq_handle = retEQAlloc;
	/*sending the content of md3 to me1*/
	ret = PtlMDBind(retNIInit, &md3, &retBind);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlMDBind failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlAtomic(retBind, 0, sizeof(rang0), PTL_ACK_REQ, id, retPTAlloc1, 0, 0, NULL,
			header_data, PTL_SUM, PTL_UINT32_T);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlAtomic failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		ret = PtlCTInc(counter1_bis, increment);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlCTInc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
			return 1;
		}
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	int *num0 = me0.start;
	printf("content of me0 : {%d,%d,%d} \n", num0[0], num0[1], num0[2]);

	int *num1 = me1.start;
	printf("content of me1 : {%d,%d,%d} \n", num1[0], num1[1], num1[2]);

	md0_bis.eq_handle = retEQAlloc;
	/*sending the content of md0_bis to me2*/
	ret = PtlMDBind(retNIInit, &md0_bis, &retBind);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlMDBind failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlTriggeredAtomic(retBind, 0, sizeof(rang0), PTL_ACK_REQ, id, retPTAlloc2, 0, 0,
				 NULL, header_data, PTL_SUM, PTL_UINT32_T, counter0_bis, 2);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlAtomic failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	md1_bis.eq_handle = retEQAlloc;
	/*sending the content of md1_bis to me2*/
	ret = PtlMDBind(retNIInit, &md1_bis, &retBind);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlMDBind failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlTriggeredAtomic(retBind, 0, sizeof(rang0), PTL_ACK_REQ, id, retPTAlloc2, 0, 0,
				 NULL, header_data, PTL_SUM, PTL_UINT32_T, counter1_bis, 2);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlAtomic failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	int *num2 = me2.start;
	printf("content of me2 : {%d,%d,%d} \n", num2[0], num2[1], num2[2]);
}