#include <complex.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <stdbool.h>
#include <stdatomic.h>

#include "bximsg.h"
#include "swptl.h"
#include "timo.h"
#include "utils.h"
#include "ptl.h"
#include "ptl_log.h"

#ifdef DEBUG
#define LOGN(n, ...) do {				\
		if (swptl_verbose >= (n))		\
			ptl_log(__VA_ARGS__);		\
	} while (0)
#define LOG(...)	LOGN(1, __VA_ARGS__)
#else
#define LOGN(n, ...) do {} while (0)
#define LOG(...) do {} while (0)
#endif

#define SWPTL_MAX_FDS	1024
#define SWPTL_DEV_NMAX	4

int swptl_ct_cmd(int, struct swptl_ct *, ptl_size_t, ptl_size_t);
void swptl_snd_qstart(struct swptl_ni *, struct swptl_sodata *, struct swptl_query *);
void swptl_snd_rstart(struct swptl_ni *, struct swptl_sodata *, struct swptl_reply *);
void swptl_snd_qdat(struct swptl_ni *, struct swptl_sodata *, size_t, void **, size_t *);
void swptl_snd_rdat(struct swptl_ni *, struct swptl_sodata *, size_t, void **, size_t *);
void swptl_snd_qend(struct swptl_ni *, struct swptl_sodata *, int);
void swptl_snd_rend(struct swptl_ni *, struct swptl_sodata *, int);

int swptl_rcv_qstart(struct swptl_ni *,
		     struct swptl_query *, int, int, int,
		     struct swptl_sodata **, size_t *);
void swptl_rcv_qdat(struct swptl_ni *, struct swptl_sodata *, size_t, void **, size_t *);
void swptl_rcv_qend(struct swptl_ni *, struct swptl_sodata *, int);
int swptl_rcv_rstart(struct swptl_ni *,
		     struct swptl_reply *, int, int, int,
		     struct swptl_sodata **, size_t *);
void swptl_rcv_rdat(struct swptl_ni *, struct swptl_sodata *, size_t, void **, size_t *);
void swptl_rcv_rend(struct swptl_ni *, struct swptl_sodata *, int);
void swptl_tend(struct swptl_ni *, struct swptl_sodata *, int);

void swptl_snd_start(void *, struct swptl_sodata *, void *);
void swptl_snd_data(void *, struct swptl_sodata *, size_t, void **, size_t *);
void swptl_snd_end(void *, struct swptl_sodata *, int);
int swptl_rcv_start(void *, void *, int, int, int, int, int,
		    struct swptl_sodata **, size_t *);
void swptl_rcv_data(void *, struct swptl_sodata *, size_t, void **, size_t *);
void swptl_rcv_end(void *, struct swptl_sodata *, int);
void swptl_conn_err(void *arg, struct bximsg_conn *conn);
void swptl_postack(struct swptl_md *, int, int, int, uint32_t, uint64_t, void *);

struct bximsg_ops swptl_bximsg_ops = {
	swptl_snd_start, swptl_snd_data, swptl_snd_end,
	swptl_rcv_start, swptl_rcv_data, swptl_rcv_end,
	swptl_conn_err
};

int swptl_dump_pending = 0;

char *swptl_cmdname[] = {
	"PUT",
	"GET",
	"ATOMIC",
	"FETCH",
	"SWAP",
	"CTINC",
	"CTSET"
};

char *swptl_ackname[] = {
	"none",
	"ct",
	"oc",
	"full"
};

char *swptl_aopname[] = {
	"min",		"max",		"sum",		"prod",
	"lor",		"land",		"lxor",		"0x7",
	"bor",		"band",		"bxor",		"0xb",
	"swap",		"0xd",		"0xe",		"0xf",
	"cswap_gt",	"cswap_lt",	"cswap_ge",	"cswap_le",
	"cswap",	"cswap_ne",	"0x16",		"0x17",
	"mswap",	"0x19",		"0x1a",		"0x1b",
	"0x1c",		"0x1d",		"0x1e",		"0x1f"
};

char *swptl_atypename[] = {
	"s8",		"u8",		"s16",		"u16",
	"s32",		"u32",		"s64",		"u64",
	"0x8",		"0x9",		"f32",		"c32",
	"f64",		"c64",		"0xe",		"0xf",
	"0x10",		"0x11",		"0x12",		"0x13",
	"f80",		"c80",		"0x16",		"0x17",
	"0x18",		"0x19",		"0x1a",		"0x1b",
	"0x1c",		"0x1d",		"0x1e",		"0x1f"
};

int swptl_verbose = 0;

#include "ptl_getenv.h"

struct swptl_dev *swptl_dev_list = NULL;

pthread_mutex_t swptl_init_mutex = PTHREAD_MUTEX_INITIALIZER;
int swptl_init_count = 0;
static atomic_bool swptl_aborting;

char swptl_dummy[0x1000];

/*
 * On a single line, print a string followed by the hex representation
 * of the given block. If the block is too large, it's truncated
 */
void
swptl_log_hex(char *fmt, void *addr, size_t len)
{
#define LOG_HEX_MAX	32
	size_t todo;
	unsigned char *p = addr;
	char buf[PTL_LOG_BUF_SIZE];
	int buf_len;

	todo = (len <= LOG_HEX_MAX) ? len : LOG_HEX_MAX;
	buf_len = snprintf(buf, sizeof(buf), "%s", fmt);
	while (todo > 0) {
		if (todo < len)
			buf_len += snprintf(buf + buf_len,
					    sizeof(buf) - buf_len, " ");
		buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len,
				    "%02x", *p);
		todo--;
		p++;
	}
	if (len > LOG_HEX_MAX)
		buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len,
				    " ...");
	snprintf(buf + buf_len, sizeof(buf) - buf_len, "\n");

	ptl_log("%s", buf);
}

static size_t
swptl_qhdr_getsize(uint8_t cmd)
{
	return (cmd == SWPTL_SWAP) ?
		offsetof(struct swptl_hdr, u) + sizeof(struct swptl_query) :
		offsetof(struct swptl_hdr, u.query.swapcst);
}

/*
 * Print an array of one of the given atomic types
 */
void
swptl_mem_log(void *addr, size_t size, int atype)
{
	int i;
	char buf[PTL_LOG_BUF_SIZE];
	int buf_len = 0;

	switch (atype) {
	case PTL_INT8_T:
	case PTL_UINT8_T:
		size /= sizeof(int8_t);
		for (i = 0; i < size; i++) {
			buf_len += snprintf(buf + buf_len,
					    sizeof(buf) - buf_len, "%02x%s",
					    ((int8_t *)addr)[i] & 0xff,
					    (i % 16 == 15) ? "\n" : " ");
			if (i % 16 == 15) {
				ptl_log("%s", buf);
				buf_len = 0;
			}
		}
		break;
	case PTL_INT16_T:
	case PTL_UINT16_T:
		size /= sizeof(int16_t);
		for (i = 0; i < size; i++) {
			buf_len += snprintf(buf + buf_len,
					    sizeof(buf) - buf_len, "%04x%s",
					    ((int16_t *)addr)[i] & 0xffff,
					    (i % 8 == 7) ? "\n" : " ");
			if (i % 8 == 7) {
				ptl_log("%s", buf);
				buf_len = 0;
			}
		}
		break;
	case PTL_INT32_T:
	case PTL_UINT32_T:
		size /= sizeof(int32_t);
		for (i = 0; i < size; i++) {
			buf_len += snprintf(buf + buf_len,
					    sizeof(buf) - buf_len, "%08x%s",
					    ((int32_t *)addr)[i],
					    (i % 8 == 7) ? "\n" : " ");
			if (i % 8 == 7) {
				ptl_log("%s", buf);
				buf_len = 0;
			}
		}
		break;
	case PTL_INT64_T:
	case PTL_UINT64_T:
		size /= sizeof(int64_t);
		for (i = 0; i < size; i++) {
			buf_len += snprintf(buf + buf_len,
					    sizeof(buf) - buf_len, "%016llx%s",
					    (long long)((int64_t *)addr)[i],
					    (i % 8 == 7) ? "\n" : " ");
			if (i % 8 == 7) {
				ptl_log("%s", buf);
				buf_len = 0;
			}
		}
		break;
	case PTL_FLOAT:
		size /= sizeof(float);
		for (i = 0; i < size; i++) {
			buf_len += snprintf(buf + buf_len,
					    sizeof(buf) - buf_len, "%g%s",
					    ((float *)addr)[i],
					    (i % 8 == 7) ? "\n" : " ");
			if (i % 8 == 7) {
				ptl_log("%s", buf);
				buf_len = 0;
			}
		}
		break;
	case PTL_DOUBLE:
		size /= sizeof(double);
		for (i = 0; i < size; i++) {
			buf_len += snprintf(buf + buf_len,
					    sizeof(buf) - buf_len, "%g%s",
					    ((double *)addr)[i],
					    (i % 8 == 7) ? "\n" : " ");
			if (i % 8 == 7) {
				ptl_log("%s", buf);
				buf_len = 0;
			}
		}
		break;
	case PTL_LONG_DOUBLE:
		size /= sizeof(long double);
		for (i = 0; i < size; i++) {
			buf_len += snprintf(buf + buf_len,
					    sizeof(buf) - buf_len, "%Lg%s",
					    ((long double *)addr)[i],
					    (i % 8 == 7) ? "\n" : " ");
			if (i % 8 == 7) {
				ptl_log("%s", buf);
				buf_len = 0;
			}
		}
		break;
	case PTL_FLOAT_COMPLEX:
		size /= sizeof(float complex);
		for (i = 0; i < size; i++) {
			buf_len += snprintf(buf + buf_len,
					    sizeof(buf) - buf_len, "%g+i%g%s",
					    crealf(((float complex *)addr)[i]),
					    cimagf(((float complex *)addr)[i]),
					    (i % 8 == 7) ? "\n" : " ");
			if (i % 8 == 7) {
				ptl_log("%s", buf);
				buf_len = 0;
			}
		}
		break;
	case PTL_DOUBLE_COMPLEX:
		size /= sizeof(double complex);
		for (i = 0; i < size; i++) {
			buf_len += snprintf(buf + buf_len,
					    sizeof(buf) - buf_len, "%g+i%g%s",
					    creal(((double complex *)addr)[i]),
					    cimag(((double complex *)addr)[i]),
					    (i % 8 == 7) ? "\n" : " ");
			if (i % 8 == 7) {
				ptl_log("%s", buf);
				buf_len = 0;
			}
		}
		break;
	case PTL_LONG_DOUBLE_COMPLEX:
		size /= sizeof(long double complex);
		for (i = 0; i < size; i++) {
			buf_len += snprintf(buf + buf_len,
					    sizeof(buf) - buf_len, "%Lg+i%Lg%s",
					    creall(((long double complex *)addr)[i]),
					    cimagl(((long double complex *)addr)[i]),
					    (i % 8 == 7) ? "\n" : " ");
			if (i % 8 == 7) {
				ptl_log("%s", buf);
				buf_len = 0;
			}
		}
		break;
	default:
		ptl_panic("memdump: unknown type\n");
	}

	if (buf_len > 0) {
		snprintf(buf + buf_len, sizeof(buf) - buf_len, "\n");
		ptl_log("%s", buf);
	}
}

/*
 * Print an iovec
 */
void
swptl_iovec_log(ptl_iovec_t *iov, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		ptl_log("%p..%p\n",
		    iov[i].iov_base,
		    (char *)iov[i].iov_base + iov[i].iov_len - 1);
	}
}

/*
 * Return a pointer to the given offset in a iovec
 */
void *
swptl_iovec_ptr(void *base, int iovcnt, size_t offs)
{
	ptl_iovec_t *iov = base;

	while (iovcnt != 0 && iov->iov_len <= offs) {
		offs -= iov->iov_len;
		iovcnt--;
		iov++;
	}
	return iov + offs;
}

/*
 * Return a pointer to the given offset in a iovec and the max amount
 * of contiguous memory that can be accessed
 */
void
swptl_iovseg(void *base, int iovcnt, size_t offs, size_t todo,
    void **rdata, size_t *rlen)
{
	ptl_iovec_t *iov = base;

	if (iovcnt < 0) {
		*rdata = (char *)base + offs;
		*rlen = todo;
		return;
	}
	while (1) {
		if (iovcnt == 0)
			ptl_panic("swptl_iovseg: fault\n");

		if (iov->iov_len > offs)
			break;
		offs -= iov->iov_len;
		iovcnt--;
		iov++;
	}
	*rlen = iov->iov_len - offs;
	if (*rlen > todo)
		*rlen = todo;
	*rdata = (char *)iov->iov_base + offs;
}

/*
 * Return the size in bytes of the given iovec
 */
size_t
swptl_iovlen(void *base, int iovcnt)
{
	ptl_iovec_t *iov = base;
	size_t len = 0;

	while (iovcnt > 0) {
		len += iov->iov_len;
		iovcnt--;
		iov++;
	}
	return len;
}

/*
 * copy volatile data
 */
void
swptl_volmove(struct swptl_ictx *ctx)
{
	void *idata;
	unsigned char *odata;
	size_t len, offs, todo;

	if (SWPTL_ISPUT(ctx->cmd) && SWPTL_ISVOLATILE(ctx)) {
		if (ctx->put_md->opt & PTL_IOVEC) {
			offs = ctx->put_mdoffs;
			todo = ctx->rlen;
			odata = ctx->vol_data;
			while (todo > 0) {
				swptl_iovseg(ctx->put_md->buf,
				    ctx->put_md->niov,
				    offs,
				    todo,
				    &idata, &len);
				memcpy(odata, idata, len);
				offs += len;
				todo -= len;
				odata += len;
			}
		} else {
			memcpy(ctx->vol_data,
			    ctx->put_md->buf + ctx->put_mdoffs,
			    ctx->rlen);
		}
		swptl_postack(ctx->put_md,
		    PTL_EVENT_SEND, PTL_OK, 0,
		    ctx->rlen, 0,
		    ctx->uptr);
	}
}

/*
 * Print a portals event.
 */
void
swptl_ev_log(struct swptl_ni *ni, ptl_event_t *e, const char *function_name)
{
	char msg[PTL_EV_STR_SIZE];
	int len;

	if (function_name != NULL)
		len = snprintf(msg, sizeof(msg), "%s: ", function_name);
	else
		len = 0;

	if (ptl_evtostr(ni->vc, e, msg + len) < 0)
		ptl_panic("ptl_evtostr failed");

	ptl_log("%s", msg);
}


/*
 * Return portals overflow event type corresponding to the given
 * command.
 */
int
swptl_msgunev(int cmd)
{
	switch (cmd) {
	case SWPTL_PUT:
		return PTL_EVENT_PUT_OVERFLOW;
	case SWPTL_ATOMIC:
		return PTL_EVENT_ATOMIC_OVERFLOW;
	case SWPTL_FETCH:
		return PTL_EVENT_FETCH_ATOMIC_OVERFLOW;
	case SWPTL_GET:
		return PTL_EVENT_GET_OVERFLOW;
	case SWPTL_SWAP:
		return PTL_EVENT_FETCH_ATOMIC_OVERFLOW;
	}
	ptl_panic("swptl_msgunev: unhandled cmd type\n");
	return 0; /* Suppress compilation warning */
}

/*
 * Return the size in bytes of the given atomic type.
 */
int
ptl_atsize(enum ptl_datatype atype)
{
	switch (atype) {
	case PTL_INT8_T:
	case PTL_UINT8_T:
		return 1;
	case PTL_INT16_T:
	case PTL_UINT16_T:
		return 2;
	case PTL_INT32_T:
	case PTL_UINT32_T:
		return 4;
	case PTL_INT64_T:
	case PTL_UINT64_T:
		return 8;
	case PTL_FLOAT:
		return sizeof(float);
	case PTL_DOUBLE:
		return sizeof(double);
	case PTL_LONG_DOUBLE:
		return sizeof(long double);
	case PTL_FLOAT_COMPLEX:
		return sizeof(float complex);
	case PTL_DOUBLE_COMPLEX:
		return sizeof(double complex);
	case PTL_LONG_DOUBLE_COMPLEX:
		return sizeof(long double complex);
	default:
		ptl_panic("ptl_atsize: unknown type\n");
		return 0; /* Suppress compilation warning */
	}
}

/*
 * Perform the given atomic operation
 *
 *	op - the operation
 *	type - type of the operands
 *	me - pointer to the original operand
 *	rx - pointer to the operand received from the network
 *	tx - result to send in reply on the network
 *	cst - third operand (swap only)
 *	len - size of the array of operands in bytes
 */
