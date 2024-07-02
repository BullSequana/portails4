#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portals4.h"
#include "portals4_bxiext.h"

/*
 * This example initalize an interface handle to use, allocate a buffer for incoming events
 * and perform an LEAppend to generate an event in the allocated buffer.
 */

int main(void)
{
	int ret;
	ptl_handle_ni_t retNIInit;
	ptl_handle_eq_t retEQAlloc;
	ptl_index_t retPTAlloc = 0;
	ptl_handle_le_t retAppend;
	ptl_event_t retWait;
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

	/*Initalize the interface handle to use*/
	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_NO_MATCHING | PTL_NI_PHYSICAL, PTL_PID_ANY, NULL,
			NULL, &retNIInit);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlNIInit failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/*Allocate a buffer for incoming events*/
	ret = PtlEQAlloc(retNIInit, 10, &retEQAlloc);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/* Create a portals table to handle communications */
	ret = PtlPTAlloc(retNIInit, PTL_PT_FLOWCTRL, retEQAlloc, PTL_PT_ANY, &retPTAlloc);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/* Generate an PTL_EVENT_LINK event in the queue */
	ret = PtlLEAppend(retNIInit, retPTAlloc, &le, PTL_PRIORITY_LIST, NULL, &retAppend);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlLEAppend failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/*Read and remove the event from the allocated events queue*/
	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		char msg[50];
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}
}