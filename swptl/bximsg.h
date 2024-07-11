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

#ifndef BXIMSG_H
#define BXIMSG_H

/*
 * swptl protocol layer
 *
 * This interface exposes ordered reliable message-based communication
 * to peer processes (aka (nid, pid) tuples). Messages have a small
 * fixed-size header and payload of unlimited size. The payload may
 * have any in-memory layout.
 *
 * The API is call-back based. Each message is represented as a
 * context (struct swptl_sodata), which is used to keep state of
 * the message transfer. The context is managed by the caller.
 *
 * To send a new message, the caller allocates and initializes
 * a new context and queues is using bximsg_enqueue() routine.
 * Later, when the interface is ready to send the call-backs are
 * invoket to build the header and return pointers to the
 * payload to transfer.
 *
 * When a message header is received, the corresponding call-backs are
 * invoked to process the header and to return poiters to the locations
 * where message chunks must be stored by the bximsg interface.
 *
 * There's no constraint of how the message is stored. It may be
 * fragmented in as many chucks as desired. The call-backs will be
 * used by the bximsg interface to iterate over them.
 */

#include "bxipkt.h"
#include "timo.h"
#include "utils.h"

#define VAL_ATOMIC_ADD(x, val) __atomic_fetch_add(&x, val, __ATOMIC_RELAXED)
#define VAL_ATOMIC_SUB(x, val) __atomic_fetch_sub(&x, val, __ATOMIC_RELAXED)

#define BXIMSG_VC_COUNT 4

struct swptl_sodata;

struct bximsg_ctx {
	struct bximsg_options opts;

	struct bxipkt_ctx pkt_ctx;
	struct timo_ctx *timo;
};

/*
 * The caller must provide the following call-backs during initialization
 */
struct bximsg_ops {
	/*
	 * snd_start is called whenever the bximsg interface is ready
	 * to start sending a new message. It must store the message
	 * header to the given location.  It has the following
	 * argumets:
	 *
	 *	arg:	pointer passed to bximsg_init()
	 *
	 *	ctx:	ctx pointer passed to bximsg_enqueue()
	 *
	 *	hdr:	pointer to location when the routine must write
	 *		the message header. Its size is swptl_hdr.
	 *
	 * The routine must set ctx->data and ctx->start to
	 * the first chuck of the message payload. It must return 1
	 * if there are more payload chucks, 0 if there's only one
	 * payload chuck. If the message has no payload, then
	 * ctx->size must be set to 0.
	 */
	void (*snd_start)(void *arg, struct swptl_sodata *ctx, void *hdr);

	/*
	 * snd_data() is called whenever by the bximsg layer to request
	 * payload to sent.
	 *
	 *	arg:	pointer passed to bximsg_init()
	 *
	 *	ctx:	ctx pointer passed to bximsg_enqueue()
	 *
	 *	offs:	offset withing the payload chunk
	 *
	 *	data:	location where is stored pointer to the
	 *		next data chunk
	 *
	 *	size:	location where is stored the size of the
	 *		next data chunk
	 *
	 * Returns 1 on success, 0 if there's no more data.
	 */
	void (*snd_data)(void *arg, struct swptl_sodata *, size_t, void **, size_t *);

	/*
	 * snd_end is called whenever transmission of the message completes.
	 * It has the following argumets:
	 *
	 *	arg:	pointer passed to bximsg_init()
	 *
	 *	ctx:	ctx pointer passed to bximsg_enqueue()
	 *
	 *	status:	flag indicating the status of the network transmission
	 */
	void (*snd_end)(void *arg, struct swptl_sodata *ctx, enum swptl_transport_status status);

	/*
	 * rcv_start() is called whenever the header of a new message was
	 * received. The arguments are:
	 *
	 *	arg:	pointer passed to bximsg_init()
	 *
	 *	hdr:	location of the header, of size of struct
	 *		swptl_hdr
	 *
	 *	avail:	packet size (used for debug only)
	 *
	 *	nid:	sender nid
	 *
	 *	pid:	sender pid
	 *
	 *	vc:	vc moving the message
	 *
	 *	uid:	sender effective user-id during bximsg_init()
	 *
	 *	pctx:	pointer to the context created for this message
	 *
	 *	size:	location where is stored the size of the
	 *		next data chunk
	 *
	 * If the message can't be processed immediately by temporary
	 * resource shortage, the routine must return 0, and it will
	 * be called later.
	 *
	 * The routine must parse the message header, determine the
	 * message context (or create a new context if necessary) and
	 * store it at the location pointed by pctx. The ctx->data
	 * ctx->size attributes must be set to a location were
	 * the first chunk of payload can be stored.
	 *
	 * If there's no payload (thus, no need for a context), the
	 * context must be set NULL and 1 must be returned.
	 */
	int (*rcv_start)(void *arg, void *hdr, int avail, int nid, int pid, int vc, int uid,
			 struct swptl_sodata **pctx, size_t *size);

	/*
	 * rcv_data() is called whenever a payload chuck is
	 * available. If there's no more space, it must return
	 * 0. Else, it must set data and size to the
	 * location where the next data chuck will be stored and
	 * return 1.
	 *
	 *	arg:	pointer passed to bximsg_init()
	 *
	 *	ctx:	ctx pointer set by rcv_start()
	 *
	 *	offs:	offset withing the payload chunk
	 *
	 *	data:	location where is stored pointer to the
	 *		next data chunk
	 *
	 *	size:	location where is stored the size of the
	 *		next data chunk
	 *
	 */
	void (*rcv_data)(void *arg, struct swptl_sodata *ctx, size_t, void **, size_t *);