void
swptl_atrcv(int op, int type, void *me, void *cst, void *rx, void *tx, size_t len)
{
	int i, j, n, asize, eq, le, ge, swap;

	asize = ptl_atsize(type);
	if (len % asize != 0)
		ptl_panic("%d: atomic size not multiple of %d\n", len, asize);

	n = len / asize;

	if (swptl_verbose >= 4) {
		switch (op) {
		case PTL_SWAP:
		case PTL_CSWAP:
		case PTL_CSWAP_NE:
		case PTL_CSWAP_LE:
		case PTL_CSWAP_LT:
		case PTL_CSWAP_GE:
		case PTL_CSWAP_GT:
		case PTL_MSWAP:
			swptl_log_hex("swap cst: ", cst, asize);
		}
	}

	LOGN(3, "%s: atomic: at %p, %d operations\n", __func__, me, n);

	for (i = 0; i < n; i++) {
		if (swptl_verbose >= 4) {
			swptl_log_hex("swap rx: ", rx, asize);
			swptl_log_hex("swap me: ", me, asize);
		}
		memcpy(tx, me, asize);
		switch (op) {
		case PTL_MIN:
			switch (type) {
			case PTL_INT8_T:
				if (*(int8_t *)me > *(int8_t *)rx)
					*(int8_t *)me = *(int8_t *)rx;
				break;
			case PTL_UINT8_T:
				if (*(uint8_t *)me > *(uint8_t *)rx)
					*(uint8_t *)me = *(uint8_t *)rx;
				break;
			case PTL_INT16_T:
				if (*(int16_t *)me > *(int16_t *)rx)
					*(int16_t *)me = *(int16_t *)rx;
				break;
			case PTL_UINT16_T:
				if (*(uint16_t *)me > *(uint16_t *)rx)
					*(uint16_t *)me = *(uint16_t *)rx;
				break;
			case PTL_INT32_T:
				if (*(int32_t *)me > *(int32_t *)rx)
					*(int32_t *)me = *(int32_t *)rx;
				break;
			case PTL_UINT32_T:
				if (*(uint32_t *)me > *(uint32_t *)rx)
					*(uint32_t *)me = *(uint32_t *)rx;
				break;
			case PTL_INT64_T:
				if (*(int64_t *)me > *(int64_t *)rx)
					*(int64_t *)me = *(int64_t *)rx;
				break;
			case PTL_UINT64_T:
				if (*(uint64_t *)me > *(uint64_t *)rx)
					*(uint64_t *)me = *(uint64_t *)rx;
				break;
			case PTL_FLOAT:
				if (*(float *)me > *(float *)rx)
					*(float *)me = *(float *)rx;
				break;
			case PTL_DOUBLE:
				if (*(double *)me > *(double *)rx)
					*(double *)me = *(double *)rx;
				break;
			case PTL_LONG_DOUBLE:
				if (*(long double *)me > *(long double *)rx)
					*(long double *)me = *(long double *)rx;
				break;
			default:
				ptl_panic("unhandled atomic type\n");
			}
			break;
		case PTL_MAX:
			switch (type) {
			case PTL_INT8_T:
				if (*(int8_t *)me < *(int8_t *)rx)
					*(int8_t *)me = *(int8_t *)rx;
				break;
			case PTL_UINT8_T:
				if (*(uint8_t *)me < *(uint8_t *)rx)
					*(uint8_t *)me = *(uint8_t *)rx;
				break;
			case PTL_INT16_T:
				if (*(int16_t *)me < *(int16_t *)rx)
					*(int16_t *)me = *(int16_t *)rx;
				break;
			case PTL_UINT16_T:
				if (*(uint16_t *)me < *(uint16_t *)rx)
					*(uint16_t *)me = *(uint16_t *)rx;
				break;
			case PTL_INT32_T:
				if (*(int32_t *)me < *(int32_t *)rx)
					*(int32_t *)me = *(int32_t *)rx;
				break;
			case PTL_UINT32_T:
				if (*(uint32_t *)me < *(uint32_t *)rx)
					*(uint32_t *)me = *(uint32_t *)rx;
				break;
			case PTL_INT64_T:
				if (*(int64_t *)me < *(int64_t *)rx)
					*(int64_t *)me = *(int64_t *)rx;
				break;
			case PTL_UINT64_T:
				if (*(uint64_t *)me < *(uint64_t *)rx)
					*(uint64_t *)me = *(uint64_t *)rx;
				break;
			case PTL_FLOAT:
				if (*(float *)me < *(float *)rx)
					*(float *)me = *(float *)rx;
				break;
			case PTL_DOUBLE:
				if (*(double *)me < *(double *)rx)
					*(double *)me = *(double *)rx;
				break;
			case PTL_LONG_DOUBLE:
				if (*(long double *)me <= *(long double *)rx)
					*(long double *)me = *(long double *)rx;
				break;
			default:
				ptl_panic("unhandled atomic type\n");
			}
			break;
		case PTL_SUM:
			switch (type) {
			case PTL_INT8_T:
			case PTL_UINT8_T:
				*(int8_t *)me += *(int8_t *)rx;
				break;
			case PTL_INT16_T:
			case PTL_UINT16_T:
				*(int16_t *)me += *(int16_t *)rx;
				break;
			case PTL_INT32_T:
			case PTL_UINT32_T:
				*(int32_t *)me += *(int32_t *)rx;
				break;
			case PTL_INT64_T:
			case PTL_UINT64_T:
				*(int64_t *)me += *(int64_t *)rx;
				break;
			case PTL_FLOAT:
				*(float *)me += *(float *)rx;
				break;
			case PTL_FLOAT_COMPLEX:
				*(float complex *)me += *(float complex *)rx;
				break;
			case PTL_DOUBLE:
				*(double *)me += *(double *)rx;
				break;
			case PTL_DOUBLE_COMPLEX:
				*(double complex *)me += *(double complex *)rx;
				break;
			case PTL_LONG_DOUBLE:
				*(long double *)me += *(long double *)rx;
				break;
			case PTL_LONG_DOUBLE_COMPLEX:
				*(long double complex *)me += *(long double complex *)rx;
				break;
			default:
				ptl_panic("unhandled atomic type\n");
			}
			break;
		case PTL_PROD:
			switch (type) {
			case PTL_INT8_T:
				*(int8_t *)me *= *(int8_t *)rx;
				break;
			case PTL_UINT8_T:
				*(uint8_t *)me *= *(uint8_t *)rx;
				break;
			case PTL_INT16_T:
				*(int16_t *)me *= *(int16_t *)rx;
				break;
			case PTL_UINT16_T:
				*(uint16_t *)me *= *(uint16_t *)rx;
				break;
			case PTL_INT32_T:
				*(int32_t *)me *= *(int32_t *)rx;
				break;
			case PTL_UINT32_T:
				*(uint32_t *)me *= *(uint32_t *)rx;
				break;
			case PTL_INT64_T:
				*(int64_t *)me *= *(int64_t *)rx;
				break;
			case PTL_UINT64_T:
				*(uint64_t *)me *= *(uint64_t *)rx;
				break;
			case PTL_FLOAT:
				*(float *)me *= *(float *)rx;
				break;
			case PTL_FLOAT_COMPLEX:
				*(float complex *)me *= *(float complex *)rx;
				break;
			case PTL_DOUBLE:
				*(double *)me *= *(double *)rx;
				break;
			case PTL_DOUBLE_COMPLEX:
				*(double complex *)me *= *(double complex *)rx;
				break;
			case PTL_LONG_DOUBLE:
				*(long double *)me *= *(long double *)rx;
				break;
			case PTL_LONG_DOUBLE_COMPLEX:
				*(long double complex *)me *= *(long double complex *)rx;
				break;
			default:
				ptl_panic("unhandled atomic type\n");
			}
			break;
		case PTL_BOR:
			switch (type) {
			case PTL_INT8_T:
			case PTL_UINT8_T:
				*(int8_t *)me = *(int8_t *)me | *(int8_t *)rx;
				break;
			case PTL_INT16_T:
			case PTL_UINT16_T:
				*(int16_t *)me = *(int16_t *)me | *(int16_t *)rx;
				break;
			case PTL_INT32_T:
			case PTL_UINT32_T:
				*(int32_t *)me = *(int32_t *)me | *(int32_t *)rx;
				break;
			case PTL_INT64_T:
			case PTL_UINT64_T:
				*(int64_t *)me = *(int64_t *)me | *(int64_t *)rx;
				break;
			default:
				ptl_panic("unhandled atomic type\n");
			}
			break;
		case PTL_BAND:
			switch (type) {
			case PTL_INT8_T:
			case PTL_UINT8_T:
				*(int8_t *)me = *(int8_t *)me & *(int8_t *)rx;
				break;
			case PTL_INT16_T:
			case PTL_UINT16_T:
				*(int16_t *)me = *(int16_t *)me & *(int16_t *)rx;
				break;
			case PTL_INT32_T:
			case PTL_UINT32_T:
				*(int32_t *)me = *(int32_t *)me & *(int32_t *)rx;
				break;
			case PTL_INT64_T:
			case PTL_UINT64_T:
				*(int64_t *)me = *(int64_t *)me & *(int64_t *)rx;
				break;
			default:
				ptl_panic("unhandled atomic type\n");
			}
			break;
		case PTL_BXOR:
			switch (type) {
			case PTL_INT8_T:
			case PTL_UINT8_T:
				*(int8_t *)me = *(int8_t *)me ^ *(int8_t *)rx;
				break;
			case PTL_INT16_T:
			case PTL_UINT16_T:
				*(int16_t *)me = *(int16_t *)me ^ *(int16_t *)rx;
				break;
			case PTL_INT32_T:
			case PTL_UINT32_T:
				*(int32_t *)me = *(int32_t *)me ^ *(int32_t *)rx;
				break;
			case PTL_INT64_T:
			case PTL_UINT64_T:
				*(int64_t *)me = *(int64_t *)me ^ *(int64_t *)rx;
				break;
			default:
				ptl_panic("unhandled atomic type\n");
			}
			break;
		case PTL_LOR:
			switch (type) {
			case PTL_INT8_T:
			case PTL_UINT8_T:
				*(int8_t *)me = *(int8_t *)me || *(int8_t *)rx;
				break;
			case PTL_INT16_T:
			case PTL_UINT16_T:
				*(int16_t *)me = *(int16_t *)me || *(int16_t *)rx;
				break;
			case PTL_INT32_T:
			case PTL_UINT32_T:
				*(int32_t *)me = *(int32_t *)me || *(int32_t *)rx;
				break;
			case PTL_INT64_T:
			case PTL_UINT64_T:
				*(int64_t *)me = *(int64_t *)me || *(int64_t *)rx;
				break;
			default:
				ptl_panic("unhandled atomic type\n");
			}
			break;
		case PTL_LAND:
			switch (type) {
			case PTL_INT8_T:
			case PTL_UINT8_T:
				*(int8_t *)me = *(int8_t *)me && *(int8_t *)rx;
				break;
			case PTL_INT16_T:
			case PTL_UINT16_T:
				*(int16_t *)me = *(int16_t *)me && *(int16_t *)rx;
				break;
			case PTL_INT32_T:
			case PTL_UINT32_T:
				*(int32_t *)me = *(int32_t *)me && *(int32_t *)rx;
				break;
			case PTL_INT64_T:
			case PTL_UINT64_T:
				*(int64_t *)me = *(int64_t *)me && *(int64_t *)rx;
				break;
			default:
				ptl_panic("unhandled atomic type\n");
			}
			break;
		case PTL_LXOR:
			switch (type) {
			case PTL_INT8_T:
			case PTL_UINT8_T:
				*(int8_t *)me = (!!*(int8_t *)me) ^ (!!*(int8_t *)rx);
				break;
			case PTL_INT16_T:
			case PTL_UINT16_T:
				*(int16_t *)me = (!!*(int16_t *)me) ^ (!!*(int16_t *)rx);
				break;
			case PTL_INT32_T:
			case PTL_UINT32_T:
				*(int32_t *)me = (!!*(int32_t *)me) ^ (!!*(int32_t *)rx);
				break;
			case PTL_INT64_T:
			case PTL_UINT64_T:
				*(int64_t *)me = (!!*(int64_t *)me) ^ (!!*(int64_t *)rx);
				break;
			default:
				ptl_panic("unhandled atomic type\n");
			}
			break;
		case PTL_SWAP:
		case PTL_CSWAP:
		case PTL_CSWAP_NE:
		case PTL_CSWAP_LE:
		case PTL_CSWAP_LT:
		case PTL_CSWAP_GE:
		case PTL_CSWAP_GT:
			if (n != 1)
				ptl_panic("only one item allowed for swap\n");

			switch (type) {
			case PTL_INT8_T:
				eq = *(int8_t *)me == *(int8_t *)cst;
				le = *(int8_t *)me >= *(int8_t *)cst;
				ge = *(int8_t *)me <= *(int8_t *)cst;
				break;
			case PTL_UINT8_T:
				eq = *(uint8_t *)me == *(uint8_t *)cst;
				le = *(uint8_t *)me >= *(uint8_t *)cst;
				ge = *(uint8_t *)me <= *(uint8_t *)cst;
				break;
			case PTL_INT16_T:
				eq = *(int16_t *)me == *(int16_t *)cst;
				le = *(int16_t *)me >= *(int16_t *)cst;
				ge = *(int16_t *)me <= *(int16_t *)cst;
				break;
			case PTL_UINT16_T:
				eq = *(uint16_t *)me == *(uint16_t *)cst;
				le = *(uint16_t *)me >= *(uint16_t *)cst;
				ge = *(uint16_t *)me <= *(uint16_t *)cst;
				break;
			case PTL_INT32_T:
				eq = *(int32_t *)me == *(int32_t *)cst;
				le = *(int32_t *)me >= *(int32_t *)cst;
				ge = *(int32_t *)me <= *(int32_t *)cst;
				break;
			case PTL_UINT32_T:
				eq = *(uint32_t *)me == *(uint32_t *)cst;
				le = *(uint32_t *)me >= *(uint32_t *)cst;
				ge = *(uint32_t *)me <= *(uint32_t *)cst;
				break;
			case PTL_INT64_T:
				eq = *(int64_t *)me == *(int64_t *)cst;
				le = *(int64_t *)me >= *(int64_t *)cst;
				ge = *(int64_t *)me <= *(int64_t *)cst;
				break;
			case PTL_UINT64_T:
				eq = *(uint64_t *)me == *(uint64_t *)cst;
				le = *(uint64_t *)me >= *(uint64_t *)cst;
				ge = *(uint64_t *)me <= *(uint64_t *)cst;
				break;
			case PTL_FLOAT:
				eq = *(float *)me == *(float *)cst;
				le = *(float *)me >= *(float *)cst;
				ge = *(float *)me <= *(float *)cst;
				break;
			case PTL_DOUBLE:
				eq = *(double *)me == *(double *)cst;
				le = *(double *)me >= *(double *)cst;
				ge = *(double *)me <= *(double *)cst;
				break;
			case PTL_LONG_DOUBLE:
				eq = *(long double *)me == *(long double *)cst;
				le = *(long double *)me >= *(long double *)cst;
				ge = *(long double *)me <= *(long double *)cst;
				break;
			case PTL_FLOAT_COMPLEX:
				eq = *(float complex *)me == *(float complex *)cst;
				le = 0;
				ge = 0;
				break;
			case PTL_DOUBLE_COMPLEX:
				eq = *(double complex *)me == *(double complex *)cst;
				le = 0;
				ge = 0;
				break;
			case PTL_LONG_DOUBLE_COMPLEX:
				eq = *(long double complex *)me == *(long double complex *)cst;
				le = 0;
				ge = 0;
				break;
			default:
				ptl_panic("unhandled atomic type\n");
				return; /* Fix compilation warning */
			}
			switch (op) {
			case PTL_CSWAP:
				swap = eq;
				break;
			case PTL_CSWAP_NE:
				swap = !eq;
				break;
			case PTL_CSWAP_LE:
				swap = le;
				break;
			case PTL_CSWAP_LT:
				swap = le && !eq;
				break;
			case PTL_CSWAP_GE:
				swap = ge;
				break;
			case PTL_CSWAP_GT:
				swap = ge && !eq;
				break;
			case PTL_SWAP:
				swap = 1;
				break;
			default:
				swap = 0;
			}
			if (swap)
				memcpy(me, rx, asize);
			break;
		case PTL_MSWAP:
			for (j = 0; j < asize; j++) {
				((char *)me)[j] &= ~((char *)cst)[j];
				((char *)me)[j] |= ((char *)rx)[j] & ((char *)cst)[j];
			}
			break;
		default:
			ptl_panic("%x: unhandled atomic op\n", op);
		}
		if (swptl_verbose >= 4) {
			swptl_log_hex("swap tx: ", tx, asize);
			swptl_log_hex("swap me: ", me, asize);
		}
		rx = (unsigned char *)rx + asize;
		tx = (unsigned char *)tx + asize;
		me = (unsigned char *)me + asize;
	}
}

static void
swptl_ctx_add(struct swptl_sodata **list, struct swptl_sodata *sodata)
{
	sodata->ni_next = *list;
	sodata->ni_prev = list;
	if (*list)
		(*list)->ni_prev = &sodata->ni_next;
	*list = sodata;
}

static void
swptl_ctx_rm(struct swptl_sodata **list, struct swptl_sodata *sodata)
{
	*sodata->ni_prev = sodata->ni_next;
	if (sodata->ni_next)
		sodata->ni_next->ni_prev = sodata->ni_prev;
}

/*
 * Put the given event in the event queue.
 */
void
swptl_eq_putev(struct swptl_eq *eq,
    ptl_event_kind_t type,
    ptl_ni_fail_t ni_fail_type,
    void *start,
    void *user_ptr,
    ptl_hdr_data_t hdr_data,
    ptl_match_bits_t match_bits,
    ptl_size_t rlength,
    ptl_size_t mlength,
    ptl_size_t remote_offset,
    ptl_uid_t uid,
    int nid,
    int pid,
    int rank,
    ptl_list_t ptl_list,
    ptl_pt_index_t pt_index,
    ptl_op_t atomic_operation,
    ptl_datatype_t atomic_type)
{
	struct swptl_ev *e;

	if (pool_isempty(&eq->ev_pool)) {
		eq->dropped = 1;
		return;
	}
	e = pool_get(&eq->ev_pool);
	e->ev.start = start;
	e->ev.user_ptr = user_ptr;
	e->ev.hdr_data = hdr_data;
	e->ev.match_bits = match_bits;
	e->ev.rlength = rlength;
	e->ev.mlength = mlength;
	e->ev.remote_offset = remote_offset;
	e->ev.uid = uid;
	if (SWPTL_ISCOMM(type) || SWPTL_ISOVER(type) || type == PTL_EVENT_SEARCH) {
		if (SWPTL_ISPHYSICAL(eq->ni->vc)) {
			e->ev.initiator.phys.nid = nid;
			e->ev.initiator.phys.pid = pid;
		} else
			e->ev.initiator.rank = rank;
	} else
		memset(&e->ev.initiator, 0, sizeof(e->ev.initiator));
	e->ev.type = type;
	e->ev.ptl_list = ptl_list;
	e->ev.pt_index = pt_index;
	e->ev.ni_fail_type = ni_fail_type;
	e->ev.atomic_operation = atomic_operation;
	e->ev.atomic_type = atomic_type;

	/* link to list */
	e->next = NULL;
	*eq->ev_tail = e;
	eq->ev_tail = &e->next;
}

/*
 * Retrieve the next event from the queue: copy it's contents to the
 * given location and put in the pool the consumed structure.
 */
int
swptl_eq_getev(struct swptl_eq *eq, struct ptl_event *ev)
{
	struct swptl_ev *e;

	e = eq->ev_head;
	if (e == NULL)
		return PTL_EQ_EMPTY;
	eq->ev_head = e->next;
	if (eq->ev_head == NULL)
		eq->ev_tail = &eq->ev_head;
	if (ev)
		*ev = e->ev;
	pool_put(&eq->ev_pool, e);
	if (eq->dropped) {
		eq->dropped = 0;
		return PTL_EQ_DROPPED;
	}
	return PTL_OK;
}

/*
 * Initialize the given event queue.
 */
void
swptl_eq_init(struct swptl_eq *eq, struct swptl_ni *ni, size_t len)
{
	struct swptl_eq **pnext;

	pool_init(&eq->ev_pool, "ev_pool", sizeof(struct swptl_ev), len);

	eq->ev_head = NULL;
	eq->ev_tail = &eq->ev_head;
	eq->ni = ni;
	eq->dropped = 0;

	/* append to the list */
	pnext = &ni->eq_list;
	while (*pnext != NULL)
		pnext = &(*pnext)->next;
	eq->next = NULL;
	*pnext = eq;
}

/*
 * Free the given event queue. Must be empty
 */
int
swptl_eq_done(struct swptl_eq *eq)
{
	struct swptl_eq **pnext;
	struct ptl_event ev;

	if (eq->ev_head) {
		while (swptl_eq_getev(eq, &ev) == PTL_OK)
			if (swptl_verbose >= 2)
				swptl_ev_log(eq->ni, &ev, __func__);
	}
	pool_done(&eq->ev_pool);

	pnext = &eq->ni->eq_list;
	while (*pnext != eq)
		pnext = &(*pnext)->next;
	*pnext = eq->next;

	return 1;
}

/*
 * Initialize the given counter (aka lightweight event counter), both
 * of its counters to zero.
 */
void
swptl_ct_init(struct swptl_ct *ct, struct swptl_ni *ni)
{
	struct swptl_ct **pnext;

	ct->val.success = 0;
	ct->val.failure = 0;
	ct->ni = ni;
	ct->trig = NULL;

	/* append to the list */
	pnext = &ni->ct_list;
	while (*pnext != NULL)
		pnext = &(*pnext)->next;
	ct->next = NULL;
	*pnext = ct;
}

/*
 * Free the given CT (aka lightweight event counter)
 */
void
swptl_ct_done(struct swptl_ct *ct)
{
	struct swptl_ct **pnext;

	pnext = &ct->ni->ct_list;
	while (*pnext != ct)
		pnext = &(*pnext)->next;
	*pnext = ct->next;
}

/*
 * Set a counter to the given value (both success and failure) and
 * mark as pending any triggered operation whose threshold is below
 * it.
 */
void
swptl_ct_trigval(struct swptl_ct *ct, ptl_size_t success, ptl_size_t failure)
{
	struct swptl_ni *ni = ct->ni;
	struct swptl_trig *trig;

	ct->val.success = success;
	ct->val.failure = failure;
	LOGN(2, "%s: ct %p: incr -> %zu\n",
		__func__, ct, ct->val.success);
	while ((trig = ct->trig) != NULL &&
	    ct->val.success >= trig->thres) {
		LOGN(2, "%s: running triggered op\n", __func__);
		ct->trig = trig->next;
		trig->next = ni->trig_pending;
		ni->trig_pending = trig;
	}
}

/*
 * Post a SEND, ACK or REPLY event for the given MD. Depending on the
 * MD options, an event may be posted in the MD's EQ, its CT may be
 * incremented or this may end-up as a no-op.
 */
void
swptl_postack(struct swptl_md *md, int type, int fail, int list,
    uint32_t mlen, uint64_t roffs, void *uptr)
{
	union ptl_process dummy;

	LOGN(2, "%s: %p: posting: type = %d, fail = %d\n", __func__, uptr, type, fail);
	memset(&dummy, 0, sizeof(union ptl_process));

	if ((fail == PTL_OK && (md->opt & PTL_MD_EVENT_SUCCESS_DISABLE)) ||
	    (type == PTL_EVENT_SEND && (md->opt & PTL_MD_EVENT_SEND_DISABLE))) {
		LOGN(2, "%s: not posted, disabled by mdopt = 0x%x\n", __func__, md->opt);
	} else if (!md->eq) {
		LOGN(2, "%s: not posted, no eq\n", __func__);
	} else {
		swptl_eq_putev(md->eq,
		    type,
		    fail,
		    0,	/* start */
		    uptr,
		    0,	/* hdr_data */
		    0,	/* match_bits */
		    0,	/* rlength */
		    mlen,
		    roffs,
		    0,	/* uid */
		    0,	/* nid */
		    0,	/* pid */
		    0,	/* rank */
		    list,
		    0,	/* pt_index */
		    0,	/* atomic_operation */
		    0); /* atomic_type */
	}
	if (((md->opt & PTL_MD_EVENT_CT_ACK) && type == PTL_EVENT_ACK) ||
	    ((md->opt & PTL_MD_EVENT_CT_SEND) && type == PTL_EVENT_SEND) ||
	    ((md->opt & PTL_MD_EVENT_CT_REPLY) && type == PTL_EVENT_REPLY)) {
		swptl_ct_cmd(SWPTL_CTINC, md->ct,
		    fail == PTL_OK, fail != PTL_OK);
	}
}

/*
 * Post a comm, an overflow or a flowcontrol event (PUT, GET, ATOMIC,
 * FETCH_ATOMIC and SWAP and their OVERFLOW equivalents) for the given
 * PTE. Depending on the PTE and ME options, an event may be posted in
 * the PTE's EQ, the ME's CT may be incremented or this may end-up as
 * a no-op.
 */
