#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#include "portals4.h"
#include "portals4_bxiext.h"

#include "ptl_log.h"
#include "include/swptl4.h"

#define PTL_STR_STRINGIFY(x...) #x

struct ptl_const_to_str {
	int rc;
	char *str;
};

const struct ptl_const_to_str ptl_errors[] = {
	/* Portals4.1 Return codes Spec Table 3.7 */
	{ PTL_ARG_INVALID, PTL_STR_STRINGIFY(PTL_ARG_INVALID) },
	{ PTL_CT_NONE_REACHED, PTL_STR_STRINGIFY(PTL_CT_NONE_REACHED) },
	{ PTL_EQ_DROPPED, PTL_STR_STRINGIFY(PTL_EQ_DROPPED) },
	{ PTL_EQ_EMPTY, PTL_STR_STRINGIFY(PTL_EQ_EMPTY) },
	{ PTL_FAIL, PTL_STR_STRINGIFY(PTL_FAIL) },
	{ PTL_IGNORED, PTL_STR_STRINGIFY(PTL_IGNORED) },
	{ PTL_IN_USE, PTL_STR_STRINGIFY(PTL_IN_USE) },
	{ PTL_INTERRUPTED, PTL_STR_STRINGIFY(PTL_INTERRUPTED) },
	{ PTL_LIST_TOO_LONG, PTL_STR_STRINGIFY(PTL_LIST_TOO_LONG) },
	{ PTL_NO_INIT, PTL_STR_STRINGIFY(PTL_NO_INIT) },
	{ PTL_NO_SPACE, PTL_STR_STRINGIFY(PTL_NO_SPACE) },
	{ PTL_OK, PTL_STR_STRINGIFY(PTL_OK) },
	{ PTL_PID_IN_USE, PTL_STR_STRINGIFY(PTL_PID_IN_USE) },
	{ PTL_PT_EQ_NEEDED, PTL_STR_STRINGIFY(PTL_PT_EQ_NEEDED) },
	{ PTL_PT_FULL, PTL_STR_STRINGIFY(PTL_PT_FULL) },
	{ PTL_PT_IN_USE, PTL_STR_STRINGIFY(PTL_PT_IN_USE) },
	{ PTL_FAIL, PTL_STR_STRINGIFY(PTL_FAIL) },
	{ PTL_ABORTED, PTL_STR_STRINGIFY(PTL_ABORTED) }
};

const struct ptl_const_to_str ptl_events[] = {
	/* Portals4 Events */
	{ PTL_EVENT_GET, PTL_STR_STRINGIFY(PTL_EVENT_GET) },
	{ PTL_EVENT_GET_OVERFLOW, PTL_STR_STRINGIFY(PTL_EVENT_GET_OVERFLOW) },
	{ PTL_EVENT_PUT, PTL_STR_STRINGIFY(PTL_EVENT_PUT) },
	{ PTL_EVENT_PUT_OVERFLOW, PTL_STR_STRINGIFY(PTL_EVENT_PUT_OVERFLOW) },
	{ PTL_EVENT_ATOMIC, PTL_STR_STRINGIFY(PTL_EVENT_ATOMIC) },
	{ PTL_EVENT_ATOMIC_OVERFLOW, PTL_STR_STRINGIFY(PTL_EVENT_ATOMIC_OVERFLOW) },
	{ PTL_EVENT_FETCH_ATOMIC, PTL_STR_STRINGIFY(PTL_EVENT_FETCH_ATOMIC) },
	{ PTL_EVENT_FETCH_ATOMIC_OVERFLOW, PTL_STR_STRINGIFY(PTL_EVENT_FETCH_ATOMIC_OVERFLOW) },
	{ PTL_EVENT_REPLY, PTL_STR_STRINGIFY(PTL_EVENT_REPLY) },
	{ PTL_EVENT_SEND, PTL_STR_STRINGIFY(PTL_EVENT_SEND) },
	{ PTL_EVENT_ACK, PTL_STR_STRINGIFY(PTL_EVENT_ACK) },
	{ PTL_EVENT_PT_DISABLED, PTL_STR_STRINGIFY(PTL_EVENT_PT_DISABLED) },
	{ PTL_EVENT_AUTO_UNLINK, PTL_STR_STRINGIFY(PTL_EVENT_AUTO_UNLINK) },
	{ PTL_EVENT_AUTO_FREE, PTL_STR_STRINGIFY(PTL_EVENT_AUTO_FREE) },
	{ PTL_EVENT_SEARCH, PTL_STR_STRINGIFY(PTL_EVENT_SEARCH) },
	{ PTL_EVENT_LINK, PTL_STR_STRINGIFY(PTL_EVENT_LINK) }
};