	/*
	 * rcv_end is called whenever transmission of the message completes.
	 * It has the following argumets:
	 *
	 *	arg:	pointer passed to bximsg_init()
	 *
	 *	ctx:	ctx pointer passed to rcv_start() call-back
	 *
	 *	status:	status code for the message transport
	 */
	void (*rcv_end)(void *arg, struct swptl_sodata *ctx, enum swptl_transport_status status);

	void (*conn_err)(void *arg, struct bximsg_conn *conn);
};

#define BXIMSG_SND_START_NB 0
#define BXIMSG_SND_DATA_NB 1
#define BXIMSG_SND_END_NB 2
#define BXIMSG_RCV_START_SUCCESS_NB 3
#define BXIMSG_RCV_START_ERROR_NB 4
#define BXIMSG_RCV_DATA_NB 5
#define BXIMSG_RCV_END_NB 6
#define BXIMSG_IN_PKT_NB 7
#define BXIMSG_OUT_MSG_NB 8
#define BXIMSG_OUT_PKT_NB 9
#define BXIMSG_OUT_PKT_ERROR_NB 10
#define BXIMSG_OUT_INLINE_PKT_NB 11
#define BXIMSG_OUT_INLINE_PKT_ERROR_NB 12
#define BXIMSG_RTX_CALL_NB 13
#define BXIMSG_RTX_PKT_NB 14
#define BXIMSG_RTX_MAX_RETRIES_NB 15
#define BXIMSG_GET_BUF_ERROR_NB 16
#define BXIMSG_IN_PKT_DUPLICATES 17
#define BXIMSG_MAX_STATS 18 /* Should be the last one */

struct bximsg_conn {
	struct bximsg_conn *hnext; /* next on hash list */

	struct bximsg_conn *qnext; /* next on send queue */
	struct bximsg_conn **qprev; /* previous on send queue */

	struct swptl_sodata *send_qhead, **send_qtail; /* send queue */

	struct swptl_sodata *recv_ctx; /* receive context */

	struct bximsg_iface *iface; /* owner */

	int onqueue;

	/* seq. num of next message queued */
	uint16_t msg_seq;

	/* seq. num. of next packet that will be sent */
	uint16_t send_seq;

	/* seq. num. of first unacked packed we sent */
	uint16_t send_ack;

	/* seq. num. of next packet we'll receive */
	uint16_t recv_seq;

	/* seq. num. of next un-acked packet */
	uint16_t recv_ack;

	/* packet retransmit buffers */
	struct swptl_sodata *ret_qhead, **ret_qtail;

	/* retransmit timeout */
	struct timo ret_timo;

	/* num. transmit retries */
	unsigned long retries;

	/* stats */
	unsigned long stats[BXIMSG_MAX_STATS];

	/* read-only data */
	int nid, pid, rank; /* peer id */

	/* virtual circuit number */
	int vc;

	/* in seq number synchronization handshake */
	int synchronizing;
	int peer_synchronizing;
};

#ifdef DEBUG
extern int bximsg_debug;
#endif

int bximsg_libinit(struct bximsg_options *opts, struct bxipkt_options *pkt_opts,
		   struct timo_ctx *timo, struct bximsg_ctx *ctx);
void bximsg_libfini(struct bximsg_ctx *ctx);

/*
 * Create a bximsg interface and return a pointer to it.
 *
 *	arg:	pointer to pass as first argument to input and output
 *		call-backs
 *
 *	nic_iface: desired nic interface (0 for bxi0)
 *
 *	uid:    user id
 *
 *	pid:	desired pid or 0 to dynamically allocate one
 *
 *	ops:    call-backs invoked to load and store messages
 *
 *	rnid: 	pointer to location where the local nid is stored
 *
 *	rpid:	pointer to location where the local pid is stored
 *
 */
struct bximsg_iface *bximsg_init(struct bximsg_ctx *ctx, void *arg, struct bximsg_ops *ops,
				 int nic_iface, int uid, int pid, int *rnid, int *rpid);

/*
 * Destroy the given interface created by bximsg_init().
 */
void bximsg_done(struct bximsg_iface *);

/*
 * Try to to send and receive.
 */
int bximsg_progress(struct bximsg_iface *);

/*
 * Return a pointer to the connection structure of the given pid.
 *
 *	iface:	pointer returned by bximsg_init()
 *
 *	nid:	peer nid
 *
 *	pid:	peer pid
 *
 * if the connection doesn't exist, it's established.
 */
struct bximsg_conn *bximsg_getconn(struct bximsg_iface *, int, int, int);

/*
 * Enqueue a new message to send.
 *
 *	iface:	pointer returned by bximsg_init()
 *
 *	ctx:    context of the message. The ctx->conn field must be set
 *		to the connection structure returned by
 *		bximsg_getconn() routine. A context may be reused for
 *		multiple messages (ex PUT query then ACK reply), so
 *		bximsg_getconn() needs to be called only for the first
 *		one.
 *
 *	size:	payload size
 *
 */
void bximsg_enqueue(struct bximsg_iface *, struct swptl_sodata *, size_t);

/*
 * Dump the state of the bximsg interface
 */
void bximsg_dump(struct bximsg_iface *);

int bximsg_nfds(struct bximsg_iface *iface);

int bximsg_pollfd(struct bximsg_iface *iface, struct pollfd *pfds);

int bximsg_revents(struct bximsg_iface *iface, struct pollfd *pfds);

#endif