void
swptl_postcomm(struct swptl_pte *pte,
    int meopt,
    struct swptl_ct *ct,
    int type,
    int fail,
    int aop,
    int atype,
    int nid,
    int pid,
    int rank,
    size_t roffs,
    unsigned char *start,
    size_t mlen,
    size_t rlen,
    void *uptr,
    uint32_t uid,
    unsigned long long hdr,
    unsigned long long bits)
{
	LOGN(2, "%s: %p: posting comm event\n", __func__, uptr);

	if ((SWPTL_ISOVER(type) && (meopt & PTL_ME_EVENT_OVER_DISABLE)) ||
	    (SWPTL_ISCOMM(type) && (meopt & PTL_ME_EVENT_COMM_DISABLE)) ||
	    (SWPTL_ISFCTRL(type) && (meopt & PTL_ME_EVENT_FLOWCTRL_DISABLE)) ||
	    (fail == PTL_NI_OK && (meopt & PTL_ME_EVENT_SUCCESS_DISABLE))) {
		LOGN(2, "%s: not posted, disabled by meopt = 0x%x\n", __func__, meopt);
	} else if (!pte->eq) {
		LOGN(2, "%s: not posted, no eq\n", __func__);
	} else {
		swptl_eq_putev(pte->eq,
		    type,
		    fail,
		    start,
		    uptr,
		    hdr,
		    bits,
		    rlen,
		    mlen,
		    roffs,
		    uid,
		    nid,
		    pid,
		    rank,
		    -1,	/* list */
		    pte->index,
		    aop,
		    atype);
	}
	if ((SWPTL_ISCOMM(type) && (meopt & PTL_ME_EVENT_CT_COMM)) ||
	    (SWPTL_ISOVER(type) && (meopt & PTL_ME_EVENT_CT_OVERFLOW)))
		swptl_ct_cmd(SWPTL_CTINC, ct, fail == PTL_OK, fail != PTL_OK);
}

/*
 * Run all pending triggered operations
 */
void
swptl_trig(struct swptl_ni *ni)
{
	struct swptl_sodata *sodata;
	struct swptl_trig *trig, **ptrig;
	struct swptl_ct *ct;
	struct swptl_ictx *ctx;

	ptrig = &ni->trig_pending;
	while ((trig = *ptrig) != NULL) {
		if (trig->scope == SWPTL_TRIG_CTOP) {
			*ptrig = trig->next;
			ct = trig->u.ctop.ct;
			switch (trig->u.ctop.op) {
			case SWPTL_CTSET:
				LOGN(2, "%s: trig ct %p set\n", __func__, trig);
				swptl_ct_trigval(ct,
				    trig->u.ctop.val.success,
				    trig->u.ctop.val.failure);
				break;
			case SWPTL_CTINC:
				LOGN(2, "%s: trig ct %p inc\n", __func__, trig);
				swptl_ct_trigval(ct,
				    ct->val.success + trig->u.ctop.val.success,
				    ct->val.failure + trig->u.ctop.val.failure);
				break;
			default:
				ptl_panic("bad trig ct operation\n");
			}
			pool_put(&ni->trig_pool, trig);
		} else if (trig->scope == SWPTL_TRIG_TX) {
			LOGN(2, "%s: trying trig to %d, %d\n",
				__func__, trig->u.tx.nid, trig->u.tx.pid);
			LOGN(2, "%s: starting trig xfer\n", __func__);
			while (pool_isempty(&ni->ictx_pool)) {
				swptl_dev_progress(ni->dev, 1);
			}
			sodata = pool_get(&ni->ictx_pool);
			sodata->init = 1;
			sodata->u.ictx = trig->u.tx.ictx;
			sodata->conn = bximsg_getconn(ni->dev->iface,
						      trig->u.tx.nid,
						      trig->u.tx.pid,
						      ni->vc);
			ctx = &sodata->u.ictx;
 			swptl_volmove(ctx);
			sodata->hdrsize = swptl_qhdr_getsize(ctx->cmd);
			bximsg_enqueue(ni->dev->iface, sodata,
				       SWPTL_ISPUT(sodata->u.ictx.cmd) ?
				       sodata->u.ictx.rlen : 0);
			LOGN(2, "%s: %u: triggered %zd byte %s query (%d, %d) "
				"-> (%d, %d), ictx = %zu\n", __func__,
			     ctx->serial,
			     ctx->rlen,
			     swptl_cmdname[ctx->cmd],
			     ni->dev->nid,
			     ni->dev->pid,
			     sodata->conn->nid,
			     sodata->conn->pid,
			     sodata - (struct swptl_sodata *)ni->ictx_pool.data);
			*ptrig = trig->next;
			pool_put(&ni->trig_pool, trig);
			swptl_ctx_add(&ni->txops, sodata);
		} else {
			ptl_panic("bad triggered op\n");
		}
	}
}

/*
 * Return the current CT value
 */
void
swptl_ct_get(struct swptl_ct *ct, struct ptl_ct_event *rval)
{
	*rval = ct->val;
}

/*
 * Allocate a new triggered operation structure and append it to the
 * CT's list at the location corresponding to the given threshold.
 */
struct swptl_trig *
swptl_trigadd(struct swptl_ct *ct, ptl_size_t thres)
{
	struct swptl_ni *ni;
	struct swptl_trig *trig, **i;
	ssize_t diff;

	if (ct == NULL) {
		LOG("%s: trig op with no ct\n", __func__);
		return NULL;
	}
	ni = ct->ni;
	if (pool_isempty(&ni->trig_pool))
		return NULL;
	trig = pool_get(&ni->trig_pool);
	if (ct->val.success >= thres) {
		trig->next = ni->trig_pending;
		ni->trig_pending = trig;
		LOGN(2, "%s: ct %p: pending trig, %zu >= %zu\n",
			__func__, ct, ct->val.success, thres);
	} else {
		for (i = &ct->trig; *i != NULL; i = &(*i)->next) {
			diff = (*i)->thres - thres;
			if (diff > 0) {
				break;
			}
		}
		trig->next = *i;
		*i = trig;
		LOGN(2, "%s: ct %p: scheduled trig, %zu < %zu\n",
			__func__, ct, ct->val.success, thres);
	}
	trig->thres = thres;
	return trig;
}

/*
 * Delete all triggered operations associated to the given counter
 */
void
swptl_trigdel(struct swptl_ct *ct)
{
	struct swptl_trig *trig;
	struct swptl_ictx *ctx;

	while ((trig = ct->trig) != NULL) {
		if (trig->scope == SWPTL_TRIG_TX) {
			ctx = &trig->u.tx.ictx;
			if (SWPTL_ISPUT(ctx->cmd))
				ctx->put_md->refs--;
			if (SWPTL_ISGET(ctx->cmd))
				ctx->get_md->refs--;
		}
		ct->trig = trig->next;
		pool_put(&ct->ni->trig_pool, trig);
	}
}

/*
 * Run the given CT operation (either set or increment CT value). If
 * the trig_ct argument is NULL, it's run immediately, else it's
 * appended to the CT list of triggered operations.
 */
int
swptl_ct_cmd(int op, struct swptl_ct *ct, ptl_size_t success, ptl_size_t failure)
{
	if (op == SWPTL_CTINC) {
		success += ct->val.success;
		failure += ct->val.failure;
	}

	/* set CT value (may marks expired triggered as pending) */
	swptl_ct_trigval(ct, success, failure);

	/* run pending triggered, if any */
	swptl_trig(ct->ni);

	return 1;
}

/*
 * create a new MD
 */
void
swptl_md_init(struct swptl_md *md, struct swptl_ni *ni,
    void *buf,
    size_t len,
    struct swptl_eq *eq,
    struct swptl_ct *ct,
    int opt)
{
	struct swptl_md **pnext;

	md->ni = ni;
	md->buf = buf;
	md->len = len;
	if (opt & PTL_IOVEC) {
		md->niov = md->len;
		md->len = swptl_iovlen(buf, len);
	} else {
		md->len = len;
		md->niov = -1;
	}

	md->eq = eq;
	md->ct = ct;
	md->opt = opt;
	md->refs = 0;

	/* append to the list */
	pnext = &ni->md_list;
	while (*pnext != NULL)
		pnext = &(*pnext)->next;
	md->next = NULL;
	*pnext = md;
}

/*
 * Destroy the given MD. If it's used by the initiator commands in
 * progress, block until they complete.
 */
void
swptl_md_done(struct swptl_md *md)
{
	struct swptl_md **pnext;

	while (md->refs > 0) {
		LOGN(2, "%s: refs = %d\n", __func__, md->refs);
		swptl_dev_progress(md->ni->dev, 1);
	}

	pnext = &md->ni->md_list;
	while (*pnext != md)
		pnext = &(*pnext)->next;
	*pnext = md->next;
}

/*
 * create a new PTE
 */
int
swptl_pte_init(struct swptl_pte *pte, struct swptl_ni *ni,
    int index, int opt, struct swptl_eq *eq)
{
	LOGN(2, "%s: index %d, %p\n", __func__, index, ni->pte[index]);

	if (index == PTL_PT_ANY) {
		index = 0;
		while (1) {
			if (index == SWPTL_NPTE) {
				return -1;
			}
			if (ni->pte[index] == NULL)
				break;
			index++;
		}
	} else {
		if (ni->pte[index] != NULL) {
			LOGN(2, "%s: can't reuse of ptes index %d, %p\n",
				__func__, index, ni->pte[index]);
			return -1;
		}
	}
	pte->eq = eq;
	pte->opt = opt;
	pte->prio.head = NULL;
	pte->prio.tail = &pte->prio.head;
	pte->over.head = NULL;
	pte->over.tail = &pte->over.head;
	pte->unex.head = NULL;
	pte->unex.tail = &pte->unex.head;
	pte->index = index;
	pte->ni = ni;
	pte->enabled = opt & PTL_PT_ALLOC_DISABLED ? 0 : 1;
	if ((opt & PTL_PT_FLOWCTRL) && eq) {
		if (pool_isempty(&eq->ev_pool)) {
			LOGN(2, "%s: %d: out of eq space\n", __func__, index);
			return -1;
		}
		pte->ev = pool_get(&eq->ev_pool);
	} else
		pte->ev = NULL;
	ni->pte[index] = pte;
	return index;
}

/*
 * Destroy the given PTE. Return true on success or false if there are
 * MEs or unexpected headers associated to it.
 */
int
swptl_pte_done(struct swptl_pte *pte)
{
	struct swptl_ni *ni = pte->ni;

	if (pte->ev) {
		pool_put(&pte->eq->ev_pool, pte->ev);
		pte->ev = NULL;
	}
	
	if (pte->prio.head || pte->over.head || pte->unex.head) {
		LOGN(1, "%s: index %d busy\n", __func__, pte->index);
		return 0;
	}
	LOGN(2, "%s: index %d\n", __func__, pte->index);
	ni->pte[pte->index] = NULL;
	return 1;
}

/*
 * Return a pointer to the given offset of a ME, including if it's an iovec.
 */
void *
swptl_me_ptr(struct swptl_me *me, size_t offs)
{
	return (me->opt & PTL_IOVEC) ?
	    swptl_iovec_ptr(me->buf, me->niov, offs) :
	    (char *)me->buf + offs;
}

/*
 * Post a LINK, AUTO_UNLINK or AUTO_FREE event for the given
 * PTE. Depending on the PTE and ME options, an event may be posted in
 * the PTE's EQ or discarded.
 */
void
swptl_postlink(struct swptl_pte *pte, struct swptl_me *me, int type, int fail)
{
	LOGN(2, "%s: %p: posting link event\n", __func__, me->uptr);

	if ((SWPTL_ISUNLINK(type) && (me->opt & PTL_ME_EVENT_UNLINK_DISABLE)) ||
	    (SWPTL_ISLINK(type) && (me->opt & PTL_ME_EVENT_LINK_DISABLE))) {
		LOGN(2, "%s: not posted, disabled meopt = 0x%x\n", __func__, me->opt);
	} else {
		if (pte->eq) {
			swptl_eq_putev(pte->eq,
			    type,
			    fail,
			    0,	/* start */
			    me->uptr,
			    0,	/* hdr_data */
			    0,	/* match_bits */
			    0,	/* rlength */
			    0,
			    0,
			    0,	/* uid */
			    0,	/* nid */
			    0,	/* pid */
			    0,	/* rank */
			    0,	/* list */
			    pte->index,
			    0,	/* atomic_operation */
			    0); /* atomic_type */
		} else
			LOGN(2, "%s: not posted, no eq\n", __func__);
	}
}

/*
 * Check ME refs count and generate a AUTO_FREE event if it drops to
 * zero, the ME is not freed, see comments in swptl_me_rm()
 */
void
swptl_meunref(struct swptl_ni *ni, struct swptl_me *me, int postev)
{
	if (--me->refs > 0)
		return;

	if (postev)
		swptl_postlink(me->pte, me, PTL_EVENT_AUTO_FREE, PTL_NI_OK);

	LOGN(2, "%s: %p: freed\n", __func__, me);
	pool_put(&ni->me_pool, me);
}

/*
 * Unlink the given ME from the prio/over list of the PTE where it's
 * attached.
 *
 * A non-persistent ME may be already auto-unlinked, in which case the
 * ME may be reused.
 */
int
swptl_me_rm(struct swptl_me *me)
{
	struct swptl_mequeue *q;
	struct swptl_me **pme;

	LOGN(2, "%s: %p, list = %d, refs = %d\n",
		__func__, me, me->list, me->refs);

	if (me->list != PTL_PRIORITY_LIST && me->list != PTL_OVERFLOW_LIST) {
		LOG("%s: %p: not on list, list = 0x%x\n", __func__, me, me->list);
		return PTL_ARG_INVALID;
	}

	if (me->refs > 1) {
		swptl_dev_progress(me->pte->ni->dev, 1);
	}

	if (me->refs > 1) {
		LOG("%s: inuse, refs = %d\n", __func__, me->refs);
		return PTL_IN_USE;
	}
	q = (me->list == PTL_PRIORITY_LIST) ? &me->pte->prio : &me->pte->over;
	pme = &q->head;
	for (;;) {
		if (*pme == NULL) {
			LOG("%s: me not found\n", __func__);
			return PTL_ARG_INVALID;
		}
		if (*pme == me) {
			*pme = me->next;
			if (*pme == NULL)
				q->tail = pme;
			break;
		}
		pme = &(*pme)->next;
	}
	me->list = -1;
	swptl_meunref(me->pte->ni, me, 0);
	LOGN(2, "%s: %p: done\n", __func__, me);
	return PTL_OK;
}

struct swptl_dev *swptl_dev_new(int nic_iface, int pid)
{
	struct swptl_dev *dev, **pdev;
	int cnt;

	dev = xmalloc(sizeof(struct swptl_dev), "dev");

	dev->iface = bximsg_init(dev, &swptl_bximsg_ops,
				 nic_iface, pid, &dev->nid, &dev->pid);
	if (dev->iface == NULL)
		goto fail_free;

	memset(dev->nis, 0, sizeof(dev->nis));
	dev->uid = geteuid();

	if (pthread_mutex_init(&dev->lock, NULL) != 0) {
		LOG("%s: failed to init mutex\n", __func__);
		goto fail_iface_free;
	}

	cnt = 0;
	pdev = &swptl_dev_list;
	while (*pdev != NULL) {
		pdev = &(*pdev)->next;
		cnt++;
	}
	if (cnt == SWPTL_DEV_NMAX) {
		LOG("%s: too many pids in use by this process\n", __func__);
		goto fail_mutex_free;
	}
	dev->next = NULL;
	*pdev = dev;

	LOGN(2, "%s: nid %d, pid = %d\n", __func__, dev->nid, dev->pid);
	return dev;
fail_mutex_free:
	pthread_mutex_destroy(&dev->lock);
fail_iface_free:
	bximsg_done(dev->iface);
fail_free:
	xfree(dev);
	return NULL;
	

}

void
swptl_dev_del(struct swptl_dev *dev)
{
	struct swptl_dev **pdev;

	pdev = &swptl_dev_list;
	while (*pdev != dev)
		pdev = &(*pdev)->next;
	*pdev = dev->next;

	ptl_mutex_lock(&dev->lock, __func__);
	bximsg_done(dev->iface);
	ptl_mutex_unlock(&dev->lock, __func__);

	pthread_mutex_destroy(&dev->lock);

	LOGN(2, "%s: nid = %d, pid = %d\n", __func__, dev->nid, dev->pid);
	xfree(dev);
}

/*
 * Create a new interface with the given limits.
 */
int
swptl_ni_init(struct swptl_ni *ni, int vc, int ntrig, int nme, int nun)
{
	int i;

	ni->ni = ni;
	ni->vc = vc;
	ni->map = NULL;
	ni->eq_list = NULL;
	ni->ct_list = NULL;
	ni->md_list = NULL;
	for (i = 0; i < SWPTL_NPTE; i++)
		ni->pte[i] = NULL;
	ni->trig_pending = NULL;
	ni->serial = 0;
	pool_init(&ni->ictx_pool, "ictx_pool",
	    sizeof(struct swptl_sodata), SWPTL_ICTX_COUNT);
	pool_init(&ni->tctx_pool, "tctx_pool",
	    sizeof(struct swptl_sodata), SWPTL_TCTX_COUNT);
	pool_init(&ni->trig_pool, "trig_pool",
	    sizeof(struct swptl_trig), ntrig);
	pool_init(&ni->me_pool, "me_pool",
	    sizeof(struct swptl_me), nme);
	pool_init(&ni->unex_pool, "unex_pool",
	    sizeof(struct swptl_unex), nun);
	ni->txops = ni->rxops = NULL;
	ni->rxcnt = 0;
	ni->txcnt = 0;
	ni->nunex = nun;
	ni->ntrig = ntrig;
	ni->nme = nme;
	return 1;
}

/*
 * Destroy the given interface. EQs, MDs, PTEs and CTs must be freed
 * before.
 */
void
swptl_ni_done(struct swptl_ni *ni)
{
	int i;

	if (ni->md_list)
		ptl_panic("swptl_ni_del: md list not empty\n");

	for (i = 0; i < SWPTL_NPTE; i++) {
		if (ni->pte[i])
			ptl_panic("swptl_ni_del: pte %d not empty\n", i);
	}
	if (ni->eq_list)
		ptl_panic("swptl_ni_del: eq list not empty\n");

	if (ni->ct_list)
		ptl_panic("swptl_ni_del: ct list not empty\n");

	pool_done(&ni->ictx_pool);
	pool_done(&ni->tctx_pool);
	pool_done(&ni->trig_pool);
	pool_done(&ni->unex_pool);
	pool_done(&ni->me_pool);
}

/*
 * Convert logical network address (aka rank) to physical network
 * address (nid, pid).
 */
int
swptl_ni_l2p(struct swptl_ni *ni, int rank, unsigned int *nid, unsigned int *pid)
{
	if (rank == PTL_RANK_ANY) {
		*nid = PTL_NID_ANY;
		*pid = PTL_PID_ANY;
		return 1;
	}
	if (ni->map == NULL || rank >= ni->mapsize) {
		LOGN(2, "%s: bad rank\n", __func__);
		return 0;
	}
	*nid = ni->map[rank].phys.nid;
	*pid = ni->map[rank].phys.pid;
	return 1;
}

/*
 * Convert logical network address (aka rank) to physical network
 * address (nid, pid).
 *
 * XXX: this function shouldn't be used for obvious performance
 * reasons. Instead, pass the rank in the messages and/or in the
 * attributes of the HELLO packet
 */
int
swptl_ni_p2l(struct swptl_ni *ni, int nid, int pid)
{
	int rank;

	if (ni->map == NULL)
		return -1;

	rank = 0;
	for (;;) {
		if (rank == ni->mapsize)
			return -1;

		if (ni->map[rank].phys.nid == nid &&
		    ni->map[rank].phys.pid == pid)
			break;
		rank++;
	}
	return rank;
}

/*
 * Find the ME on the given PTE and list (over or prio) matching the
 * given criteria. Return a pointer to the pointer holding the ME, so
 * it can be deleted without walking through the list.
 */