const struct ptl_const_to_str ptl_fail_type[] = {
	/* Portals4 failure type */
	{ PTL_NI_OK, PTL_STR_STRINGIFY(PTL_NI_OK) },
	{ PTL_NI_PERM_VIOLATION, PTL_STR_STRINGIFY(PTL_NI_PERM_VIOLATION) },
	{ PTL_NI_SEGV, PTL_STR_STRINGIFY(PTL_NI_SEGV) },
	{ PTL_NI_PT_DISABLED, PTL_STR_STRINGIFY(PTL_NI_PT_DISABLED) },
	{ PTL_NI_DROPPED, PTL_STR_STRINGIFY(PTL_NI_DROPPED) },
	{ PTL_NI_UNDELIVERABLE, PTL_STR_STRINGIFY(PTL_NI_UNDELIVERABLE) },
	{ PTL_FAIL, PTL_STR_STRINGIFY(PTL_FAIL) },
	{ PTL_ARG_INVALID, PTL_STR_STRINGIFY(PTL_ARG_INVALID) },
	{ PTL_IN_USE, PTL_STR_STRINGIFY(PTL_IN_USE) },
	{ PTL_NI_NO_MATCH, PTL_STR_STRINGIFY(PTL_NI_NO_MATCH) },
	{ PTL_NI_OP_VIOLATION, PTL_STR_STRINGIFY(PTL_NI_OP_VIOLATION) },
};

const char *PtlToStr(int rc, enum ptl_str_type type)
{
	int i;
	const struct ptl_const_to_str *p;
	size_t size;

	switch (type) {
	case PTL_STR_ERROR:
		p = ptl_errors;
		size = sizeof(ptl_errors);
		break;
	case PTL_STR_EVENT:
		p = ptl_events;
		size = sizeof(ptl_events);
		break;
	case PTL_STR_FAIL_TYPE:
		p = ptl_fail_type;
		size = sizeof(ptl_fail_type);
		break;
	default:
		return "Unknown";
	};
	for (i = 0; i < size / sizeof(struct ptl_const_to_str); i++) {
		if (rc == p[i].rc)
			return p[i].str;
	}
	return "Unknown";
}

int PtlFailTypeSize(void)
{
	return sizeof(ptl_fail_type) / sizeof(struct ptl_const_to_str);
}

#define PTL_ISPHYSICAL(opt) (((opt) & (PTL_NI_PHYSICAL | PTL_NI_LOGICAL)) == PTL_NI_PHYSICAL)

