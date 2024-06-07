#pragma once
#include "portals4.h"

struct ptl_mem_ops {
	void *(*alloc)(size_t);
	void (*free)(void *, size_t);
	void (*lock)(void *, size_t);
	void (*unlock)(void *, size_t);
};

typedef struct ptl_mem_ops ptl_mem_ops_t;

enum ptl_str_type {
	PTL_STR_ERROR, /* Return codes */
	PTL_STR_EVENT, /* Events */
	PTL_STR_FAIL_TYPE, /* Failure type */
};

typedef enum ptl_str_type ptl_str_type_t;

#define PTL_ME_MANAGE_LOCAL_STOP_IF_UH 0
#define PTL_ME_OV_RDV_PUT_ONLY 0
#define PTL_ME_OV_RDV_PUT_DISABLE 0

#define PTL_ME_UH_LOCAL_OFFSET_INC_MANIPULATED 0x200000

const char *PtlToStr(int rc, ptl_str_type_t type);

typedef struct ptl_activate_hook *ptl_activate_hook_t;

#define PTL_EV_STR_SIZE 256
int PtlEvToStr(unsigned int ni_options, ptl_event_t *e, char *msg);