struct swptl_me **
swptl_mefind(struct swptl_pte *pte, int list,
	int nid, int pid, int uid, unsigned long long bits, uint64_t roffs, uint64_t rlen, int cmd)
{
	struct swptl_me *me, **pme;
	uint64_t offs;

	LOGN(2, "%s: searching list %d for "
	    "nid = %d, pid = %d, uid = %d, bits = %016llx\n",
		__func__, list, nid, pid, uid, bits);

	switch (list) {
	case PTL_PRIORITY_LIST:
		pme = &pte->prio.head;
		break;
	case PTL_OVERFLOW_LIST:
		pme = &pte->over.head;
		break;
	default:
		ptl_panic("swptl_mefind: bad call\n");
		return NULL; /* Fix compilation warning */
	}
	for (;;) {
		if ((me = *pme) == NULL) {
			LOGN(2, "%s: no me found\n", __func__);
			return 0;
		}
		LOGN(2, "%s: trying me %p "
		    "nid = %d, pid = %d, uid = %d, "
		    "bits = %016llx/%016llx\n",
		    __func__, me, me->nid, me->pid, me->uid,
		    me->bits, me->mask);

		if (list == PTL_OVERFLOW_LIST && cmd == SWPTL_PUT) {
			if (me->opt & PTL_ME_OV_RDV_PUT_ONLY) {
				if (ptlbxi_rdv_put == 0 || rlen < ptlbxi_rdv_put) {
					pme = &me->next;
					continue;
				}
			} else if (me->opt & PTL_ME_OV_RDV_PUT_DISABLE) {
				if (ptlbxi_rdv_put && rlen >= ptlbxi_rdv_put) {
					pme = &me->next;
					continue;
				}
			}
		}

		if ((me->nid == PTL_NID_ANY || me->nid == nid) &&
		    (me->pid == PTL_PID_ANY || me->pid == pid) &&
		    (me->uid == PTL_UID_ANY || me->uid == uid) &&
		    ((bits ^ me->bits) & ~me->mask) == 0) {
			if (!(me->opt & PTL_ME_NO_TRUNCATE))
				break;
			offs = (me->opt & PTL_ME_MANAGE_LOCAL) ?
			    me->offs : roffs;
			if (offs + rlen <= me->len)
				break;
			LOGN(2, "%s: would be truncated, skipped\n", __func__);
		}
		pme = &me->next;
	}
	return pme;
}

/*
 * Unlink the given ME from whichever list it is linked to. This is
 * the code-path for auto-unlink of non-persistent MEs.
 */
void
swptl_autounlink(struct swptl_me **pme)
{
	struct swptl_mequeue *q;
	struct swptl_me *me = *pme;

	if (!(me->opt & PTL_ME_USE_ONCE)) {
		if (!(me->opt & PTL_ME_MANAGE_LOCAL))
			return;
		if (me->minfree == 0)
			return;
		if (me->offs + me->minfree <= me->len)
			return;
	}

	q = (me->list == PTL_PRIORITY_LIST) ? &me->pte->prio : &me->pte->over;
	*pme = me->next;
	if (*pme == NULL)
		q->tail = pme;
	me->list = -1;
	LOGN(2, "%s: (meh = %p), refs = %d\n", __func__, me, me->refs);
	swptl_meunref(me->pte->ni, me, 0);
}

/*
 * Allocate an unexpected-header
 */
struct swptl_unex *
swptl_unexnew(struct swptl_ni *ni, struct swptl_me *me)
{
	struct swptl_unex *u;

	if (pool_isempty(&ni->unex_pool))
		return NULL;

	u = pool_get(&ni->unex_pool);
	u->ready = 0;
	u->me = me;
	me->refs++;
	return u;
}

/*
 * Return true if the given unexpected header matches the given
 * criteria.
 */
int
swptl_unexmatch(struct swptl_unex *u,
    int nid, int pid, int uid,
    unsigned long long bits,
    unsigned long long mask)
{
	LOGN(2, "%s: trying unex (nid=%d, pid=%d, uid=%d, bits=%016llx)\n",
		__func__, u->nid, u->pid, u->uid, u->bits);
	if (nid != PTL_NID_ANY && u->nid != nid) {
		LOGN(2, "%s: nid mismatch\n", __func__);
		return 0;
	}
	if (pid != PTL_PID_ANY && u->pid != pid) {
		LOGN(2, "%s: pid mismatch\n", __func__);
		return 0;
	}
	if (uid != PTL_UID_ANY && u->uid != uid) {
		LOGN(2, "%s: uid mismatch (expected %d)\n", __func__, uid);
		return 0;
	}
	if (((u->bits ^ bits) & ~mask) != 0) {
		LOGN(2, "%s: bits mismatch (expected %016llx/%016llx)\n", __func__, bits, mask);
		return 0;
	}
	return 1;
}

/*
 * Append the given unexpected-header to the PTE.
 */
void
swptl_unexadd(struct swptl_pte *pte, struct swptl_unex *u)
{
	u->next = NULL;
	*pte->unex.tail = u;
	pte->unex.tail = &u->next;
	LOGN(2, "%s: added unex %p\n", __func__, u);
}

/*
 * Print the list of unexpected headers of the given PTE.
 */
void
swptl_dumpunex(struct swptl_pte *pte)
{
	struct swptl_unex *u;

	for (u = pte->unex.head; u != NULL; u = u->next) {
		LOGN(2, "%s: un %p %s (nid=%d, pid=%d, uid=%d, bits=%016llx\n",
		    __func__,
		    u, u->ready ? "ready" : "busy",
		    u->nid, u->pid, u->uid, u->bits);
	}
}

/*
 * PtlMESearch() implementation: walk through all matching unexpected-headers,
 * generate corresponding events and possibly delete them.
 */
void
swptl_pte_search(struct swptl_pte *pte,
    int sop,
    struct swptl_ct *ct,
    ptl_uid_t uid,
    int opt,
    int nid,
    int pid,
    ptl_match_bits_t bits,
    ptl_match_bits_t mask,
    void *uptr)
{
	struct swptl_ni *ni = pte->ni;
	struct swptl_me *un_me;
	struct swptl_unex *un, **pun;
	int found;

	LOGN(2, "%s: uptr = %p, index %d\n", __func__, uptr, pte->index);

	/* XXX: factor this with swptl_me_add() */
	found = 0;
	if (sop == PTL_SEARCH_DELETE) {
		pun = &pte->unex.head;
		for (;;) {
			if ((un = *pun) == NULL)
				break;
			if (!swptl_unexmatch(un, nid, pid, uid, bits, mask)) {
				pun = &un->next;
				continue;
			}

			/* wait for transfer to complete */
			while (!un->ready)
				swptl_dev_progress(ni->dev, 1);

			/* detach from list */
			*pun = un->next;
			if (*pun == NULL)
				pte->unex.tail = pun;

			/* post events */
			un_me = un->me;
			swptl_postcomm(pte, opt, ct,
			    un->type,
			    un->fail,
			    un->aop,
			    un->atype,
			    un->nid,
			    un->pid,
			    un->rank,
			    un->offs,
			    un->base,
			    un->mlen,
			    un->rlen,
			    uptr,
			    un->uid,
			    un->hdr,
			    un->bits);
			LOGN(3, "%s: posted a unex\n", __func__);
			pool_put(&ni->unex_pool, un);
			swptl_meunref(ni, un_me, 1);
			found++;
			if (opt & PTL_ME_USE_ONCE)
				break;
		}
	} else if (sop == PTL_SEARCH_ONLY) {
		pun = &pte->unex.head;
		for (;;) {
			if ((un = *pun) == NULL)
				break;
			if (!swptl_unexmatch(un, nid, pid, uid, bits, mask)) {
				pun = &un->next;
				continue;
			}

			/* wait for transfer to complete */
			while (!un->ready)
				swptl_dev_progress(ni->dev, 1);

			swptl_postcomm(pte, opt, ct,
			    PTL_EVENT_SEARCH,
			    PTL_NI_OK,
			    un->aop,
			    un->atype,
			    un->nid,
			    un->pid,
			    un->rank,
			    un->offs,
			    un->base,
			    un->mlen,
			    un->rlen,
			    uptr,
			    un->uid,
			    un->hdr,
			    un->bits);
			found++;
			if (opt & PTL_ME_USE_ONCE)
				break;
			pun = &un->next;
		}
	} else {
		ptl_panic("0x%x: unknown search op\n", sop);
	}
	if (found == 0) {
		LOGN(2, "%s: search_only: no unex found\n", __func__);
		swptl_postcomm(pte, opt, ct,
		    PTL_EVENT_SEARCH, PTL_NI_NO_MATCH,
		    0, 0, 0, 0, 0, 0, 0, 0, 0, uptr, 0, 0, 0);
	} else if (!(opt & PTL_ME_USE_ONCE)) {
		swptl_postcomm(pte, opt, ct,
		    PTL_EVENT_SEARCH, PTL_NI_NO_MATCH,
		    0, 0, 0, 0, 0, 0, 0, 0, 0, uptr, 0, 0, 0);
	}
}

/*
 * PtlMEAppend() implementation: walk through all matching
 * unexpected-headers, generate corresponding events and possibly
 * delete them, then append the given ME to the proper PTE list.
 */
void
swptl_me_add(struct swptl_me *me,
    struct swptl_ni *ni,
    struct swptl_pte *pte,
    void *buf,
    size_t len,
    struct swptl_ct *ct,
    ptl_uid_t uid,
    int opt,
    int nid,
    int pid,
    ptl_match_bits_t bits,
    ptl_match_bits_t mask,
    size_t minfree,
    int list,
    void *uptr)
{
	struct swptl_mequeue *q;
	struct swptl_me *un_me;
	struct swptl_unex *un, **pun;
	size_t un_mlen;
	size_t un_rlen;

	me->pte = pte;
	me->buf = buf;
	me->len = len;
	me->offs = 0;
	me->ct = ct;
	me->uid = uid;
	me->opt = opt;
	me->xfers = 0;
	me->nid = nid;
	me->pid = pid;
	me->bits = bits;
	me->mask = mask;
	minfree = (opt & PTL_ME_MANAGE_LOCAL) ? minfree : 0;

	LOGN(2, "%s: %p, len = %zd, uptr = %p to index %d, list = %d, opt = 0x%x, minfree = %zd\n",
		__func__, me, len, uptr, pte->index, list, opt, minfree);

	me->minfree = minfree;
	me->uptr = uptr;
	if (opt & PTL_IOVEC) {
		me->niov = me->len;
		me->len = swptl_iovlen(buf, len);
	} else {
		me->len = len;
		me->niov = -1;
	}

	if (list == PTL_PRIORITY_LIST && pte->unex.head != NULL) {
		pun = &pte->unex.head;
		for (;;) {
			if ((un = *pun) == NULL)
				break;
			if (!swptl_unexmatch(un, me->nid,
				me->pid, me->uid, me->bits, me->mask)) {
				pun = &un->next;
				continue;
			}

			/* wait for transfer to complete */
			while (!un->ready)
				swptl_dev_progress(ni->dev, 1);

			/* detach from list */
			*pun = un->next;
			if (*pun == NULL)
				pte->unex.tail = pun;

			/* post events */
			un_me = un->me;
			swptl_postcomm(pte, me->opt, me->ct,
			    un->type,
			    un->fail,
			    un->aop,
			    un->atype,
			    un->nid,
			    un->pid,
			    un->rank,
			    un->offs,
			    un->base,
			    un->mlen,
			    un->rlen,
			    me->uptr,
			    un->uid,
			    un->hdr,
			    un->bits);
			LOGN(3, "%s: posted a unex\n", __func__);

			un_mlen = un->mlen;
			un_rlen = un->rlen;
			pool_put(&ni->unex_pool, un);

			/*
			 * decrement ref count of ME that generated
			 * the unex. If un_me->refs reaches 0, this is
			 * because it's auto-unlinked.
			 */
			swptl_meunref(ni, un_me, 1);

			if (me->opt & PTL_ME_USE_ONCE ||
			    (me->opt & PTL_ME_MANAGE_LOCAL &&
			     me->opt & PTL_ME_MANAGE_LOCAL_STOP_IF_UH)) {
				/* spec requires, sec. 3.13.1 */
				swptl_postlink(pte, me,
				    PTL_EVENT_AUTO_UNLINK, PTL_OK);
				return;
			}

			if (!(me->opt & PTL_ME_MANAGE_LOCAL))
				continue;

			if (me->opt & PTL_ME_LOCAL_INC_UH_RLENGTH)
				me->offs += un_rlen;
			else if (me->opt & PTL_ME_UH_LOCAL_OFFSET_INC_MANIPULATED)
				me->offs += un_mlen;
			else
				continue;

			if (me->offs > me->len)
				me->offs = me->len;

			if (me->len < me->offs + minfree) {
				swptl_postlink(pte, me, PTL_EVENT_AUTO_UNLINK, PTL_OK);
				return;
			}
		}
	}

	me->list = list;
	swptl_postlink(pte, me, PTL_EVENT_LINK, PTL_OK);

	/* append to the "prio/over" list */
	q = (list == PTL_PRIORITY_LIST) ? &pte->prio : &pte->over;
	me->next = NULL;
	*q->tail = me;
	q->tail = &me->next;
	me->refs++;
}

/*
 * Remove (discard) all MEs and unexpected headers from the given PTE.
 * This mimic what BXI NIC does to free PTEs (which is not what the
 * spec requires, but makes sense).
 */
void
swptl_pte_cleanup(struct swptl_pte *pte)
{
	struct swptl_me *me;
	struct swptl_unex *un;


	LOGN(2, "%s\n", __func__);

	while ((un = pte->unex.head) != NULL) {
		swptl_meunref(pte->ni, un->me, 0);
		pte->unex.head = un->next;
		LOGN(2, "%s: removed unex %p\n", __func__, un);
		pool_put(&pte->ni->unex_pool, un);
	}
	pte->unex.tail = &pte->unex.head;

	while ((me = pte->prio.head) != NULL) {
		while (me->refs > 1)
			swptl_dev_progress(pte->ni->dev, 1);
		me->list = -1;
		LOGN(2, "%s: %p: removing\n", __func__, me);
		if (me->refs > 1)
			ptl_panic("swptl_pte_cleanup: %p: prio refs = %d\n",
				      me, me->refs);

		pte->prio.head = me->next;
		swptl_meunref(pte->ni, me, 0);
	}
	pte->prio.tail = &pte->prio.head;

	while ((me = pte->over.head) != NULL) {
		while (me->refs > 1)
			swptl_dev_progress(pte->ni->dev, 1);
		me->list = -1;
		if (me->refs > 1)
			ptl_panic("swptl_pte_cleanup: %p: over refs = %d\n",
				      me, me->refs);

		pte->over.head = me->next;
		swptl_meunref(pte->ni, me, 0);
	}
	pte->over.tail = &pte->over.head;
}

/*
 * Common implementation for Ptl{Put,Get,Atomic,FetchAtomic,Swap} and
 * their triggered equivalents.
 *
 * If the trig_ct argument, the command is assumed to be a triggered
 * one, else it's executed immediately.
 */
int
swptl_cmd(int cmd,
    struct swptl_ni *ni,
    struct swptl_md *get_md, ptl_size_t get_mdoffs,
    struct swptl_md *put_md, ptl_size_t put_mdoffs,
    size_t len,
    ptl_process_t dest,
    unsigned int pte,
    ptl_match_bits_t bits,
    ptl_size_t meoffs,
    void *uptr,
    ptl_hdr_data_t hdr_data,
    const void *cst,
    ptl_op_t aop,
    ptl_datatype_t atype,
    int ack,
    struct swptl_ct *trig_ct,
    ptl_size_t trig_thres)
{
	struct swptl_sodata *sodata = NULL;
	struct swptl_ictx *ctx; /* XXX: use sodata */
	struct swptl_trig *trig;
	unsigned int nid, pid;
	char buf[PTL_LOG_BUF_SIZE];
	int buf_len;

	if (SWPTL_ISATOMIC(cmd)) {
		if (len > SWPTL_MAXATOMIC)
			ptl_panic("cmd: atomic exceeds max size\n");
	}

	if (SWPTL_ISPHYSICAL(ni->vc)) {
		nid = dest.phys.nid;
		pid = dest.phys.pid;
	} else {
		if (!swptl_ni_l2p(ni, dest.rank, &nid, &pid))
			return 0;
		LOGN(2, "%s: rank %u -> (%u, %u)\n", __func__, dest.rank, nid, pid);
	}
	if (trig_ct != NULL) {
		/*
		 * One may think that we could open the connection
		 * here, but the peer process may not be started yet
		 */
		trig = swptl_trigadd(trig_ct, trig_thres);
		if (trig == NULL) {
			ptl_log("couldn't get trig op\n");
			return 0;
		}
		LOGN(2, "%s: trig: ct = %p, thres = %zu\n",
			__func__, trig_ct, trig_thres);
		trig->scope = SWPTL_TRIG_TX;
		trig->u.tx.nid = nid;
		trig->u.tx.pid = pid;
		ctx = &trig->u.tx.ictx;
		sodata = NULL;
	} else {
		while (pool_isempty(&ni->ictx_pool))
			swptl_dev_progress(ni->dev, 1);
		sodata = pool_get(&ni->ictx_pool);
		sodata->init = 1;
		sodata->conn = bximsg_getconn(ni->dev->iface, nid, pid,
					      ni->vc);
		ctx = &sodata->u.ictx;
	}

	if (swptl_verbose >= 2) {
		buf_len = snprintf(buf, sizeof(buf),
				   "cmd: %s, uptr=%p, nid=%u, pid=%u, len=%zu",
				   swptl_cmdname[cmd], uptr, nid, pid, len);
		if (SWPTL_ISPUT(cmd))
			buf_len += snprintf(buf + buf_len,
					    sizeof(buf) - buf_len,
					    ", put_mdoffs=0x%lx", put_mdoffs);

		buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len,
				    ", pte=0x%x, roffs=0x%lx", pte, meoffs);
		if (SWPTL_ISGET(cmd))
			buf_len += snprintf(buf + buf_len,
					    sizeof(buf) - buf_len,
					    ", get_mdoffs=0x%lx", get_mdoffs);
		snprintf(buf + buf_len, sizeof(buf) - buf_len, "\n");
		ptl_log("%s", buf);
	}

	/*
	 * copy TX command
	 */
	ctx->cmd = cmd;
	ctx->aop = aop;
	ctx->atype = atype;
	ctx->ack = ack;
	ctx->pte = pte;
	ctx->serial = ni->serial++;
	ctx->rlen = len;
	ctx->hdr_data = hdr_data;
	ctx->bits = bits;
	if (cmd == SWPTL_SWAP && cst)
		memcpy(ctx->swapcst, cst, ptl_atsize(atype));
	ctx->uptr = uptr;
	ctx->put_md = put_md;
	ctx->put_mdoffs = put_mdoffs;
	ctx->get_md = get_md;
	ctx->get_mdoffs = get_mdoffs;
	ctx->query_meoffs = meoffs;

	LOGN(2, "%s: %u: %p: sending %s query with rlen = %zu, (%d, %d) -> (%d, %d)\n",
	    __func__,
	    ctx->serial,
	    ctx->uptr, swptl_cmdname[ctx->cmd], ctx->rlen,
	    ni->dev->nid,
	    ni->dev->pid,
	    nid, pid);
	LOGN(2, "%s: target pte 0x%x, meoffs = 0x%zx, bits = %016lx\n",
		__func__, ctx->pte, ctx->query_meoffs, ctx->bits);

	/*
	 * decode md handles
	 */
	if (SWPTL_ISPUT(cmd)) {
		LOGN(2, "%s: hdr_data = 0x%lx\n", __func__, ctx->hdr_data);
		LOGN(2, "%s: put_md %p, offs 0x%zx\n",
			__func__, ctx->put_md, ctx->put_mdoffs);
		if (ctx->put_mdoffs + ctx->rlen > ctx->put_md->len) {
			LOGN(2, "%s: rdma out of put_md bounds: rlen = %zu, "
			    "mdoffs = 0x%zx, md_len = 0x%zx\n",
				__func__, ctx->rlen, ctx->put_mdoffs, ctx->put_md->len);
			swptl_postack(ctx->put_md,
			    PTL_EVENT_SEND, PTL_NI_SEGV, 0,
			    ctx->rlen, 0,
			    ctx->uptr);
			LOGN(2, "%s: %u: tx transfer complete\n", __func__, ctx->serial);
			pool_put(&ni->ictx_pool, sodata);
			return 1;
		}
	}
	if (SWPTL_ISGET(cmd)) {
		LOGN(2, "%s: get_md %p, offs 0x%zx\n",
			__func__, ctx->get_md, ctx->get_mdoffs);
		if (ctx->get_mdoffs + ctx->rlen > ctx->get_md->len) {
			LOGN(2, "%s: rdma out of get_md bounds: rlen = %zu, "
			    "mdoffs = 0x%zx, md_len = 0x%zx\n",
				__func__, ctx->rlen, ctx->get_mdoffs, ctx->get_md->len);
			swptl_postack(ctx->get_md,
			    PTL_EVENT_REPLY, PTL_NI_SEGV, 0,
			    ctx->rlen, 0,
			    ctx->uptr);
			LOGN(2, "%s: %u: tx transfer complete\n", __func__, ctx->serial);
			pool_put(&ni->ictx_pool, sodata);
			return 1;
		}
	}

	/*
	 * now that mds are decoded, lock the mds
	 */
	if (SWPTL_ISPUT(cmd))
		ctx->put_md->refs++;
	if (SWPTL_ISGET(cmd))
		ctx->get_md->refs++;
	if (trig_ct)
		swptl_trig(ni);
	else {
		sodata->hdrsize = swptl_qhdr_getsize(ctx->cmd);
		swptl_volmove(ctx);
		swptl_ctx_add(&ni->txops, sodata);
		bximsg_enqueue(ni->dev->iface, sodata, SWPTL_ISPUT(cmd) ? len : 0);
		LOGN(2, "%s: %u: %zd byte %s query (%d, %d) -> (%d, %d), ictx = %zu\n",
		      __func__,
		     ctx->serial,
		     ctx->rlen,
		     swptl_cmdname[ctx->cmd],
		     ni->dev->nid,
		     ni->dev->pid,
		     sodata->conn->nid,
		     sodata->conn->pid,
		     sodata - (struct swptl_sodata *)ni->ictx_pool.data);
	}
	return 1;
}

