#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portals4.h"
#include "portals4_bxiext.h"

/*
 * This example show the sending of a message to ourselves, we get our Id,
 * create a message and send it through a specified allocated buffer"
 */

int main(void)
{
	int ret;
	ptl_handle_ni_t retNIInit;
	ptl_handle_eq_t retEQAlloc;
	ptl_index_t retPTAlloc;
	ptl_handle_le_t retAppend;
	ptl_process_t id;
	ptl_event_t retWait;
	ptl_handle_md_t retBind;
	char msg[PTL_EV_STR_SIZE];
	char data[] = "Hello, put_to_self !";
	char receive_data[sizeof(data)];

	/*represent the memory used to receive the message*/
	ptl_le_t le = { .start = receive_data,
			.length = sizeof(data),
			.ct_handle = PTL_CT_NONE,
			.uid = PTL_UID_ANY,
			.options = PTL_LE_OP_PUT };

	/*represent the memory used to send the message*/
	ptl_md_t md = { .start = data,
			.length = sizeof(data),
			.options = 0,
			.eq_handle = PTL_EQ_NONE,
			.ct_handle = PTL_CT_NONE };
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

	ret = PtlPTAlloc(retNIInit, PTL_PT_FLOWCTRL, retEQAlloc, PTL_PT_ANY, &retPTAlloc);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPTAlloc failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	ret = PtlGetId(retNIInit, &id);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlGetId failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/*prepare our events buffer to receive a message represented by le*/
	ret = PtlLEAppend(retNIInit, retPTAlloc, &le, PTL_PRIORITY_LIST, NULL, &retAppend);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlLEAppend failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/*wait for the event link*/
	ret = PtlEQWait(retEQAlloc, &retWait);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	} else {
		PtlEvToStr(0, &retWait, msg);
		printf("Event : %s", msg);
	}

	md.eq_handle = retEQAlloc;
	/*allocate a buffer for our message represented by md*/
	ret = PtlMDBind(retNIInit, &md, &retBind);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlMDBind failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/*send the message to ourself*/
	ret = PtlPut(retBind, 0, sizeof(data), PTL_ACK_REQ, id, retPTAlloc, 0, 0, NULL,
		     header_data);
	if (ret != PTL_OK) {
		fprintf(stderr, "PtlPut failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		return 1;
	}

	/* Wait for the send, ack & put events */
	for (int i = 0; i < 3; i++) {
		ret = PtlEQWait(retEQAlloc, &retWait);
		if (ret != PTL_OK) {
			fprintf(stderr, "PtlEQWait failed : %s \n", PtlToStr(ret, PTL_STR_ERROR));
		} else {
			PtlEvToStr(0, &retWait, msg);
			printf("Event : %s", msg);
		}
	}

	printf("Message received : %s \n", (char *)md.start);
}