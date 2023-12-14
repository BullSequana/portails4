#ifndef SWPTL_H
#define SWPTL_H

#include <portals4.h>
#include <portals4_bxiext.h>
#include "pool.h"

#define SWPTL_PUT		0
#define SWPTL_GET		1
#define SWPTL_ATOMIC		2
#define SWPTL_FETCH		3
#define SWPTL_SWAP		4
#define SWPTL_CTSET		5
#define SWPTL_CTINC		6

#define SWPTL_OPINT		1
#define SWPTL_OPFLOAT		2
#define SWPTL_OPCOMPLEX		4

#define SWPTL_PID_MAX		4095
#define SWPTL_ICTX_COUNT	0x1000
#define SWPTL_TCTX_COUNT	0x1000
#define SWPTL_TRIG_MAX		0x100000
#define SWPTL_ME_MAX		0x100000
#define SWPTL_UNEX_MAX		0x100000

#define SWPTL_NI_COUNT		4

#define SWPTL_ISMATCHING(opt)  \
	(((opt) & (PTL_NI_MATCHING | PTL_NI_NO_MATCHING)) == PTL_NI_MATCHING)

#define SWPTL_ISPHYSICAL(opt)  \
	(((opt) & (PTL_NI_PHYSICAL | PTL_NI_LOGICAL)) == PTL_NI_PHYSICAL)

#define SWPTL_ISOVER(t) (			\
	(t) == PTL_EVENT_PUT_OVERFLOW ||	\
	(t) == PTL_EVENT_GET_OVERFLOW ||	\
	(t) == PTL_EVENT_ATOMIC_OVERFLOW ||	\
	(t) == PTL_EVENT_FETCH_ATOMIC_OVERFLOW)

#define SWPTL_ISCOMM(t) (			\
	(t) == PTL_EVENT_PUT ||			\
	(t) == PTL_EVENT_GET ||			\
	(t) == PTL_EVENT_ATOMIC ||		\
	(t) == PTL_EVENT_FETCH_ATOMIC)

#define SWPTL_ISFCTRL(t) (			\
	(t) == PTL_EVENT_PT_DISABLED)

#define SWPTL_ISUNLINK(t) (			\
	(t) == PTL_EVENT_AUTO_UNLINK ||		\
	(t) == PTL_EVENT_AUTO_FREE)

#define SWPTL_ISLINK(t) (			\
	(t) == PTL_EVENT_LINK)

#define SWPTL_ISPUT(c) (		\
	(c) == SWPTL_PUT ||		\
	(c) == SWPTL_ATOMIC ||		\
	(c) == SWPTL_FETCH ||		\
	(c) == SWPTL_SWAP)

#define SWPTL_ISGET(c) (		\
	(c) == SWPTL_GET ||		\
	(c) == SWPTL_FETCH ||		\
	(c) == SWPTL_SWAP)

#define SWPTL_ISCT(c)  (		\
	(c) == SWPTL_CTINC ||		\
	(c) == SWPTL_CTSET)

#define SWPTL_ISFLOAT(t) (		\
	(t) == PTL_FLOAT ||		\
	(t) == PTL_FLOAT_COMPLEX ||	\
	(t) == PTL_DOUBLE ||		\
	(t) == PTL_DOUBLE_COMPLEX ||	\
	(t) == PTL_LONG_DOUBLE	||	\
	(t) == PTL_LONG_DOUBLE_COMPLEX)

#define SWPTL_ISATOMIC(c) (		\
	(c) == SWPTL_ATOMIC ||		\
	(c) == SWPTL_FETCH ||		\
	(c) == SWPTL_SWAP)

#define SWPTL_ISVOLATILE(ctx)				\
	(((ctx)->put_md->opt & PTL_MD_VOLATILE) &&	\
	 ((ctx)->rlen <= SWPTL_MAXVOLATILE))

struct swptl_ev {
	struct poolent poolent;
	struct swptl_ev *next;
	ptl_event_t ev;
};

struct swptl_eq {
	struct swptl_ni *ni;
	struct swptl_eq *next;
	struct swptl_ev *ev_head, **ev_tail;
	struct pool ev_pool;
	int dropped;
};

struct swptl_ct {
	struct swptl_ni *ni;
	struct swptl_ct *next;
	ptl_ct_event_t val;
	struct swptl_trig *trig;
};

struct swptl_md {
	struct swptl_ni *ni;
	struct swptl_md *next;
	unsigned char *buf;
	size_t len;
	struct swptl_eq *eq;
	struct swptl_ct *ct;
	int niov;
	int opt;
	int refs;
};

struct swptl_pte {
	struct swptl_ni *ni;
	struct swptl_mequeue {
		struct swptl_me *head, **tail;
	} prio, over;
	struct swptl_unexqueue {
		struct swptl_unex *head, **tail;
	} unex;
	struct swptl_eq *eq;
	int index;
	int opt;
	int enabled;
	struct poolent *ev;
};

struct swptl_me {
	struct swptl_ni *ni;
	struct poolent poolent;
	struct swptl_me *next;
	struct swptl_pte *pte;
	int niov;
	void *buf;
	size_t len;
	size_t offs;			/* for managed local */
	struct swptl_ct *ct;
	ptl_uid_t uid;
	int opt;
	int nid, pid;
	unsigned long long bits;
	unsigned long long mask;
	size_t minfree;
	int list;
	void *uptr;
	int refs;			/* unex pointing us */
	int xfers;			/* transfers in progress */
};