/*
 * Generate comm events after a message was completely processed on
 * initiator side
 */
void
swptl_iend(struct swptl_ni *ni, struct swptl_sodata *f, int neterr)
{
	struct swptl_ictx *ctx = &f->u.ictx;

	if (neterr)
		ctx->fail = PTL_NI_UNDELIVERABLE;

	if (SWPTL_ISPUT(ctx->cmd))
		ctx->put_md->refs--;

	if (SWPTL_ISGET(ctx->cmd)) {
		ctx->get_md->refs--;
		swptl_postack(ctx->get_md,
			      PTL_EVENT_REPLY,
			      ctx->fail,
			      ctx->list,
			      ctx->mlen,
			      ctx->reply_meoffs,
			      ctx->uptr);
	} else {
		switch (ctx->ack) {
		case PTL_NO_ACK_REQ:
			LOGN(2, "%s: %u: no ack requested\n", __func__, ctx->serial);
			break;
		case PTL_ACK_REQ:
			swptl_postack(ctx->put_md,
				      PTL_EVENT_ACK,
				      ctx->fail,
				      ctx->list,
				      ctx->mlen,
				      ctx->reply_meoffs,
				      ctx->uptr);
			break;
		case PTL_CT_ACK_REQ:
			if (ctx->put_md->ct != NULL) {
				swptl_ct_cmd(SWPTL_CTINC, ctx->put_md->ct,
					     ctx->fail == PTL_OK, ctx->fail != PTL_OK);
			}
			break;
		case PTL_OC_ACK_REQ:
			if (ctx->put_md->ct != NULL) {
				swptl_ct_cmd(SWPTL_CTINC, ctx->put_md->ct,
					     1, 0);
			}
			break;
		default:
			ptl_log("%u: bad ack type\n", ctx->ack);
			break;
		}
	}
	LOGN(2, "%s: %u: tx transfer complete: %s\n", __func__, ctx->serial,
	     neterr ? "failed" : "ok");
	swptl_ctx_rm(&ni->txops, f);
	pool_put(&ni->ictx_pool, f);
	ni->txcnt++;
}

/*
 * Called by the network layer whenever the socket becomes ready and
 * there's an enqueued query to start transferring.
 */
void
swptl_snd_qstart(struct swptl_ni *ni,
		 struct swptl_sodata *f,
		 struct swptl_query *query)
{
	struct swptl_ictx *ctx = &f->u.ictx;

	/* create message */
	query->hdr_data = ctx->hdr_data;
	query->bits = ctx->bits;
	query->meoffs = ctx->query_meoffs;
	query->rlen = ctx->rlen;
	query->serial = ctx->serial;
	query->cookie = f - (struct swptl_sodata *)ni->ictx_pool.data;
	query->cmd = ctx->cmd;
	query->aop = ctx->aop;
	query->atype = ctx->atype;
	query->ack = ctx->ack;
	query->pte = ctx->pte;
	f->hdrsize = swptl_qhdr_getsize(ctx->cmd);
	if (ctx->cmd == SWPTL_SWAP)
		memcpy(query->swapcst, ctx->swapcst, sizeof(query->swapcst));

}

/*
 * Called from the network layer when a chunk of query data is needed.
 */
void
swptl_snd_qdat(struct swptl_ni *ni, struct swptl_sodata *f, size_t msgoffs,
	       void **rdata, size_t *rsize)
{
	struct swptl_ictx *ctx = &f->u.ictx;
	void *data;
	size_t size;
	char buf[PTL_LOG_BUF_SIZE];

	if (!SWPTL_ISPUT(ctx->cmd) || msgoffs == ctx->rlen)
		ptl_panic("swptl_snd_qdat: bad call\n");

	if (SWPTL_ISVOLATILE(ctx)) {
		data = ctx->vol_data;
		size = ctx->rlen;
	} else {
		swptl_iovseg(ctx->put_md->buf, ctx->put_md->niov,
			     ctx->put_mdoffs + msgoffs,
			     ctx->rlen - msgoffs,
			     &data, &size);
	}

	LOGN(2, "%s: %u: sent %zu bytes query data\n", __func__, ctx->serial, size);
	if (swptl_verbose >= 4) {
		snprintf(buf, sizeof(buf), "%u: data at %p, sent: ",
			 ctx->serial, data);
		swptl_log_hex(buf, data, size);
	}

	*rdata = data;
	*rsize = size;
}

/*
 * Called from the network layer when transfer is complete.
 */
void
swptl_snd_qend(struct swptl_ni *ni, struct swptl_sodata *f, int neterr)
{
	struct swptl_ictx *ctx = &f->u.ictx;	

	LOGN(2, "%s: %u: query complete, %s\n", __func__, ctx->serial,
	     neterr ? "failed" : "ok");

	if (SWPTL_ISPUT(ctx->cmd) && !SWPTL_ISVOLATILE(ctx)) {
		swptl_postack(ctx->put_md,
			      PTL_EVENT_SEND,
			      neterr ? PTL_NI_UNDELIVERABLE : PTL_NI_OK, 0,
			      ctx->rlen, 0,
			      ctx->uptr);
	}

	if (neterr || (!SWPTL_ISGET(ctx->cmd) && ctx->ack == PTL_NO_ACK_REQ))
		swptl_iend(ni, f, neterr);
}

/*
 * Called by the network layer whenever a query header was just
 * received.  Prepare to receive the payload, if any.
 */
int
swptl_rcv_qstart(struct swptl_ni *ni,
		 struct swptl_query *query,
		 int nid, int pid, int uid,
		 struct swptl_sodata **pctx, size_t *rsize)
{
	struct swptl_sodata *f;
	struct swptl_tctx *ctx;
	struct swptl_unex *u;
	struct swptl_me **pme;
	struct poolent *ev;
	struct pool *ev_pool;
	size_t avail;
	int i, nev;

	if (pool_isempty(&ni->tctx_pool)) {
		LOGN(2, "%s: out of pool enties\n", __func__);
		return 0;
	}

	f = *pctx = pool_get(&ni->tctx_pool);
	f->init = 0;
	f->conn = bximsg_getconn(ni->dev->iface, nid, pid, ni->vc);

	/*
	 * If rank is not set (i.e. setmap() not called when
	 * connection was accepted), then try to set it in case
	 * setmap() was called meanwhile. If the rank is still not
	 * known then drop the packet.
	 */
	if (!SWPTL_ISPHYSICAL(ni->vc) && f->conn->rank == -1) {
		f->conn->rank = swptl_ni_p2l(ni, nid, pid);
		if (f->conn->rank == -1) {
			LOGN(2, "%s: rank not set\n", __func__);
			return 0;
		}
	}

	ctx = &f->u.tctx;
	ctx->uid = uid;

	LOGN(2, "%s: %u: received %s query (%d, %d) -> (%d, %d), tctx = %zd\n",
	     __func__,
	     query->serial,
	     swptl_cmdname[query->cmd],
	     nid, pid, ni->dev->nid, ni->dev->pid,
	     f - (struct swptl_sodata *)ni->tctx_pool.data);

	ctx->serial = query->serial;
	ctx->cookie = query->cookie;
	ctx->bits = query->bits;
	ctx->cmd = query->cmd;
	ctx->aop = query->aop;
	ctx->atype = query->atype;
	ctx->pte = ni->pte[query->pte];
	ctx->rlen = query->rlen;
	ctx->hdr_data = query->hdr_data;
	ctx->query_meoffs = query->meoffs;
	ctx->ack = query->ack;
	ctx->fail = PTL_OK;
	ctx->mlen = 0;
	ctx->unex = NULL;
	ctx->evs = NULL;
	ctx->me = NULL;
	f->hdrsize = swptl_qhdr_getsize(ctx->cmd);
	if (ctx->cmd == SWPTL_SWAP)
		memcpy(ctx->swapcst, query->swapcst, sizeof(ctx->swapcst));
	swptl_ctx_add(&ni->rxops, f);

	*rsize = SWPTL_ISPUT(ctx->cmd) ? ctx->rlen : 0;

	if (ctx->pte == NULL) {
		ctx->fail = PTL_NI_TARGET_INVALID;
		return 1;
	}

	if (!ctx->pte->enabled) {
		ctx->fail = PTL_NI_PT_DISABLED;
		return 1;
	}

	if (SWPTL_ISATOMIC(ctx->cmd) && ctx->rlen > SWPTL_MAXATOMIC) {
		ctx->fail = PTL_NI_OP_VIOLATION;
		return 1;
	}

	/*
	 * XXX: check that rlen is aligned to atomic size, if not,
	 * either fail or round ctx->mlen
	 */

	pme = swptl_mefind(ctx->pte, PTL_PRIORITY_LIST, nid, pid,
			ctx->uid, ctx->bits, ctx->query_meoffs, ctx->rlen, ctx->cmd);
	if (pme)
		ctx->list = PTL_PRIORITY_LIST;
	else {
		pme = swptl_mefind(ctx->pte, PTL_OVERFLOW_LIST, nid, pid,
				   ctx->uid, ctx->bits, ctx->query_meoffs,
				ctx->rlen, ctx->cmd);
		if (pme)
			ctx->list = PTL_OVERFLOW_LIST;
		else if (ctx->pte->opt & PTL_PT_FLOWCTRL) {
			LOGN(2, "%s: %u: no match, triggering flowctrl\n",
				__func__, ctx->serial);
			goto flowctrl;
		} else {
			ctx->fail = PTL_NI_DROPPED;
			return 1;
		}
	}
	ctx->me = *pme;
	ctx->me->refs++;
	ctx->me->xfers++;

	LOGN(2, "%s: %u: cmd for me %p (list %d)\n",
		__func__, ctx->serial, ctx->me, ctx->list);

	if ((SWPTL_ISGET(ctx->cmd) && !(ctx->me->opt & PTL_ME_OP_GET)) ||
	    (SWPTL_ISPUT(ctx->cmd) && !(ctx->me->opt & PTL_ME_OP_PUT))) {
		LOGN(2, "%s: %u: operation disabled on this me, replying nack\n",
			__func__, ctx->serial);
		ctx->fail = PTL_NI_OP_VIOLATION;
		return 1;
	}

	if (ctx->me->opt & PTL_ME_MANAGE_LOCAL) {
		ctx->reply_meoffs = ctx->me->offs;
		LOGN(2, "%s: %u: using local offset %zu\n",
			__func__, ctx->serial, ctx->reply_meoffs);
	} else {
		ctx->reply_meoffs = ctx->query_meoffs;
		LOGN(2, "%s: %u: using remote offset %zu\n",
			__func__, ctx->serial, ctx->reply_meoffs);
	}
	if (ctx->reply_meoffs > ctx->me->len) {
		ptl_log("%u: xfer (%zu) out of me boundaries (%zu)\n",
		    ctx->serial, ctx->reply_meoffs, ctx->me->len);
		ctx->fail = PTL_NI_SEGV;
		return 1;
	}

	avail = ctx->me->len - ctx->reply_meoffs;
	if (ctx->rlen > avail) {
		/* XXX: handle PTL_ME_NO_TRUNCATE case here */
		ctx->mlen = avail;
		LOGN(2, "%s: %u: truncating %zu -> %zu\n",
			__func__, ctx->serial, ctx->rlen, ctx->mlen);
	} else
		ctx->mlen = ctx->rlen;

	if (ctx->list == PTL_OVERFLOW_LIST) {
		if (!(ctx->me->opt & PTL_ME_UNEXPECTED_HDR_DISABLE)) {
			u = swptl_unexnew(ni, ctx->me);
			if (u == NULL) {
				if (ctx->pte->opt & PTL_PT_FLOWCTRL) {
					LOGN(2, "%s: %u: no uh, flowctrl\n",
						__func__, ctx->serial);
					goto flowctrl;
				}
				ctx->fail = PTL_NI_DROPPED;
				return 1;
			}

			u->type = swptl_msgunev(ctx->cmd);
			u->fail = PTL_OK;
			u->aop = ctx->aop;
			u->atype = ctx->atype;
			u->hdr = ctx->hdr_data;
			u->base = swptl_me_ptr(ctx->me, ctx->reply_meoffs);
			/* XXX: we must use md-side offset, right? */
			u->offs = ctx->query_meoffs;
			u->mlen = ctx->mlen;
			u->rlen = ctx->rlen;
			u->nid = nid;
			u->pid = pid;
			u->rank = f->conn->rank;
			u->uid = ctx->uid;
			u->bits = ctx->bits;

			/*
			 * We must link the unex to the list
			 * here, as a subsequent ME append may
			 * must be able to match it. It'll not
			 * be allowed to generate the overflow
			 * event yet, because the transfer is
			 * in progress.
			 *
			 * The append/search code-paths
			 * examine the ready flag and wait the
			 * transfer to complete.
			 */
			ctx->unex = u;
			swptl_unexadd(ctx->pte, u);
		}
	}

	/* reserve events for flow-control */
	if (ctx->pte->eq) {
		nev = 0;
		if (!(ctx->me->opt & PTL_ME_EVENT_COMM_DISABLE))
			nev++;
		if (!(ctx->me->opt & PTL_ME_EVENT_UNLINK_DISABLE) &&
		    ctx->me->list < 0) {
			nev++;
			if (ctx->list == PTL_OVERFLOW_LIST)
				nev++;
		}
		ev_pool = &ctx->pte->eq->ev_pool;
		for (i = 0; i < nev; i++) {
			ev = ev_pool->first;
			if (ev == NULL) {
				if (ctx->pte->opt & PTL_PT_FLOWCTRL) {
					LOGN(2, "%s: %u: eq full, flowctrl\n",
						__func__, ctx->serial);
					goto flowctrl;
				}
				break;
			}
			ev_pool->first = ev->next;
			ev->next = ctx->evs;
			ctx->evs = ev;
		}
	}

	if (ctx->me->opt & PTL_ME_MANAGE_LOCAL) {
		ctx->me->offs += ctx->mlen;
		LOGN(2, "%s: %u: manage local offset -> %zu\n",
			__func__, ctx->serial, ctx->me->offs);
	}

	if (ctx->fail == PTL_NI_OK)
		swptl_autounlink(pme);

	return 1;
flowctrl:
	ctx->fail = PTL_NI_PT_DISABLED;
	if (ctx->me != NULL) {
		ctx->me->refs--;
		ctx->me = NULL;
	}
	ctx->mlen = 0;
	ctx->pte->enabled = 0;
	if (ctx->pte->ev) {
		pool_put(&ctx->pte->eq->ev_pool, ctx->pte->ev);
		ctx->pte->ev = NULL;
	}

	swptl_postcomm(ctx->pte,
		       0,
		       NULL,
		       PTL_EVENT_PT_DISABLED,
		       ctx->fail,
		       0, 0,    /* aop; atype */
		       0, 0, 0, /* nid, pid, rank */
		       0,       /* roffs */
		       NULL,    /* start */
		       0,       /* mlen */
		       0,       /* rlen */
		       NULL,    /* uptr */
		       0,       /* uid */
		       0,       /* hdr */
		       0);      /* bits */

	return 1;
}

/*
 * Called by the network layer whenever a chunk of query data is
 * received.
 */
void
swptl_rcv_qdat(struct swptl_ni *ni, struct swptl_sodata *f, size_t msgoffs,
	       void **rdata, size_t *rsize)
{
	struct swptl_tctx *ctx = &f->u.tctx;
	size_t size;
	void *data;

	if (!SWPTL_ISPUT(ctx->cmd) || msgoffs == ctx->rlen)
		ptl_panic("swptl_rcv_qdat: bad call\n");

	if (msgoffs >= ctx->mlen) {
		/*
		 * XXX: set rdata to NULL and change the
		 * caller to just omit copying the payload
		 */
		data = swptl_dummy;
		size = ctx->rlen - msgoffs;
		if (size > sizeof(swptl_dummy))
			size = sizeof(swptl_dummy);
		LOGN(2, "%s: %u: dropping %zu bytes\n", __func__, ctx->serial, size);
	} else {
		if (SWPTL_ISATOMIC(ctx->cmd)) {
			data = ctx->atbuf;
			size = ctx->mlen;
		} else {
			swptl_iovseg(ctx->me->buf, ctx->me->niov,
				     ctx->reply_meoffs + msgoffs,
				     ctx->mlen - msgoffs,
				     &data, &size);
		}
	}

	LOGN(2, "%s: %u: receiving %zu bytes query data\n", __func__, ctx->serial, size);

	*rdata = data;
	*rsize = size;
}

void
swptl_rcv_qend(struct swptl_ni *ni, struct swptl_sodata *f, int neterr)
{
	struct swptl_tctx *ctx = &f->u.tctx;
	void *data;
	size_t offs, todo, size;

	if (SWPTL_ISATOMIC(ctx->cmd)) {
		todo = ctx->mlen;
		offs = ctx->reply_meoffs;
		while (todo > 0) {
			swptl_iovseg(ctx->me->buf, ctx->me->niov,
				     offs, todo, &data, &size);

			swptl_atrcv(ctx->aop, ctx->atype, data,
				    ctx->swapcst, ctx->atbuf,
				    ctx->swapbuf,
				    size);

			todo -= size;
			offs += size;
		}
	}

	if (neterr || (!SWPTL_ISGET(ctx->cmd) && ctx->ack == PTL_NO_ACK_REQ)) {
		swptl_tend(ni, f, neterr);
		return;
	}

	if (ctx->me != NULL && !SWPTL_ISGET(ctx->cmd) &&
	    (ctx->me->opt & PTL_ME_ACK_DISABLE))
		ctx->ack = PTL_NO_ACK_REQ;

	/* build reply message header */
	f->hdrsize = offsetof(struct swptl_hdr, u) + sizeof(struct swptl_reply);
	bximsg_enqueue(ni->dev->iface, f, SWPTL_ISGET(ctx->cmd) ? ctx->mlen : 0);
}

/*
 * Start a reply. Called by the network layer whenever the socket
 * becomes ready to send and there's a enqueued reply.
 */
void
swptl_snd_rstart(struct swptl_ni *ni,
		 struct swptl_sodata *f,
		 struct swptl_reply *reply)
{
	struct swptl_tctx *ctx = &f->u.tctx;

	LOGN(2, "%s: %u: %zd byte %s reply, tctx %zu\n",
	      __func__,
	     ctx->serial,
	     ctx->mlen,
	     swptl_cmdname[ctx->cmd],
	     f - (struct swptl_sodata *)ni->tctx_pool.data);

	reply->meoffs = ctx->reply_meoffs;
	reply->mlen = ctx->mlen;
	reply->serial = ctx->serial;
	reply->cookie = ctx->cookie;
	reply->list = ctx->list;
	reply->fail = ctx->fail;
	reply->ack = ctx->ack;
}

/*
 * Called from the network layer when a chunk of reply data is needed.
 */
void
swptl_snd_rdat(struct swptl_ni *ni, struct swptl_sodata *f, size_t msgoffs,
	       void **rdata, size_t *rsize)
{
	struct swptl_tctx *ctx = &f->u.tctx;
	void *data;
	size_t size;
	char buf[PTL_LOG_BUF_SIZE];

	if (!SWPTL_ISGET(ctx->cmd) || (msgoffs == ctx->mlen))
		ptl_panic("swptl_snd_rdat: bad call\n");

	if (SWPTL_ISATOMIC(ctx->cmd)) {
		data = ctx->swapbuf;
		size = ctx->mlen;
	} else {
		swptl_iovseg(ctx->me->buf, ctx->me->niov,
			     ctx->reply_meoffs + msgoffs,
			     ctx->mlen - msgoffs,
			     &data, &size);
	}

	LOGN(2, "%s: %u: sent %zu bytes reply data\n", __func__, ctx->serial, size);
	if (swptl_verbose >= 4) {
		snprintf(buf, sizeof(buf), "%u: data at %p, sent: ",
			 ctx->serial, data);
		swptl_log_hex(buf, data, size);
	}

	*rdata = data;
	*rsize = size;
}