struct ptl_ev_desc ptl_ev_desc[17] = {
	{ "GET", PTL_EVENT_GET,
	  PTL_EV_HAS_INIT | PTL_EV_HAS_PT | PTL_EV_HAS_UID | PTL_EV_HAS_BITS | PTL_EV_HAS_RLEN |
		  PTL_EV_HAS_MLEN | PTL_EV_HAS_ROFFS | PTL_EV_HAS_START | PTL_EV_HAS_UPTR,
	  PTL_EV_HAS_INIT | PTL_EV_HAS_PT | PTL_EV_HAS_UID | PTL_EV_HAS_BITS | PTL_EV_HAS_MLEN |
		  PTL_EV_HAS_START | PTL_EV_HAS_UPTR },
	{ "GET_OVERFLOW", PTL_EVENT_GET_OVERFLOW,
	  PTL_EV_HAS_INIT | PTL_EV_HAS_PT | PTL_EV_HAS_UID | PTL_EV_HAS_BITS | PTL_EV_HAS_RLEN |
		  PTL_EV_HAS_MLEN | PTL_EV_HAS_ROFFS | PTL_EV_HAS_START | PTL_EV_HAS_UPTR,
	  PTL_EV_HAS_INIT | PTL_EV_HAS_PT | PTL_EV_HAS_UID | PTL_EV_HAS_BITS | PTL_EV_HAS_MLEN |
		  PTL_EV_HAS_START | PTL_EV_HAS_UPTR },
	{ "PUT", PTL_EVENT_PUT,
	  PTL_EV_HAS_INIT | PTL_EV_HAS_PT | PTL_EV_HAS_UID | PTL_EV_HAS_BITS | PTL_EV_HAS_RLEN |
		  PTL_EV_HAS_MLEN | PTL_EV_HAS_ROFFS | PTL_EV_HAS_START | PTL_EV_HAS_UPTR |
		  PTL_EV_HAS_HDR,
	  PTL_EV_HAS_INIT | PTL_EV_HAS_PT | PTL_EV_HAS_UID | PTL_EV_HAS_BITS | PTL_EV_HAS_MLEN |
		  PTL_EV_HAS_START | PTL_EV_HAS_UPTR | PTL_EV_HAS_HDR },
	{ "PUT_OVERFLOW", PTL_EVENT_PUT_OVERFLOW,
	  PTL_EV_HAS_INIT | PTL_EV_HAS_PT | PTL_EV_HAS_UID | PTL_EV_HAS_BITS | PTL_EV_HAS_RLEN |
		  PTL_EV_HAS_MLEN | PTL_EV_HAS_ROFFS | PTL_EV_HAS_START | PTL_EV_HAS_UPTR |
		  PTL_EV_HAS_HDR,
	  PTL_EV_HAS_INIT | PTL_EV_HAS_PT | PTL_EV_HAS_UID | PTL_EV_HAS_BITS | PTL_EV_HAS_MLEN |
		  PTL_EV_HAS_START | PTL_EV_HAS_UPTR | PTL_EV_HAS_HDR },
	{ "ATOMIC", PTL_EVENT_ATOMIC,
	  PTL_EV_HAS_INIT | PTL_EV_HAS_PT | PTL_EV_HAS_UID | PTL_EV_HAS_BITS | PTL_EV_HAS_RLEN |
		  PTL_EV_HAS_MLEN | PTL_EV_HAS_ROFFS | PTL_EV_HAS_START | PTL_EV_HAS_UPTR |
		  PTL_EV_HAS_HDR | PTL_EV_HAS_AOP | PTL_EV_HAS_ATYP,
	  PTL_EV_HAS_INIT | PTL_EV_HAS_PT | PTL_EV_HAS_UID | PTL_EV_HAS_BITS | PTL_EV_HAS_MLEN |
		  PTL_EV_HAS_START | PTL_EV_HAS_UPTR | PTL_EV_HAS_HDR | PTL_EV_HAS_AOP |
		  PTL_EV_HAS_ATYP },
	{ "ATOMIC_OVERFLOW", PTL_EVENT_ATOMIC_OVERFLOW,
	  PTL_EV_HAS_INIT | PTL_EV_HAS_PT | PTL_EV_HAS_UID | PTL_EV_HAS_BITS | PTL_EV_HAS_RLEN |
		  PTL_EV_HAS_MLEN | PTL_EV_HAS_ROFFS | PTL_EV_HAS_START | PTL_EV_HAS_UPTR |
		  PTL_EV_HAS_HDR | PTL_EV_HAS_AOP | PTL_EV_HAS_ATYP,
	  PTL_EV_HAS_INIT | PTL_EV_HAS_PT | PTL_EV_HAS_UID | PTL_EV_HAS_BITS | PTL_EV_HAS_MLEN |
		  PTL_EV_HAS_START | PTL_EV_HAS_UPTR | PTL_EV_HAS_HDR | PTL_EV_HAS_AOP |
		  PTL_EV_HAS_ATYP },
	{ "FETCH_ATOMIC", PTL_EVENT_FETCH_ATOMIC,
	  PTL_EV_HAS_INIT | PTL_EV_HAS_PT | PTL_EV_HAS_UID | PTL_EV_HAS_BITS | PTL_EV_HAS_RLEN |
		  PTL_EV_HAS_MLEN | PTL_EV_HAS_ROFFS | PTL_EV_HAS_START | PTL_EV_HAS_UPTR |
		  PTL_EV_HAS_HDR | PTL_EV_HAS_AOP | PTL_EV_HAS_ATYP,
	  PTL_EV_HAS_INIT | PTL_EV_HAS_PT | PTL_EV_HAS_UID | PTL_EV_HAS_BITS | PTL_EV_HAS_MLEN |
		  PTL_EV_HAS_START | PTL_EV_HAS_UPTR | PTL_EV_HAS_HDR | PTL_EV_HAS_AOP |
		  PTL_EV_HAS_ATYP },
	{ "FETCH_ATOMIC_OVERFLOW", PTL_EVENT_FETCH_ATOMIC_OVERFLOW,
	  PTL_EV_HAS_INIT | PTL_EV_HAS_PT | PTL_EV_HAS_UID | PTL_EV_HAS_BITS | PTL_EV_HAS_RLEN |
		  PTL_EV_HAS_MLEN | PTL_EV_HAS_ROFFS | PTL_EV_HAS_START | PTL_EV_HAS_UPTR |
		  PTL_EV_HAS_HDR | PTL_EV_HAS_AOP | PTL_EV_HAS_ATYP,
	  PTL_EV_HAS_INIT | PTL_EV_HAS_PT | PTL_EV_HAS_UID | PTL_EV_HAS_BITS | PTL_EV_HAS_MLEN |
		  PTL_EV_HAS_START | PTL_EV_HAS_UPTR | PTL_EV_HAS_HDR | PTL_EV_HAS_AOP |
		  PTL_EV_HAS_ATYP },
	{ "REPLY", PTL_EVENT_REPLY,
	  PTL_EV_HAS_LIST | PTL_EV_HAS_MLEN | PTL_EV_HAS_ROFFS | PTL_EV_HAS_UPTR, PTL_EV_HAS_UPTR },
	{ "SEND", PTL_EVENT_SEND, PTL_EV_HAS_MLEN | PTL_EV_HAS_UPTR, PTL_EV_HAS_UPTR },
	{ "ACK", PTL_EVENT_ACK,
	  PTL_EV_HAS_LIST | PTL_EV_HAS_MLEN | PTL_EV_HAS_ROFFS | PTL_EV_HAS_UPTR, PTL_EV_HAS_UPTR },
	{ "PT_DISABLED", PTL_EVENT_PT_DISABLED, PTL_EV_HAS_PT, PTL_EV_HAS_PT },
	{ "LINK", PTL_EVENT_LINK, PTL_EV_HAS_PT | PTL_EV_HAS_UPTR,
	  PTL_EV_HAS_PT | PTL_EV_HAS_UPTR },
	{ "AUTO_UNLINK", PTL_EVENT_AUTO_UNLINK, PTL_EV_HAS_PT | PTL_EV_HAS_UPTR,
	  PTL_EV_HAS_PT | PTL_EV_HAS_UPTR },
	{ "AUTO_FREE", PTL_EVENT_AUTO_FREE, PTL_EV_HAS_PT | PTL_EV_HAS_UPTR,
	  PTL_EV_HAS_PT | PTL_EV_HAS_UPTR },
	{ "SEARCH", PTL_EVENT_SEARCH,
	  PTL_EV_HAS_INIT | PTL_EV_HAS_PT | PTL_EV_HAS_UID | PTL_EV_HAS_BITS | PTL_EV_HAS_RLEN |
		  PTL_EV_HAS_MLEN | PTL_EV_HAS_ROFFS | PTL_EV_HAS_START | PTL_EV_HAS_UPTR |
		  PTL_EV_HAS_HDR | PTL_EV_HAS_AOP | PTL_EV_HAS_ATYP,
	  PTL_EV_HAS_INIT | PTL_EV_HAS_PT | PTL_EV_HAS_UID | PTL_EV_HAS_BITS | PTL_EV_HAS_RLEN |
		  PTL_EV_HAS_MLEN | PTL_EV_HAS_ROFFS | PTL_EV_HAS_START | PTL_EV_HAS_UPTR |
		  PTL_EV_HAS_HDR | PTL_EV_HAS_AOP | PTL_EV_HAS_ATYP },
	{ NULL, 0, 0 }
};