struct swptl_unex {
	struct poolent poolent;
        struct swptl_unex *next;
	struct swptl_me *me;
	int type;
	int fail;
	int ready;			/* transfer not complete */
        uint32_t mlen, rlen;
        uint64_t hdr;
        int nid, pid, rank, uid;
        int aop, atype;
        unsigned char *base;
        uint64_t offs;
        unsigned long long bits;
};

struct swptl_dev {
	struct swptl_dev *next;
	struct swptl_ni *nis[SWPTL_NI_COUNT];
	struct bximsg_iface *iface;
	ptl_uid_t uid;
	int nid, pid;
	pthread_mutex_t lock;
};

struct swptl_ni {
	struct swptl_ni *ni;
	struct swptl_dev *dev;
	struct swptl_eq *eq_list;
	struct swptl_ct *ct_list;
	struct swptl_md *md_list;
#define SWPTL_NPTE	256
	struct swptl_pte *pte[SWPTL_NPTE];
	struct swptl_sodata *rxops, *txops;
	int vc;
	union ptl_process *map;
	size_t mapsize;
	int initcnt;
	struct swptl_trig *trig_pending;
	struct pool ictx_pool;
	struct pool tctx_pool;
	struct pool trig_pool;
	struct pool unex_pool;
	struct pool me_pool;
	unsigned int serial;
	unsigned int txcnt;
	unsigned int rxcnt;
	unsigned int nunex, ntrig, nme;
};

/*
 * query, as it passes on the network
 */
struct swptl_query {
	uint64_t hdr_data;
	uint64_t bits;
	uint64_t meoffs;
	uint32_t rlen;
	uint32_t serial;
	uint32_t cookie;
	uint8_t cmd;
	uint8_t	aop;
	uint8_t	atype;
	uint8_t ack;
	uint8_t pte;
	/* swapcst field must remain the last */
	uint8_t swapcst[32];
};

/*
 * reply, as it passes on the network
 */
struct swptl_reply {
	uint64_t meoffs;
	uint32_t mlen;
	uint32_t serial;
	uint32_t cookie;
	uint8_t list;
	uint8_t fail;
	uint8_t ack;
};

struct swptl_ictx {
	/* command args */
	int cmd;
	int aop;
	int atype;
	int ack;
	int pte;
	unsigned int serial;
	size_t rlen;
	uint64_t hdr_data;
	uint64_t bits;
	unsigned char swapcst[32];
	void *uptr;
	struct swptl_md *put_md, *get_md;
	size_t put_mdoffs, get_mdoffs;
	size_t query_meoffs;
	/* reply data */
	int fail;
	int list;
	size_t reply_meoffs;
	size_t mlen;
#define SWPTL_MAXVOLATILE	64
	unsigned char vol_data[64];
};

struct swptl_tctx {
	struct swptl_pte *pte;
	struct swptl_unex *unex;
	struct swptl_me *me;
	/* command args */
	int cmd;
	int aop;
	int atype;
	int ack;
	int uid;
	unsigned int serial;
	unsigned int cookie;
	size_t rlen;
	size_t query_meoffs;
	uint64_t hdr_data;
	uint64_t bits;
	unsigned char swapcst[32];
	/* reply event */
	int fail;
	int list;
	size_t reply_meoffs;
	size_t mlen;
#define SWPTL_MAXATOMIC	2048
	unsigned char atbuf[SWPTL_MAXATOMIC];
	unsigned char swapbuf[SWPTL_MAXATOMIC];
	struct poolent *evs;
};

struct swptl_sodata {
	struct poolent poolent;
	struct swptl_sodata *next;
	struct swptl_sodata *ni_next, **ni_prev;
	struct bximsg_conn *conn;
	struct swptl_sodata *ret_next;
	unsigned int seq;		/* seq number of first packet */
	unsigned int pkt_count;		/* number of packets if the message */
	unsigned int pkt_next;		/* packets already sent */
	unsigned int pkt_acked;		/* packets already acked */
	size_t hdrsize;			/* header size */
	size_t msgsize;			/* payload size */
	int init;			/* initiator ? */
	union {
		struct swptl_ictx ictx;
		struct swptl_tctx tctx;
	} u;

	/* The number of pending memcpy, only used when receiving data. */
	volatile uint64_t recv_pending_memcpy;

	/* Does use asynchronous memory copy for this message ? */
	int use_async_memcpy;
};

struct swptl_trig {
	struct poolent poolent;
	struct swptl_trig *next;
#define SWPTL_TRIG_TX		0
#define SWPTL_TRIG_CTOP		1
	int scope;
	union {
		struct {
			/* triggered put/get/... args */
			struct swptl_ictx ictx;
			int nid, pid;
		} tx;
		struct {
			/* triggered ctinc/ctset args */
			int op;
			struct swptl_ct *ct;
			ptl_ct_event_t val;
		} ctop;
	} u;
	ptl_size_t thres;
};

struct swptl_hdr {
#define SWPTL_QUERY	0			/* message is a query */
#define SWPTL_REPLY	1			/* message is a reply */
	uint8_t type;				/* one of above */
	uint8_t _pad[7];
	union {
		struct swptl_query query;
		struct swptl_reply reply;
	} u;
};

extern unsigned int ptlbxi_rdv_put;

void swptl_dump(struct swptl_ni *);
void swptl_progress(int timeout);
void swptl_dev_progress(struct swptl_dev *, int);
void swptl_ctx_dump(struct swptl_sodata *);
int swptl_ctx_log(struct swptl_sodata *f, int buf_size, char *buf);
int swptl_ni_l2p(struct swptl_ni *, int, unsigned int *, unsigned int *);
int swptl_ni_p2l(struct swptl_ni *, int, int);

#endif