/*
 * Called from the network layer when transmission is complete
 */
void
swptl_snd_rend(struct swptl_ni *ni, struct swptl_sodata *f, int neterr)
{
	swptl_tend(ni, f, neterr);
}

/*
 * Generate comm events after a message was completely processed on
 * target side
 */
void
swptl_tend(struct swptl_ni *ni, struct swptl_sodata *f, int neterr)
{
	struct swptl_tctx *ctx = &f->u.tctx;
	struct poolent *ev;
	struct pool *ev_pool;
	int evtype;

	if (ctx->pte != NULL && ctx->pte->eq) {
		ev_pool = &ctx->pte->eq->ev_pool;
		while (ctx->evs) {
			ev = ctx->evs;
			ctx->evs = ev->next;
			ev->next = ev_pool->first;
			ev_pool->first = ev;
		}
	}
		
	if (ctx->me == NULL) {
		LOGN(2, "%s: %u: no matching me, target events make no sense\n",
			__func__, ctx->serial);
		goto end;
	}

	switch (ctx->cmd) {
	case SWPTL_PUT:
		evtype = PTL_EVENT_PUT;
		break;
	case SWPTL_GET:
		evtype = PTL_EVENT_GET;
		break;
	case SWPTL_ATOMIC:
		evtype = PTL_EVENT_ATOMIC;
		break;
	case SWPTL_FETCH:
	case SWPTL_SWAP:
		evtype = PTL_EVENT_FETCH_ATOMIC;
		break;
	default:
		ptl_panic("%u: unhandled command\n", ctx->serial);
		return; /* Fix compilation warning */
	}

	if (ctx->fail == PTL_NI_OK) {
		if (neterr)
			ctx->fail = PTL_NI_UNDELIVERABLE;

		swptl_postcomm(ctx->pte, ctx->me->opt, ctx->me->ct,
		    evtype, ctx->fail,
		    ctx->aop, ctx->atype,
		    f->conn->nid, f->conn->pid, f->conn->rank,
		    ctx->query_meoffs, /* XXX: reply_meoffs ? */
		    swptl_me_ptr(ctx->me, ctx->reply_meoffs),
		    ctx->mlen,
		    ctx->rlen,
		    ctx->me->uptr,
		    ctx->uid,
		    ctx->hdr_data,
		    ctx->bits);

		if (ctx->unex) {
			LOGN(2, "%s: %u: unex ready\n", __func__, ctx->serial);
			ctx->unex->ready = 1;
			ctx->unex->fail = ctx->fail;
			if (swptl_verbose >= 2)
				swptl_dumpunex(ctx->pte);
		}

		LOGN(2, "%s: %u: meopts = 0x%x\n", __func__, ctx->serial, ctx->me->opt);

	}

	if (ctx->me) {
		ctx->me->xfers--;
		if (ctx->me->list < 0 && ctx->me->xfers == 0) {
			swptl_postlink(ctx->pte, ctx->me,
			    PTL_EVENT_AUTO_UNLINK, PTL_NI_OK);
		}
		/* only overflow list generates AUTO_FREE, sec. 3.13.1 */
		swptl_meunref(ni, ctx->me, ctx->list == PTL_OVERFLOW_LIST);
	}
end:
	LOGN(2, "%s: %u: rx transfer complete, %s\n", __func__, ctx->serial,
	     neterr ? "failed" : "ok");
	swptl_ctx_rm(&ni->rxops, f);
	pool_put(&ni->tctx_pool, f);
	ni->rxcnt++;
}

/*
 * Called from the network layer to free resources and generate the proper fail
 * events.
 */
void
swptl_conn_err(void *arg, struct bximsg_conn *conn)
{
	struct swptl_dev *dev = arg;
	struct swptl_ni *ni = dev->nis[conn->vc];
	struct swptl_sodata *f, *fnext;

	for (f = ni->rxops; f != NULL; f = fnext) {
		fnext = f->ni_next;
		if (f->conn == conn)
			swptl_tend(ni, f, 1);
	}

	for (f = ni->txops; f != NULL; f = fnext) {
		fnext = f->ni_next;
		if (f->conn == conn)
			swptl_iend(ni, f, 1);
	}
}

/*
 * Called from the network layer when a reply header was received.
 */
int
swptl_rcv_rstart(struct swptl_ni *ni,
		 struct swptl_reply *reply,
		 int nid, int pid, int uid,
		 struct swptl_sodata **pctx, size_t *rsize)
{
	struct swptl_sodata *f;
	struct swptl_ictx *ctx;

	/*
	 * Find and check initiator context
	 */

	if (reply->cookie >= SWPTL_ICTX_COUNT)
		ptl_panic("%d: bad reply cookie %p\n",
			      reply->serial, reply);

	f = *pctx = (struct swptl_sodata *)ni->ictx_pool.data + reply->cookie;
	f->hdrsize = offsetof(struct swptl_hdr, u) + sizeof(struct swptl_reply);
	ctx = &f->u.ictx;

	if (ctx->serial != reply->serial)
		ptl_panic("%d: bad reply serial, expected = %d\n",
			      reply->serial, ctx->serial);

	if (f->conn->nid != nid || f->conn->pid != pid)
		ptl_panic("%d: bad nid/pid\n", reply->serial);

	if (!SWPTL_ISPHYSICAL(ni->vc) && f->conn->rank == -1)
		ptl_panic("%d: bad rank\n", reply->serial);

	ctx->reply_meoffs = reply->meoffs;
	ctx->mlen = reply->mlen;
	ctx->list = reply->list;
	ctx->fail = reply->fail;

	LOGN(2, "%s: %u: %zd byte %s reply (%d, %d) -> (%d, %d), ictx = %zu\n",
	      __func__,
	     ctx->serial,
	     ctx->mlen,
	     swptl_cmdname[ctx->cmd],
	     f->conn->nid,
	     f->conn->pid,
	     ni->dev->nid,
	     ni->dev->pid,
	     f - (struct swptl_sodata *)ni->ictx_pool.data);

	if (ctx->ack == PTL_ACK_REQ && reply->ack == PTL_NO_ACK_REQ)
		ctx->ack = PTL_NO_ACK_REQ;

	*rsize = SWPTL_ISGET(ctx->cmd) ? ctx->mlen : 0;
	return 1;
}

/*
 * Called from the network layer when a chink of reply data was received.
 */
void
swptl_rcv_rdat(struct swptl_ni *ni, struct swptl_sodata *f, size_t msgoffs,
	       void **rdata, size_t *rsize)
{
	struct swptl_ictx *ctx = &f->u.ictx;
	void *data;
	size_t size;
	char buf[PTL_LOG_BUF_SIZE];

	if (!SWPTL_ISGET(ctx->cmd) || msgoffs == ctx->mlen)
		ptl_panic("swptl_rcv_rdat: bad call\n");

	swptl_iovseg(ctx->get_md->buf, ctx->get_md->niov,
		     ctx->get_mdoffs + msgoffs,
		     ctx->mlen - msgoffs,
		     &data, &size);

	LOGN(2, "%s: %u: received %zu bytes reply data\n", __func__, ctx->serial, size);
	if (swptl_verbose >= 4) {
		snprintf(buf, sizeof(buf), "%u: data at %p, recv: ",
			 ctx->serial, data);
		swptl_log_hex(buf, data, size);
	}
	*rdata = data;
	*rsize = size;
}

void
swptl_rcv_rend(struct swptl_ni *ni, struct swptl_sodata *f, int neterr)
{
	swptl_iend(ni, f, neterr);
}

void swptl_snd_start(void *arg, struct swptl_sodata *f, void *buf)
{
	struct swptl_dev *dev = arg;
	struct swptl_ni *ni = dev->nis[f->conn->vc];
	struct swptl_hdr *hdr = buf;

	if (f->init) {
		hdr->type = SWPTL_QUERY;
		swptl_snd_qstart(ni, f, &hdr->u.query);
	} else {
		hdr->type = SWPTL_REPLY;
		swptl_snd_rstart(ni, f, &hdr->u.reply);
	}
}

void swptl_snd_data(void *arg, struct swptl_sodata *f, size_t msgoffs,
		    void **rdata, size_t *rsize)
{
	struct swptl_dev *dev = arg;
	struct swptl_ni *ni = dev->nis[f->conn->vc];

	if (f->init)
		swptl_snd_qdat(ni, f, msgoffs, rdata, rsize);
	else
		swptl_snd_rdat(ni, f, msgoffs, rdata, rsize);
}

void swptl_snd_end(void *arg, struct swptl_sodata *f, int err)
{
	struct swptl_dev *dev = arg;
	struct swptl_ni *ni = dev->nis[f->conn->vc];

	if (f->init) {
		swptl_snd_qend(ni, f, err);
	} else {
		swptl_snd_rend(ni, f, err);
	}
}

int swptl_rcv_start(void *arg,
		    void *buf, int size,
		    int nid, int pid, int vc, int uid,
		    struct swptl_sodata **pctx, size_t *rsize)
{
	struct swptl_dev *dev = arg;
	struct swptl_ni *ni;
	struct swptl_hdr *hdr = buf;

	if (vc >= SWPTL_NI_COUNT || (ni = dev->nis[vc]) == NULL) {
		LOG("%s: %d: bad vc\n", __func__, vc);
		return 0;
	}

	/*
	 * We need to check a minimum size for header independently whether it
	 * is a query or a reply. As a result, the lower bound is the minimum
	 * between the smallest query header (corresponding to all operations
	 * except SWAP) and reply header.
	 */
	if (size < offsetof(struct swptl_hdr, u.query.swapcst) &&
	    size < offsetof(struct swptl_hdr, u) + sizeof(struct swptl_reply))
		ptl_panic("swptl_rcv_start: short header\n");

	return hdr->type == SWPTL_QUERY ?
		swptl_rcv_qstart(ni, &hdr->u.query, nid, pid, uid, pctx, rsize) :
		swptl_rcv_rstart(ni, &hdr->u.reply, nid, pid, uid, pctx, rsize);
}

void swptl_rcv_data(void *arg, struct swptl_sodata *f, size_t msgoffs,
		    void **rdata, size_t *rsize)
{
	struct swptl_dev *dev = arg;
	struct swptl_ni *ni = dev->nis[f->conn->vc];

	if (f->init)
		swptl_rcv_rdat(ni, f, msgoffs, rdata, rsize);
	else
		swptl_rcv_qdat(ni, f, msgoffs, rdata, rsize);
}

void swptl_rcv_end(void *arg, struct swptl_sodata *f, int err)
{
	struct swptl_dev *dev = arg;
	struct swptl_ni *ni = dev->nis[f->conn->vc];

	if (f->init)
		swptl_rcv_rend(ni, f, err);
	else
		swptl_rcv_qend(ni, f, err);
}

void
swptl_eq_dump(struct swptl_eq *eq)
{
	struct swptl_ev *ev;

	ptl_log("eq %p: dropped = %d\n", eq, eq->dropped);
	for (ev = eq->ev_head; ev != NULL; ev = ev->next) {
		swptl_ev_log(eq->ni, &ev->ev, NULL);
	}
}

void
swptl_md_dump(struct swptl_md *md)
{
	ptl_log("md %p: eq = %p, ct = %p, opt = 0x%x, refs = %d\n",
		  md, md->eq, md->ct, md->opt, md->refs);
	if (md->opt & PTL_IOVEC) {
		ptl_log("  iovec with %d el:\n", md->niov);
		//swptl_iovec_log(md->buf, md->niov);
	} else
		ptl_log("  start = %p, len = %zd\n", md->buf, md->len);
}

void
swptl_me_dump(struct swptl_me *me)
{
	ptl_log("  me %p: uptr = %p, ct = %p, opt = 0x%x, refs = %d, list = %d\n",
		  me, me->uptr, me->ct, me->opt, me->refs, me->list);
	ptl_log("    nid = %d, pid = %d, uid = %d, "
		  "bits = %016llx/%016llx\n",
		  me->nid, me->pid, me->uid,
		  me->bits, me->mask);
	if (me->opt & PTL_IOVEC) {
		ptl_log("  iovec with %d el:\n", me->niov);
		//swptl_iovec_log(me->buf, me->niov);
	} else
		ptl_log("    start = %p, len = %zd\n", me->buf, me->len);
	if (me->opt & PTL_ME_MANAGE_LOCAL)
		ptl_log("    manage_local offs = %zd\n", me->offs);
}

void
swptl_pte_dump(struct swptl_pte *pte)
{
	struct swptl_me *me;
	struct swptl_unex *un;

	ptl_log("pte %d: opt = 0x%x:\n", pte->index, pte->opt);

	ptl_log("  prio list:\n");
	for (me = pte->prio.head; me != NULL; me = me->next) 
		swptl_me_dump(me);

	ptl_log("  over list:\n");
	for (me = pte->over.head; me != NULL; me = me->next) 
		swptl_me_dump(me);

	ptl_log("  unex list:\n");
	for (un = pte->unex.head; un != NULL; un = un->next)
		ptl_log("    un %p: nid = %d, pid = %d, uid = %d, bits=%016llx\n",
			  un, un->nid, un->pid, un->uid, un->bits);
}

int swptl_ctx_log(struct swptl_sodata *f, int buf_size, char *buf)
{
	return snprintf(buf, buf_size, "%u",
			f->init ? f->u.ictx.serial : f->u.tctx.serial);
}

void
swptl_ctx_dump(struct swptl_sodata *f)
{
	if (f->init) {
		ptl_log("%d: cmd = %s, ack = %s, nid = %d, pid = %d, "
			  "put_md = %p, get_md = %p, pte = %d, rlen = %zd, "
			  "meoffs = %zd, bits = 0x%lx\n",
			  f->u.ictx.serial,
			  swptl_cmdname[f->u.ictx.cmd],
			  swptl_ackname[f->u.ictx.ack],
			  f->conn->nid, f->conn->pid,
			  f->u.ictx.put_md,
			  f->u.ictx.get_md,
			  f->u.ictx.pte,
			  f->u.ictx.rlen,
			  f->u.ictx.query_meoffs,
			  f->u.ictx.bits);
	} else {
		ptl_log("%d: cmd = %s, nid = %d, pid = %d, pte = %d, rlen = %zd, mlen = %zd, "
			  "offs = %zd, bits = 0x%lx, fail = %d\n",
			  f->u.tctx.serial,
			  swptl_cmdname[f->u.tctx.cmd],
			  f->conn->nid, f->conn->pid,
			  f->u.tctx.pte ? f->u.tctx.pte->index : -1,
			  f->u.tctx.rlen,
			  f->u.tctx.mlen,
			  f->u.tctx.query_meoffs,
			  f->u.tctx.bits,
			  f->u.tctx.fail);
	}
}

void
swptl_dump(struct swptl_ni *ni)
{
	struct swptl_eq *eq;
	struct swptl_md *md;
	struct swptl_sodata *f;
	int pte;

	ptl_log("ni: vc = %d, nid = %d, pid = %d, uid = %d\n",
		  ni->vc, ni->dev->nid, ni->dev->pid, ni->dev->uid);
	if (ni->map)
		ptl_log("ni: map size = %zu\n", ni->mapsize);
	for (eq = ni->eq_list; eq != NULL; eq = eq->next)
		swptl_eq_dump(eq);
	for (md = ni->md_list; md != NULL; md = md->next)
		swptl_md_dump(md);
	for (pte = 0; pte < SWPTL_NPTE; pte++) {
		if (ni->pte[pte] != NULL)
			swptl_pte_dump(ni->pte[pte]);
	}
	for (f = ni->txops; f != NULL; f = f->ni_next)
		swptl_ctx_dump(f);
	for (f = ni->rxops; f != NULL; f = f->ni_next)
		swptl_ctx_dump(f);
	bximsg_dump(ni->dev->iface);
}

void
swptl_check_dump(struct swptl_dev *dev)
{
	int i;

	if (swptl_dump_pending) {
		swptl_dump_pending = 0;

		for (i = 0; i < SWPTL_NI_COUNT; i++) {
			if (dev->nis[i] != NULL)
				swptl_dump(dev->nis[i]);
		}
	}
}

void
swptl_dev_progress(struct swptl_dev *dev, int timeout)
{
	struct pollfd pfds[SWPTL_MAX_FDS];
	int nfds;
	int rc;

	swptl_check_dump(dev);

	nfds = bximsg_pollfd(dev->iface, pfds);

	if (nfds > 0) {
		ptl_mutex_unlock(&dev->lock, __func__);
		rc = poll(pfds, nfds, timeout);
		if (rc < 0) {
			if (errno == EINTR) {
				ptl_mutex_lock(&dev->lock, __func__);
				return;
			}
			ptl_panic("poll: %s\n", strerror(errno));
		}
		ptl_mutex_lock(&dev->lock, __func__);
	}

	bximsg_revents(dev->iface, pfds);

	timo_update();
}

void
swptl_progress(int timeout)
{
	struct swptl_dev *dev;
	struct pollfd pfds[SWPTL_MAX_FDS];
	unsigned int dev_nfds[SWPTL_DEV_NMAX];
	int nfds = 0;
	int rc;
	int i;

	i = 0;
	nfds = 0;
	for (dev = swptl_dev_list; dev != NULL; dev = dev->next) {
		ptl_mutex_lock(&dev->lock, __func__);
		dev_nfds[i] = bximsg_pollfd(dev->iface, &pfds[nfds]);
		ptl_mutex_unlock(&dev->lock, __func__);

		nfds += dev_nfds[i];
		i++;
	}

	if (nfds > 0) {
		rc = poll(pfds, nfds, timeout);
		if (rc < 0) {
			if (errno == EINTR)
				return;
			ptl_panic("poll: %s\n", strerror(errno));
		}
	}

	i = 0;
	nfds = 0;
	for (dev = swptl_dev_list; dev != NULL; dev = dev->next) {
		ptl_mutex_lock(&dev->lock, __func__);
		bximsg_revents(dev->iface, &pfds[nfds]);
		ptl_mutex_unlock(&dev->lock, __func__);

		nfds += dev_nfds[i];
		i++;
	}

	timo_update();
}

void
swptl_sigusr1(int s)
{	
	swptl_dump_pending = 1;
}

int
swptl_func_ni_init(ptl_interface_t nic_iface,
    unsigned int flags,
    ptl_pid_t pid,
    const struct ptl_ni_limits *desired_lim,
    struct ptl_ni_limits *actual_lim,
    ptl_handle_ni_t *retnih)
{
	unsigned int vc;
	struct swptl_dev *dev;
	struct swptl_ni *ni;
	unsigned int ntrig, nme, nunex;

	switch (flags) {
		case PTL_NI_PHYSICAL | PTL_NI_MATCHING:
		case PTL_NI_PHYSICAL | PTL_NI_NO_MATCHING:
		case PTL_NI_LOGICAL | PTL_NI_MATCHING:
		case PTL_NI_LOGICAL | PTL_NI_NO_MATCHING:
			break;
		default:
			LOG("%s:: bad options\n", __func__);
			return PTL_ARG_INVALID;
	}

	/* we use these bits as index in swptl_dev->nis[] */
	vc = flags & (PTL_NI_PHYSICAL | PTL_NI_MATCHING);

	ptl_mutex_lock(&swptl_init_mutex, __func__);

	/* portals requires the same ni to be reused if called twice */
	if (swptl_dev_list == NULL) {
		dev = swptl_dev_new(nic_iface, pid);
		if (dev == NULL) {
			ptl_mutex_unlock(&swptl_init_mutex, __func__);
			return PTL_FAIL;
		}
	} else {
		/*
		 * portals spec says, only one pid per process.
		 *
		 * XXX: add the "don't reuse pid" logic here.
		 */
		dev = swptl_dev_list;
		if (pid != PTL_PID_ANY && dev->pid != pid) {
			ptl_mutex_unlock(&swptl_init_mutex, __func__);
			return PTL_PID_IN_USE;
		}
	}

	ni = dev->nis[vc];
	if (ni != NULL) {
		ni->initcnt++;
	} else {
		ni = xmalloc(sizeof(struct swptl_ni), "swptl_ni");
		if (desired_lim != NULL) {
			ntrig = desired_lim->max_triggered_ops;
			if (ntrig > SWPTL_TRIG_MAX)
				ntrig = SWPTL_TRIG_MAX;
			nme = desired_lim->max_entries;
			if (nme > SWPTL_ME_MAX)
				nme = SWPTL_ME_MAX;
			nunex = desired_lim->max_unexpected_headers;
			if (nunex > SWPTL_UNEX_MAX)
				nunex = SWPTL_UNEX_MAX;
		} else {
			ntrig = 0x8000;
			nunex = 0x8000;
			nme = 0x8000;
		}
		if (!swptl_ni_init(ni, vc, ntrig, nme, nunex)) {
			xfree(ni);
			ptl_mutex_unlock(&swptl_init_mutex, __func__);
			return PTL_FAIL;
		}
		ni->dev = dev;
		dev->nis[vc] = ni;
		ni->initcnt = 1;
	}

	if (actual_lim != NULL) {
		actual_lim->features = PTL_TARGET_BIND_INACCESSIBLE |
		    PTL_TOTAL_DATA_ORDERING;
		actual_lim->max_entries = ni->nme;
		actual_lim->max_unexpected_headers = ni->nunex;
		actual_lim->max_mds = INT_MAX;
		actual_lim->max_cts = INT_MAX;
		actual_lim->max_eqs = INT_MAX;
		actual_lim->max_pt_index = SWPTL_NPTE - 1;
		actual_lim->max_list_size = INT_MAX;
		actual_lim->max_triggered_ops = ni->ntrig;
		actual_lim->max_iovecs = INT_MAX;
		actual_lim->max_msg_size = INT_MAX;
		actual_lim->max_atomic_size = SWPTL_MAXATOMIC;
		actual_lim->max_fetch_atomic_size = SWPTL_MAXATOMIC;
		actual_lim->max_waw_ordered_size = SWPTL_MAXATOMIC;
		actual_lim->max_war_ordered_size = SWPTL_MAXATOMIC;
		actual_lim->max_volatile_size = SWPTL_MAXVOLATILE;
	}

	ptl_mutex_unlock(&swptl_init_mutex, __func__);

	retnih->handle = ni;
	retnih->incarnation = 0;
	return PTL_OK;
}