struct ptl_fail_desc {
	char *name;
	int fail;
} ptl_fail_desc[] = { { "OK", PTL_NI_OK },
		      { "PERM_VIOLATION", PTL_NI_PERM_VIOLATION },
		      { "SEGV", PTL_NI_SEGV },
		      { "PT_DISABLED", PTL_NI_PT_DISABLED },
		      { "DROPPED", PTL_NI_DROPPED },
		      { "UNDELIVERABLE", PTL_NI_UNDELIVERABLE },
		      { "ARG_INVALID", PTL_ARG_INVALID },
		      { "IN_USE", PTL_IN_USE },
		      { "NO_MATCH", PTL_NI_NO_MATCH },
		      { "OP_VIOLATION", PTL_NI_OP_VIOLATION },
		      { "FAIL", PTL_FAIL },
		      { NULL, 0 } };

int ptl_evtostr(unsigned int ni_options, ptl_event_t *e, char *msg)
{
	struct ptl_ev_desc *d;
	struct ptl_fail_desc *f;
	int flags;
	int len = 0;

	d = ptl_ev_desc;
	for (;;) {
		if (d->name == NULL) {
#ifndef __KERNEL__
			ptl_log("type=0x%x\n", e->type);
#endif
			return -1;
		}
		if (d->type == e->type)
			break;
		d++;
	}

	len += snprintf(msg + len, SWPTL_EV_STR_SIZE - len, "type=%s", d->name);

	f = ptl_fail_desc;
	for (;;) {
		if (f->name == NULL) {
#ifndef __KERNEL__
			ptl_log(", fail=0x%x\n", e->ni_fail_type);
#endif
			return -1;
		}
		if (f->fail == e->ni_fail_type)
			break;
		f++;
	}
	len += snprintf(msg + len, SWPTL_EV_STR_SIZE - len, " fail=%s", f->name);

	flags = e->ni_fail_type == PTL_OK ? d->flags : d->fail_flags;

	if (flags & PTL_EV_HAS_INIT) {
		if (PTL_ISPHYSICAL(ni_options)) {
			len += snprintf(msg + len, SWPTL_EV_STR_SIZE - len, ", init=(%u,%u)",
					e->initiator.phys.nid, e->initiator.phys.pid);
		} else
			len += snprintf(msg + len, SWPTL_EV_STR_SIZE - len, ", init=(%u)",
					e->initiator.rank);
	}
	if (flags & PTL_EV_HAS_UID) {
		len += snprintf(msg + len, SWPTL_EV_STR_SIZE - len, ", uid=%d", e->uid);
	}
	if (flags & PTL_EV_HAS_BITS) {
		len += snprintf(msg + len, SWPTL_EV_STR_SIZE - len, ", bits=%" PRIx64,
				e->match_bits);
	}
	if (flags & PTL_EV_HAS_RLEN) {
		len += snprintf(msg + len, SWPTL_EV_STR_SIZE - len, ", rlen=%" PRIx64, e->rlength);
	}
	if (flags & PTL_EV_HAS_PT) {
		len += snprintf(msg + len, SWPTL_EV_STR_SIZE - len, ", pt=%u", e->pt_index);
	}
	if (flags & PTL_EV_HAS_HDR) {
		len += snprintf(msg + len, SWPTL_EV_STR_SIZE - len, ", hdr=%" PRIx64, e->hdr_data);
	}
	if (flags & PTL_EV_HAS_LIST) {
		len += snprintf(msg + len, SWPTL_EV_STR_SIZE - len, ", list=%d", e->ptl_list);
	}
	if (flags & PTL_EV_HAS_UPTR) {
		len += snprintf(msg + len, SWPTL_EV_STR_SIZE - len, ", uptr=%p", e->user_ptr);
	}
	if (flags & PTL_EV_HAS_MLEN) {
		len += snprintf(msg + len, SWPTL_EV_STR_SIZE - len, ", mlen=%" PRIx64, e->mlength);
	}
	if (flags & PTL_EV_HAS_ROFFS) {
		len += snprintf(msg + len, SWPTL_EV_STR_SIZE - len, ", roffs=%" PRIx64,
				e->remote_offset);
	}
	if (flags & PTL_EV_HAS_START) {
		len += snprintf(msg + len, SWPTL_EV_STR_SIZE - len, ", start=%p", (void *)e->start);
	}
	if (flags & PTL_EV_HAS_AOP) {
		len += snprintf(msg + len, SWPTL_EV_STR_SIZE - len, ", aop=0x%x",
				e->atomic_operation);
	}
	if (flags & PTL_EV_HAS_ATYP) {
		len += snprintf(msg + len, SWPTL_EV_STR_SIZE - len, ", atyp=0x%x", e->atomic_type);
	}
	len += snprintf(msg + len, SWPTL_EV_STR_SIZE - len, "\n");

	return len;
}