int
swptl_func_ni_fini(ptl_handle_ni_t nih)
{
	struct swptl_ni *ni = nih.handle;
	struct swptl_dev *dev = ni->dev;
	struct swptl_md *md;
	struct swptl_eq *eq;
	struct swptl_ct *ct;
	struct swptl_pte *pte;
	int i;

	ptl_mutex_lock(&swptl_init_mutex, __func__);

	if (--ni->initcnt > 0) {
		ptl_mutex_unlock(&swptl_init_mutex, __func__);
		return PTL_OK;
	}

	ptl_mutex_lock(&dev->lock, __func__);

	/* finalize transfers in progress */
	while (ni->rxops != NULL || ni->txops != NULL) {
		LOGN(0, "%s: rxops = %p, txops = %p\n",
		     __func__, ni->rxops, ni->txops);
		swptl_dev_progress(ni->dev, 1);
	}

	while ((md = ni->md_list) != NULL) {
		swptl_md_done(md);
		xfree(md);
	}
	for (i = 0; i < SWPTL_NPTE; i++) {
		pte = ni->pte[i];
		if (!pte)
			continue;
		swptl_pte_cleanup(pte);
		ni->pte[i] = NULL;
		xfree(pte);
	}
	while ((eq = ni->eq_list) != NULL) {
		swptl_eq_done(eq);
		xfree(eq);
	}
	while ((ct = ni->ct_list) != NULL) {
		swptl_ct_done(ct);
		xfree(ct);
	}
	if (ni->map != NULL)
		xfree(ni->map);

	dev->nis[ni->vc] = NULL;

	ptl_mutex_unlock(&dev->lock, __func__);

	swptl_ni_done(ni);
	xfree(ni);

	for (i = 0; i < SWPTL_NI_COUNT; i++) {
		if (dev->nis[i] != NULL) {
			ptl_mutex_unlock(&swptl_init_mutex, __func__);
			return PTL_OK;
		}
	}

	swptl_dev_del(dev);

	ptl_mutex_unlock(&swptl_init_mutex, __func__);
	return PTL_OK;
}

int
swptl_func_libinit(void)
{
	int ret;
	struct sigaction sa;
#ifdef DEBUG
	char *debug;
#endif
	char *env;

	ptl_mutex_lock(&swptl_init_mutex, __func__);

	if (swptl_init_count++ == 0) {
		sigfillset(&sa.sa_mask);
		sa.sa_flags = SA_RESTART;
		sa.sa_handler = swptl_sigusr1;
		if (sigaction(SIGUSR1, &sa, NULL) < 0)
			ptl_panic("%s: sigaction failed\n", __func__);
#ifdef DEBUG
		debug = getenv("PORTALS4_DEBUG");
		if (debug)
			sscanf(debug, "%d", &swptl_verbose);
#endif

		env = ptl_getenv("PORTALS4_STATISTICS");
		if (env)
			sscanf(env, "%d", ptl_counters.print);

		env = ptl_getenv("PORTALS4_RDV_PUT");
		if (env)
			sscanf(env, "%u", &ptlbxi_rdv_put);

		timo_init();
		ret = bximsg_libinit();
		atomic_store_explicit(&swptl_aborting, false, memory_order_relaxed);
	} else {
		ret = PTL_OK;
	}

	ptl_mutex_unlock(&swptl_init_mutex, __func__);

	return ret;
}

void
swptl_func_libfini(void)
{
	struct swptl_dev *dev;
	struct swptl_ni *ni;
	struct sigaction sa;
	int i;

	ptl_mutex_lock(&swptl_init_mutex, __func__);

	if (--swptl_init_count > 0) {
		ptl_mutex_unlock(&swptl_init_mutex, __func__);
		return;
	}

	/*
	 * This is the last PtlFini() call, and the portals spec
	 * forbids further calls to any function, including PtlInit().
	 */
	ptl_mutex_unlock(&swptl_init_mutex, __func__);

	sigfillset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = SIG_DFL;
	if (sigaction(SIGUSR1, &sa, NULL) < 0)
		ptl_panic("%s: sigaction failed\n", __func__);

	while ((dev = swptl_dev_list) != NULL) {
		for (i = 0; i < SWPTL_NI_COUNT; i++) {
			ni = dev->nis[i];
			if (ni != NULL)
				swptl_func_ni_fini(((ptl_handle_any_t){ ni }));
		}
	}

	bximsg_libfini();
}

void
swptl_func_abort(void)
{
	atomic_store_explicit(&swptl_aborting, true, memory_order_relaxed);
	/* No need to wakeup anything here. */
}

int
swptl_func_setmemops(struct ptl_mem_ops *ops)
{
	return PTL_OK;
}

int
swptl_func_activate_add(void (*cb)(void *, unsigned int, int),
    void *arg, struct ptl_activate_hook **rh)
{
	return PTL_FAIL;
}

int
swptl_func_activate_rm(struct ptl_activate_hook *h)
{
	return PTL_FAIL;
}

int
swptl_func_ni_handle(ptl_handle_any_t hdl, ptl_handle_ni_t *ret)
{
	if (hdl.handle == NULL)
		return PTL_ARG_INVALID;

	ret->handle = *(struct swptl_ni **)hdl.handle;
	ret->incarnation = 0;

	return PTL_OK;
}

int
swptl_func_ni_status(ptl_handle_ni_t nih, ptl_sr_index_t reg, ptl_sr_value_t *status)
{
	/* not implemented yet */
	return PTL_FAIL;
}

int
swptl_func_setmap(ptl_handle_ni_t nih, ptl_size_t size, const union ptl_process *map)
{
	struct swptl_ni *ni = nih.handle;
	void *p;

	p = xmalloc(size * sizeof(union ptl_process), "map");
	memcpy(p, map, size * sizeof(union ptl_process));

	ptl_mutex_lock(&ni->dev->lock, __func__);
	if (ni->map != NULL) {
		ptl_log("swptl_ni_setmap: can't change map, xfers in progress\n");
		ptl_mutex_unlock(&ni->dev->lock, __func__);
		xfree(p);
		return PTL_IN_USE;
	}
	ni->map = p;
	ni->mapsize = size;
	ptl_mutex_unlock(&ni->dev->lock, __func__);

	return PTL_OK;
}

int
swptl_func_getmap(ptl_handle_ni_t nih,
    ptl_size_t size, union ptl_process *map, ptl_size_t *retsize)
{
	struct swptl_ni *ni = nih.handle;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	if (ni->map == NULL) {
		ptl_log("swptl_ni_getmap: no map\n");
		ptl_mutex_unlock(&ni->dev->lock, __func__);
		return PTL_FAIL;
	}

	*retsize = ni->mapsize;
	if (size > ni->mapsize)
		size = ni->mapsize;
	memcpy(map, ni->map, size * sizeof(union ptl_process));

	ptl_mutex_unlock(&ni->dev->lock, __func__);

	return PTL_OK;
}

int
swptl_func_pte_alloc(ptl_handle_ni_t nih,
    unsigned int opt,
    ptl_handle_eq_t eqh,
    ptl_index_t index,
    ptl_index_t *retval)
{
	struct swptl_ni *ni = nih.handle;
	struct swptl_pte *pte;
	int n;

	pte = xmalloc(sizeof(struct swptl_pte), "swptl_pte");
	ptl_mutex_lock(&ni->dev->lock, __func__);
	n = swptl_pte_init(pte, ni, index, opt,
			   (eqh.handle != PTL_EQ_NONE.handle) ?
			   eqh.handle : NULL);
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	if (n < 0) {
		xfree(pte);
		return  PTL_NO_SPACE;
	}
	*retval = n;
	return PTL_OK;
}

int
swptl_func_pte_free(ptl_handle_ni_t nih, ptl_pt_index_t index)
{
	struct swptl_ni *ni = nih.handle;
	struct swptl_pte *pte = ni->pte[index];
	int rc;

	ptl_mutex_lock(&ni->dev->lock, __func__);

	/* do what bxi nic does */
	swptl_pte_cleanup(pte);

	rc = swptl_pte_done(pte);
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	if (!rc)
		return PTL_IN_USE;
	xfree(pte);
	return PTL_OK;
}

int
swptl_func_pte_enable(ptl_handle_ni_t nih, ptl_pt_index_t index, int enable, int nbio)
{
	struct swptl_ni *ni = nih.handle;
	struct swptl_pte *pte = ni->pte[index];

	ptl_mutex_lock(&ni->dev->lock, __func__);

	if (enable && (pte->opt & PTL_PT_FLOWCTRL) && pte->eq && !pte->ev) {
		if (pool_isempty(&pte->eq->ev_pool)) {
			LOGN(2, "%s: %d: out of eq space\n",
				__func__, index);
			ptl_mutex_unlock(&ni->dev->lock, __func__);
			return PTL_FAIL;
		}
		pte->ev = pool_get(&pte->eq->ev_pool);
	}

	pte->enabled = enable;

	ptl_mutex_unlock(&ni->dev->lock, __func__);
	return PTL_OK;
}

int
swptl_func_getuid(ptl_handle_ni_t nih, ptl_uid_t *uid)
{
	struct swptl_ni *ni = nih.handle;

	*uid = ni->dev->uid;
	return PTL_OK;
}

int
swptl_func_getid(ptl_handle_ni_t nih, union ptl_process *id)
{
	struct swptl_ni *ni = nih.handle;
	int rank;

	if (SWPTL_ISPHYSICAL(ni->vc)) {
	        id->phys.nid = ni->dev->nid;
	        id->phys.pid = ni->dev->pid;
	} else {
		ptl_mutex_lock(&ni->dev->lock, __func__);
		rank = swptl_ni_p2l(ni, ni->dev->nid, ni->dev->pid);
		ptl_mutex_unlock(&ni->dev->lock, __func__);
		if (rank < 0)
			return PTL_FAIL;
		id->rank = rank;
	}
	return PTL_OK;
}

int
swptl_func_getphysid(ptl_handle_ni_t nih, union ptl_process *id)
{
	struct swptl_ni *ni = nih.handle;

	id->phys.nid = ni->dev->nid;
	id->phys.pid = ni->dev->pid;
	return PTL_OK;
}

int
swptl_func_gethwid(ptl_handle_ni_t nih, uint64_t *hwid, uint64_t *capabilities)
{
	/* not implemented yet */
	return PTL_FAIL;
}

int
swptl_func_md_bind(ptl_handle_ni_t nih, const struct ptl_md *mdpar, ptl_handle_md_t *retmd)
{
	struct swptl_ni *ni = nih.handle;
	struct swptl_md *md;

	md = xmalloc(sizeof(struct swptl_md), "swptl_md");
	ptl_mutex_lock(&ni->dev->lock, __func__);
	swptl_md_init(md, ni, mdpar->start, mdpar->length,
		      mdpar->eq_handle.handle != PTL_EQ_NONE.handle ?
		      mdpar->eq_handle.handle : NULL,
		      mdpar->ct_handle.handle != PTL_CT_NONE.handle ?
		      mdpar->ct_handle.handle : NULL,
		      mdpar->options);
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	retmd->handle = md;
	retmd->incarnation = 0;
	return PTL_OK;
}

int
swptl_func_md_release(ptl_handle_md_t mdh)
{
	struct swptl_md *md = mdh.handle;
	struct swptl_ni *ni = md->ni;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	swptl_md_done(mdh.handle);
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	xfree(mdh.handle);
	return PTL_OK;
}

int
swptl_func_append(ptl_handle_ni_t nih,
    ptl_index_t index,
    const struct ptl_me *mepar,
    ptl_list_t list,
    void *uptr,
    ptl_handle_me_t *mehret,
    int nbio)
{
	struct swptl_ni *ni = nih.handle;
	struct swptl_me *me;
	unsigned int nid;
	unsigned int pid;
	ptl_match_bits_t bits;
	ptl_match_bits_t mask;

	if (!(mepar->options & PTL_ME_MANAGE_LOCAL) &&
	    (mepar->options & (PTL_ME_LOCAL_INC_UH_RLENGTH |
			       PTL_ME_UH_LOCAL_OFFSET_INC_MANIPULATED |
			       PTL_ME_MANAGE_LOCAL_STOP_IF_UH))) {
		LOG("%s: increment local offset and stop if UH should be used with manage local\n",
		    __func__);
		return PTL_ARG_INVALID;
	}

	if (list == PTL_PRIORITY_LIST &&
	    (mepar->options & (PTL_ME_OV_RDV_PUT_ONLY |
			       PTL_ME_OV_RDV_PUT_DISABLE))) {
		LOG("%s: options for RDV PUT are only available for overflow list\n", __func__);
		return PTL_ARG_INVALID;
	}

	ptl_mutex_lock(&ni->dev->lock, __func__);

	if (SWPTL_ISMATCHING(ni->vc)) {
		if (SWPTL_ISPHYSICAL(ni->vc)) {
			nid = mepar->match_id.phys.nid;
			pid = mepar->match_id.phys.pid;
		} else {
			if (!swptl_ni_l2p(ni, mepar->match_id.rank,
					  &nid, &pid)) {
				ptl_mutex_unlock(&ni->dev->lock, __func__);
				return PTL_FAIL;
			}
		}
		bits = mepar->match_bits;
		mask = mepar->ignore_bits;
	}
	else {
		nid = PTL_NID_ANY;
		pid = PTL_PID_ANY;
		bits = 0;
		mask = ~0ULL;
	}

	if (pool_isempty(&ni->me_pool)) {
		ptl_mutex_unlock(&ni->dev->lock, __func__);
		return PTL_NO_SPACE;
	}
	me = pool_get(&ni->me_pool);
	me->refs = 1;
	me->ni = ni;
	swptl_me_add(me, ni, ni->pte[index], mepar->start, mepar->length,
		     mepar->ct_handle.handle != PTL_CT_NONE.handle ?
		     mepar->ct_handle.handle : NULL,
		     mepar->uid, mepar->options, nid, pid, bits, mask,
		     mepar->min_free, list, uptr);
	mehret->handle = me;
	mehret->incarnation = 0;

	/* if me is linked, it will have an extra ref */
	swptl_meunref(ni, me, 0);

	ptl_mutex_unlock(&ni->dev->lock, __func__);
	return PTL_OK;
}

int
swptl_func_unlink(ptl_handle_me_t meh)
{
	struct swptl_me *me = meh.handle;
	struct swptl_ni *ni = me->pte->ni;
	int rc;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	rc = swptl_me_rm(me);
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	return rc;
}

int
swptl_func_search(ptl_handle_ni_t nih,
    ptl_pt_index_t index,
    const ptl_le_t *mepar,
    ptl_search_op_t sop,
    void *uptr,
    int nbio)
{
	struct swptl_ni *ni = nih.handle;
	unsigned int nid, pid;
	ptl_match_bits_t bits;
	ptl_match_bits_t mask;

	ptl_mutex_lock(&ni->dev->lock, __func__);

	if (SWPTL_ISMATCHING(ni->vc)) {
		if (SWPTL_ISPHYSICAL(ni->vc)) {
			nid = mepar->match_id.phys.nid;
			pid = mepar->match_id.phys.pid;
		} else {
			if (!swptl_ni_l2p(ni, mepar->match_id.rank,
					  &nid, &pid)) {
				ptl_mutex_unlock(&ni->dev->lock, __func__);
				return PTL_FAIL;
			}
		}
		bits = mepar->match_bits;
		mask = mepar->ignore_bits;
	}
	else {
		nid = PTL_NID_ANY;
		pid = PTL_PID_ANY;
		bits = 0;
		mask = ~0ULL;
	}

	swptl_pte_search(ni->pte[index], sop,
			 mepar->ct_handle.handle != PTL_CT_NONE.handle ?
			 mepar->ct_handle.handle : NULL,
			 mepar->uid, mepar->options, nid, pid, bits, mask,
			 uptr);
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	return PTL_OK;
}

int
swptl_func_eq_alloc(ptl_handle_ni_t nih,
    ptl_size_t count,
    ptl_handle_eq_t *reteq,
    void (*cb)(void *, ptl_handle_eq_t),
    void *arg, int hint)
{
	struct swptl_ni *ni = nih.handle;
	struct swptl_eq *eq;

	if (cb != NULL)
		return PTL_FAIL; /* not implemented */

	eq = xmalloc(sizeof(struct swptl_eq), "swptl_eq");
	ptl_mutex_lock(&ni->dev->lock, __func__);
	swptl_eq_init(eq, ni, count);
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	reteq->handle = eq;
	reteq->incarnation = 0;
	return PTL_OK;
}

int
swptl_func_eq_free(ptl_handle_eq_t eqh)
{
	struct swptl_eq *eq = eqh.handle;
	struct swptl_ni *ni = eq->ni;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	swptl_eq_done(eqh.handle);
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	xfree(eqh.handle);
	return PTL_OK;
}

int
swptl_func_eq_get(ptl_handle_eq_t eqh, struct ptl_event *rev)
{
	struct swptl_eq *eq = eqh.handle;
	struct swptl_ni *ni = eq->ni;
	int rc;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	rc = swptl_eq_getev(eq, rev);
	if (rc == PTL_EQ_EMPTY)
		swptl_dev_progress(eq->ni->dev, 0);
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	return rc;
}

void
swptl_setflag_cb(void *arg)
{
	*(int *)arg = 1;
}

int
swptl_func_eq_poll(const ptl_handle_eq_t *eqhlist,
    unsigned int size, long timeout,
    struct ptl_event *rev,
    unsigned int *rwhich)
{
	struct swptl_eq *eq;
	struct swptl_ni *ni;
	unsigned int i;
	struct timo timo;
	int expired = 0;
	int rc;

	if (timeout != PTL_TIME_FOREVER && timeout > 0) {
		timo_set(&timo, swptl_setflag_cb, &expired);
		timo_add(&timo, 1000 * timeout);
	}
	while (!expired) {
		for (i = 0; i < size; i++) {
			eq = eqhlist[i].handle;
			ni = eq->ni;
			ptl_mutex_lock(&ni->dev->lock, __func__);
			if (atomic_load_explicit(&swptl_aborting, memory_order_relaxed))
				rc = PTL_ABORTED;
			else
				rc = swptl_eq_getev(eq, rev);
			ptl_mutex_unlock(&ni->dev->lock, __func__);
			if (rc != PTL_EQ_EMPTY) {
				if (rwhich)
					*rwhich = i;
				if (timeout != PTL_TIME_FOREVER && timeout > 0)
					timo_del(&timo);
				return rc;
			}
		}

		if (size > 0) {
			eq = eqhlist[0].handle;
			ptl_mutex_lock(&ni->dev->lock, __func__);
			swptl_dev_progress(eq->ni->dev, 1);
			ptl_mutex_unlock(&ni->dev->lock, __func__);
		} else
			swptl_progress(1);

		if (timeout == 0)
			break;
	}
	return PTL_EQ_EMPTY;
}

int
swptl_func_ct_alloc(ptl_handle_ni_t nih, ptl_handle_ct_t *retct)
{
	struct swptl_ni *ni = nih.handle;
	struct swptl_ct *ct;

	ct = xmalloc(sizeof(struct swptl_ct), "swptl_ct");
	ptl_mutex_lock(&ni->dev->lock, __func__);
	swptl_ct_init(ct, ni);
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	retct->handle = ct;
	retct->incarnation = 0;
	return PTL_OK;
}

int
swptl_func_ct_free(ptl_handle_ct_t cth)
{
	struct swptl_ct *ct = cth.handle;
	struct swptl_ni *ni = ct->ni;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	swptl_ct_done(cth.handle);
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	xfree(cth.handle);
	return PTL_OK;
}

int
swptl_func_ct_cancel(ptl_handle_ct_t cth)
{
	struct swptl_ct *ct = cth.handle;
	struct swptl_ni *ni = ct->ni;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	swptl_trigdel(cth.handle);
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	return PTL_OK;
}

int
swptl_func_ct_get(ptl_handle_ct_t cth, struct ptl_ct_event *rev)
{
	struct swptl_ct *ct = cth.handle;
	struct swptl_ni *ni = ct->ni;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	swptl_dev_progress(ct->ni->dev, 0);
	swptl_ct_get(cth.handle, rev);
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	return PTL_OK;
}

int
swptl_func_ct_poll(const ptl_handle_ct_t *cthlist,
    const ptl_size_t *test,
    unsigned int size,
    long timeout,
    struct ptl_ct_event *rev,
    unsigned int *rwhich)
{
	unsigned int i;
	struct timo timo;
	int expired = 0;
	struct ptl_ct_event val;
	struct swptl_ct *ct;
	struct swptl_ni *ni;

	if (timeout != PTL_TIME_FOREVER && timeout > 0) {
		timo_set(&timo, swptl_setflag_cb, &expired);
		timo_add(&timo, 1000 * timeout);
	}
	while (!expired) {
		for (i = 0; i < size; i++) {
			ct = cthlist[i].handle;
			ni = ct->ni;
			ptl_mutex_lock(&ni->dev->lock, __func__);
			if (atomic_load_explicit(&swptl_aborting, memory_order_relaxed)) {
				ptl_mutex_unlock(&ni->dev->lock, __func__);
				if (timeout != PTL_TIME_FOREVER && timeout > 0)
					timo_del(&timo);
				return PTL_ABORTED;
			}
			swptl_ct_get(ct, &val);
			ptl_mutex_unlock(&ni->dev->lock, __func__);
			if (val.success >= test[i] || val.failure > 0) {
				if (rwhich)
					*rwhich = i;
				if (timeout != PTL_TIME_FOREVER && timeout > 0)
					timo_del(&timo);
				if (rev)
					*rev = val;
				return PTL_OK;
			}
		}

		if (size > 0) {
			ct = cthlist[0].handle;
			ni = ct->ni;
			ptl_mutex_lock(&ni->dev->lock, __func__);
			swptl_dev_progress(ni->dev, 1);
			ptl_mutex_unlock(&ni->dev->lock, __func__);
		} else
			swptl_progress(1);

		if (timeout == 0)
			break;
	}
	return PTL_CT_NONE_REACHED;
}

int
swptl_func_ct_op(ptl_handle_ct_t cth, struct ptl_ct_event delta, int inc)
{
	struct swptl_ct *ct = cth.handle;
	struct swptl_ni *ni = ct->ni;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	swptl_ct_cmd(inc ? SWPTL_CTINC : SWPTL_CTSET, cth.handle, delta.success,
		     delta.failure);
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	return PTL_OK;
}

int
swptl_func_put(ptl_handle_md_t mdh,
    ptl_size_t loffs,
    ptl_size_t len,
    int ack,
    union ptl_process dest,
    ptl_pt_index_t index,
    ptl_match_bits_t bits,
    ptl_size_t roffs,
    void *uptr,
    ptl_hdr_data_t hdr,
    int nbio)
{
	struct swptl_md *put_md = mdh.handle;
	struct swptl_ni *ni = put_md->ni;
	int rc;

	ptl_mutex_lock(&ni->dev->lock, __func__);

	if ((put_md->opt & PTL_MD_UNRELIABLE) && ack != PTL_NO_ACK_REQ) {
		LOG("%s: Only the put operation with no acknowledgment is supported in unreliable communications\n",
			__func__);
		ptl_mutex_unlock(&ni->dev->lock, __func__);
		return PTL_ARG_INVALID;
	}

	rc = swptl_cmd(SWPTL_PUT, ni,
		       NULL,		/* get_md */
		       0,		/* get_mdoffs */
		       mdh.handle,	/* put_md */
		       loffs,		/* put_mdoffs */
		       len,		/* len */
		       dest,		/* dest */
		       index,		/* pte */
		       bits,		/* bits */
		       roffs,		/* meoffs */
		       uptr,		/* uptr */
		       hdr,		/* hdr */
		       NULL,		/* const void *cst */
		       0,		/* aop */
		       0,		/* atype */
		       ack,		/* ack type */
		       NULL,		/* trig_ct */
		       0);		/* trig_thres */
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	if (!rc)
		return PTL_FAIL;

	return PTL_OK;
}

int
swptl_func_get(ptl_handle_md_t mdh,
    ptl_size_t loffs,
    ptl_size_t len,
    union ptl_process dest,
    ptl_pt_index_t index,
    ptl_match_bits_t bits,
    ptl_size_t roffs,
    void *uptr,
    int nbio)
{
	struct swptl_ni *ni = ((struct swptl_md *)mdh.handle)->ni;
	int rc;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	rc = swptl_cmd(SWPTL_GET, ni,
		       mdh.handle,	/* get_md */
		       loffs,		/* get_mdoffs */
		       NULL,		/* put_md */
		       0,		/* put_mdoffs */
		       len,		/* len */
		       dest,		/* dest */
		       index,		/* pte */
		       bits,		/* bits */
		       roffs,		/* meoffs */
		       uptr,		/* uptr */
		       0,		/* hdr */
		       NULL,		/* const void *cst */
		       0,		/* aop */
		       0,		/* atype */
		       0,		/* ack type */
		       NULL,		/* trig_ct */
		       0);		/* trig_thres */
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	if (!rc)
		return PTL_FAIL;

	return PTL_OK;
}

int
swptl_func_atomic(ptl_handle_md_t mdh,
    ptl_size_t loffs,
    ptl_size_t len,
    ptl_ack_req_t ack,
    ptl_process_t dest,
    ptl_pt_index_t index,
    ptl_match_bits_t bits,
    ptl_size_t roffs,
    void *uptr,
    ptl_hdr_data_t hdr,
    ptl_op_t aop,
    ptl_datatype_t atype,
    int nbio)
{
	struct swptl_ni *ni = ((struct swptl_md *)mdh.handle)->ni;
	int rc;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	rc = swptl_cmd(SWPTL_ATOMIC, ni,
		       NULL,		/* get_md */
		       0,		/* get_mdoffs */
		       mdh.handle,	/* put_md */
		       loffs,		/* put_mdoffs */
		       len,		/* len */
		       dest,		/* dest */
		       index,		/* pte */
		       bits,		/* bits */
		       roffs,		/* meoffs */
		       uptr,		/* uptr */
		       hdr,		/* hdr */
		       NULL,		/* const void *cst */
		       aop,		/* aop */
		       atype,		/* atype */
		       ack,		/* ack type */
		       NULL,		/* trig_ct */
		       0);		/* trig_thres */
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	if (!rc)
		return PTL_FAIL;
	return PTL_OK;
}

int
swptl_func_fetch(ptl_handle_md_t get_mdh,
    ptl_size_t get_loffs,
    ptl_handle_md_t put_mdh,
    ptl_size_t put_loffs,
    ptl_size_t len,
    ptl_process_t dest,
    ptl_pt_index_t index,
    ptl_match_bits_t bits,
    ptl_size_t roffs,
    void *uptr,
    ptl_hdr_data_t hdr,
    ptl_op_t aop,
    ptl_datatype_t atype,
    int nbio)
{
	struct swptl_ni *ni = ((struct swptl_md *)put_mdh.handle)->ni;
	int rc;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	rc = swptl_cmd(SWPTL_FETCH, ni,
		       get_mdh.handle,	/* get_md */
		       get_loffs,	/* get_mdoffs */
		       put_mdh.handle,	/* put_md */
		       put_loffs,	/* put_mdoffs */
		       len,		/* len */
		       dest,		/* dest */
		       index,		/* pte */
		       bits,		/* bits */
		       roffs,		/* meoffs */
		       uptr,		/* uptr */
		       hdr,		/* hdr */
		       NULL,		/* const void *cst */
		       aop,		/* aop */
		       atype,		/* atype */
		       0,		/* ack type */
		       NULL,		/* trig_ct */
		       0);		/* trig_thres */
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	if (!rc)
		return PTL_FAIL;

	return PTL_OK;
}

int
swptl_func_swap(ptl_handle_md_t get_mdh,
    ptl_size_t get_loffs,
    ptl_handle_md_t put_mdh,
    ptl_size_t put_loffs,
    ptl_size_t len,
    ptl_process_t dest,
    ptl_pt_index_t index,
    ptl_match_bits_t bits,
    ptl_size_t roffs,
    void *uptr,
    ptl_hdr_data_t hdr,
    const void *cst,
    ptl_op_t aop,
    ptl_datatype_t atype,
    int nbio)
{
	struct swptl_ni *ni = ((struct swptl_md *)put_mdh.handle)->ni;
	int rc;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	rc = swptl_cmd(SWPTL_SWAP, ni,
		       get_mdh.handle,	/* get_md */
		       get_loffs,	/* get_mdoffs */
		       put_mdh.handle,	/* put_md */
		       put_loffs,	/* put_mdoffs */
		       len,		/* len */
		       dest,		/* dest */
		       index,		/* pte */
		       bits,		/* bits */
		       roffs,		/* meoffs */
		       uptr,		/* uptr */
		       hdr,		/* hdr */
		       cst,		/* const void *cst */
		       aop,		/* aop */
		       atype,		/* atype */
		       0,		/* ack type */
		       NULL,		/* trig_ct */
		       0);		/* trig_thres */
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	if (!rc)
		return PTL_FAIL;
	return PTL_OK;
}

int
swptl_func_atsync(void)
{
	return PTL_OK;
}

int
swptl_func_niatsync(ptl_handle_ni_t nih)
{
	return PTL_OK;
}

int
swptl_func_trigput(ptl_handle_md_t mdh,
    ptl_size_t loffs,
    ptl_size_t len,
    int ack,
    union ptl_process dest,
    ptl_pt_index_t index,
    ptl_match_bits_t bits,
    ptl_size_t roffs,
    void *uptr,
    ptl_hdr_data_t hdr,
    ptl_handle_ct_t cth,
    ptl_size_t thres,
    int nbio)
{
	struct swptl_md *put_md = mdh.handle;
	struct swptl_ni *ni = put_md->ni;
	int rc;

	ptl_mutex_lock(&ni->dev->lock, __func__);

	if ((put_md->opt & PTL_MD_UNRELIABLE) && ack != PTL_NO_ACK_REQ) {
		LOG("%s: Only the put operation with no acknowledgment is supported in unreliable communications\n",
			__func__);
		ptl_mutex_unlock(&ni->dev->lock, __func__);
		return PTL_ARG_INVALID;
	}

	rc = swptl_cmd(SWPTL_PUT, ni,
		       NULL,		/* get_md */
		       0,		/* get_mdoffs */
		       mdh.handle,	/* put_md */
		       loffs,		/* put_mdoffs */
		       len,		/* len */
		       dest,		/* dest */
		       index,		/* pte */
		       bits,		/* bits */
		       roffs,		/* meoffs */
		       uptr,		/* uptr */
		       hdr,		/* hdr */
		       NULL,		/* const void *cst */
		       0,		/* aop */
		       0,		/* atype */
		       ack,		/* ack type */
		       cth.handle,	/* trig_ct */
		       thres);		/* trig_thres */
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	if (!rc)
		return PTL_FAIL;

	return PTL_OK;
}

int
swptl_func_trigget(ptl_handle_md_t mdh,
    ptl_size_t loffs,
    ptl_size_t len,
    union ptl_process dest,
    ptl_pt_index_t index,
    ptl_match_bits_t bits,
    ptl_size_t roffs,
    void *uptr,
    ptl_handle_ct_t cth,
    ptl_size_t thres,
    int nbio)
{
	struct swptl_ni *ni = ((struct swptl_md *)mdh.handle)->ni;
	int rc;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	rc = swptl_cmd(SWPTL_GET, ni,
		       mdh.handle,	/* get_md */
		       loffs,		/* get_mdoffs */
		       NULL,		/* put_md */
		       0,		/* put_mdoffs */
		       len,		/* len */
		       dest,		/* dest */
		       index,		/* pte */
		       bits,		/* bits */
		       roffs,		/* meoffs */
		       uptr,		/* uptr */
		       0,		/* hdr */
		       NULL,		/* const void *cst */
		       0,		/* aop */
		       0,		/* atype */
		       0,		/* ack type */
		       cth.handle,	/* trig_ct */
		       thres);		/* trig_thres */
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	if (!rc)
		return PTL_FAIL;

	return PTL_OK;
}

int
swptl_func_trigatomic(ptl_handle_md_t mdh,
    ptl_size_t loffs,
    ptl_size_t len,
    ptl_ack_req_t ack,
    ptl_process_t dest,
    ptl_pt_index_t index,
    ptl_match_bits_t bits,
    ptl_size_t roffs,
    void *uptr,
    ptl_hdr_data_t hdr,
    ptl_op_t aop,
    ptl_datatype_t atype,
    ptl_handle_ct_t cth,
    ptl_size_t thres,
    int nbio)
{
	struct swptl_ni *ni = ((struct swptl_md *)mdh.handle)->ni;
	int rc;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	rc = swptl_cmd(SWPTL_ATOMIC, ni,
		       NULL,		/* get_md */
		       0,		/* get_mdoffs */
		       mdh.handle,	/* put_md */
		       loffs,		/* put_mdoffs */
		       len,		/* len */
		       dest,		/* dest */
		       index,		/* pte */
		       bits,		/* bits */
		       roffs,		/* meoffs */
		       uptr,		/* uptr */
		       hdr,		/* hdr */
		       NULL,		/* const void *cst */
		       aop,		/* aop */
		       atype,		/* atype */
		       ack,		/* ack type */
		       cth.handle,	/* trig_ct */
		       thres);		/* trig_thres */
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	if (!rc)
		return PTL_FAIL;

	return PTL_OK;
}

int
swptl_func_trigfetch(ptl_handle_md_t get_mdh,
    ptl_size_t get_loffs,
    ptl_handle_md_t put_mdh,
    ptl_size_t put_loffs,
    ptl_size_t len,
    ptl_process_t dest,
    ptl_pt_index_t index,
    ptl_match_bits_t bits,
    ptl_size_t roffs,
    void *uptr,
    ptl_hdr_data_t hdr,
    ptl_op_t aop,
    ptl_datatype_t atype,
    ptl_handle_ct_t cth,
    ptl_size_t thres,
    int nbio)
{
	struct swptl_ni *ni = ((struct swptl_md *)put_mdh.handle)->ni;
	int rc;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	rc = swptl_cmd(SWPTL_FETCH, ni,
		       get_mdh.handle,	/* get_md */
		       get_loffs,	/* get_mdoffs */
		       put_mdh.handle,	/* put_md */
		       put_loffs,	/* put_mdoffs */
		       len,		/* len */
		       dest,		/* dest */
		       index,		/* pte */
		       bits,		/* bits */
		       roffs,		/* meoffs */
		       uptr,		/* uptr */
		       hdr,		/* hdr */
		       NULL,		/* const void *cst */
		       aop,		/* aop */
		       atype,		/* atype */
		       0,		/* ack type */
		       cth.handle,	/* trig_ct */
		       thres);		/* trig_thres */
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	if (!rc)
		return PTL_FAIL;

	return PTL_OK;
}

int
swptl_func_trigswap(ptl_handle_md_t get_mdh,
    ptl_size_t get_loffs,
    ptl_handle_md_t put_mdh,
    ptl_size_t put_loffs,
    ptl_size_t len,
    ptl_process_t dest,
    ptl_pt_index_t index,
    ptl_match_bits_t bits,
    ptl_size_t roffs,
    void *uptr,
    ptl_hdr_data_t hdr,
    const void *cst,
    ptl_op_t aop,
    ptl_datatype_t atype,
    ptl_handle_ct_t cth,
    ptl_size_t thres,
    int nbio)
{
	struct swptl_ni *ni = ((struct swptl_md *)put_mdh.handle)->ni;
	int rc;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	rc = swptl_cmd(SWPTL_SWAP, ni,
		       get_mdh.handle,	/* get_md */
		       get_loffs,	/* get_mdoffs */
		       put_mdh.handle,	/* put_md */
		       put_loffs,	/* put_mdoffs */
		       len,		/* len */
		       dest,		/* dest */
		       index,		/* pte */
		       bits,		/* bits */
		       roffs,		/* meoffs */
		       uptr,		/* uptr */
		       hdr,		/* hdr */
		       cst,		/* const void *cst */
		       aop,		/* aop */
		       atype,		/* atype */
		       0,		/* ack type */
		       cth.handle,	/* trig_ct */
		       thres);		/* trig_thres */
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	if (!rc)
		return PTL_FAIL;

	return PTL_OK;
}

int
swptl_func_trigctop(ptl_handle_ct_t cth,
    struct ptl_ct_event delta,
    ptl_handle_ct_t trig_cth,
    ptl_size_t thres,
    int set,
    int nbio)
{
	struct swptl_ct *ct = cth.handle;
	struct swptl_ni *ni = ct->ni;
	struct swptl_trig *trig;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	trig = swptl_trigadd(trig_cth.handle, thres);
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	if (trig == NULL) {
		ptl_log("couldn't get trig op\n");
		return PTL_NO_SPACE;
	}
	trig->scope = SWPTL_TRIG_CTOP;
	trig->u.ctop.op = set ? SWPTL_CTSET : SWPTL_CTINC;
	trig->u.ctop.val.success = delta.success;
	trig->u.ctop.val.failure = delta.failure;
	trig->u.ctop.ct = cth.handle;
	return PTL_OK;
}

int
swptl_func_nfds(ptl_handle_ni_t nih)
{
	struct swptl_ni *ni = nih.handle;
	int n;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	n = bximsg_nfds(ni->dev->iface);
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	return n;
}

int
swptl_func_pollfd(ptl_handle_ni_t nih, struct pollfd *pfds, int events)
{
	struct swptl_ni *ni = nih.handle;
	int n;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	n = bximsg_pollfd(ni->dev->iface, pfds);
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	return n;
}

int
swptl_func_revents(ptl_handle_ni_t nih, struct pollfd *pfds)
{
	struct swptl_ni *ni = nih.handle;
	int n;

	ptl_mutex_lock(&ni->dev->lock, __func__);
	n = bximsg_revents(ni->dev->iface, pfds);
	ptl_mutex_unlock(&ni->dev->lock, __func__);
	return n;
}

void
swptl_func_waitcompl(ptl_handle_ni_t nih,
		     unsigned int txcnt, unsigned int rxcnt)
{
	struct swptl_ni *ni = nih.handle;

	/* finalize transfers in progress */
	ptl_mutex_lock(&ni->dev->lock, __func__);
	while (txcnt > ni->txcnt || rxcnt > ni->rxcnt)
		swptl_dev_progress(ni->dev, 1);
	ptl_mutex_unlock(&ni->dev->lock, __func__);
}


struct ptl_ops swptl_ops __attribute__((visibility("default"))) = {
	swptl_func_libinit,
	swptl_func_libfini,
	swptl_func_abort,
	swptl_func_setmemops,
	swptl_func_activate_add,
	swptl_func_activate_rm,
	swptl_func_ni_init,
	swptl_func_ni_fini,
	swptl_func_ni_handle,
	swptl_func_ni_status,
	swptl_func_setmap,
	swptl_func_getmap,
	swptl_func_pte_alloc,
	swptl_func_pte_free,
	swptl_func_pte_enable,
	swptl_func_getuid,
	swptl_func_getid,
	swptl_func_getphysid,
	swptl_func_gethwid,
	swptl_func_md_bind,
	swptl_func_md_release,
	swptl_func_append,
	swptl_func_unlink,
	swptl_func_search,
	swptl_func_eq_alloc,
	swptl_func_eq_free,
	swptl_func_eq_get,
	swptl_func_eq_poll,
	swptl_func_ct_alloc,
	swptl_func_ct_free,
	swptl_func_ct_cancel,
	swptl_func_ct_get,
	swptl_func_ct_poll,
	swptl_func_ct_op,
	swptl_func_put,
	swptl_func_get,
	swptl_func_atomic,
	swptl_func_fetch,
	swptl_func_swap,
	swptl_func_atsync,
	swptl_func_niatsync,
	swptl_func_trigput,
	swptl_func_trigget,
	swptl_func_trigatomic,
	swptl_func_trigfetch,
	swptl_func_trigswap,
	swptl_func_trigctop,
	swptl_func_nfds,
	swptl_func_pollfd,
	swptl_func_revents,
	NULL,
	swptl_func_waitcompl,
};
