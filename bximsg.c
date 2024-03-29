#include <limits.h>
#include <stdint.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "swptl.h"
#include "bximsg.h"
#include "bxipkt.h"
#include "utils.h"
#include "bximsg_wthr.h"
#include "ptl_log.h"

/* In microseconds */
#define BXIMSG_TX_TIMEOUT 2000
#define BXIMSG_TX_TIMEOUT_MAX 1000000
#define BXIMSG_TX_NET_TIMEOUT 20000
#define BXIMSG_TX_NET_TIMEOUT_MAX 10000000
#define BXIMSG_MAX_RETRIES 30
#define BXIMSG_NACK_MAX 10
#define BXIMSG_NBUFS 32
#define BXIMSG_HASHSIZE 1024
#define BXIMSG_HASH(nid, pid, vc)                                                                  \
	(((unsigned int)(nid) + 31 * (unsigned int)(pid) + 5 * (unsigned int)(vc)) %               \
	 BXIMSG_HASHSIZE)

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#ifdef DEBUG
/*
 * log with 'ptl_log' and the given debug level
 */
#define LOGN(n, ...)                                                                               \
	do {                                                                                       \
		if (bximsg_debug >= (n))                                                           \
			ptl_log(__VA_ARGS__);                                                      \
	} while (0)

#define LOG(...) LOGN(1, __VA_ARGS__);
#else
#define LOGN(n, ...)                                                                               \
	do {                                                                                       \
	} while (0)
#define LOG(...)                                                                                   \
	do {                                                                                       \
	} while (0)
#endif

struct bximsg_iface {
	struct bximsg_ctx *ctx;

	struct bxipkt_iface *pktif;

	/*
	 * Hash-list with all connections. The container must be such
	 * that that given a (nid, pid) the lookup of the connection
	 * structure is fast.
	 */
	struct bximsg_conn *bximsg_connlist[BXIMSG_HASHSIZE];

	/*
	 * List of connections trying to send, ie such that cansend()
	 * returns true. No connection for which cansend() is false
	 * should be there.
	 */
	struct bximsg_conn *conn_qhead, **conn_qtail;

	/*
	 * List of packets (aka bxipkt buffers) enqueued for sending
	 */
	struct bxipkt_buf *pkt_qhead, **pkt_qtail;

	/*
	 * This links us with the caller
	 */
	void *arg;
	struct bximsg_ops *ops;

	/*
	 * XXX: these are copie of the bxipkt fields. Use them
	 * instead
	 */
	int nid, pid, mtu;

	/* Set to true while interface is being closed. */
	int drain;
};

/*
 * Compare sequence numbers. We use
 *
 *	diff = x - y
 *
 * in modulo 2^16 signed arithmetic. This works for any x and y as
 * long as difference is in the [-2^15 : 2^15 - 1] range.
 */
static inline int seqcmp(unsigned int x, unsigned int y)
{
	int diff;

	diff = (int16_t)(x - y);
#ifdef DEBUG
	if (bximsg_debug >= 1) {
		if (diff > 1000 || diff < -1000)
			ptl_log("%u - %u = %d: seqcmp failed\n", x, y, diff);
	}
#endif
	return diff;
}

int bximsg_debug = 0;

/* Messages used for statistics. */
static const char *stat_msgs[] = {
	"'snd_start' call number",
	"'snd_data' call number",
	"'snd_end' call number",
	"Successful 'rcv_start' call number",
	"'rcv_start' call in error number",
	"'rcv_data' call number",
	"'rcv_end' call number",
	"Received packet number",
	"Sent message number",
	"Successfully sent not inline packet number",
	"Sent not inline packet in error number",
	"Successfully sent inline packet number",
	"Sent inline packet in error number",
	"Number of retransmission",
	"Sent packet number during retransmission",
	"Number of reached max retries during retransmission",
	"Failed called to 'bxipkt_getbuf'",
	"Received duplicate packets",
	NULL,
};

void bximsg_timo(void *arg);

void bximsg_options_set_default(struct bximsg_options *opts)
{
	opts->debug = 0;
	opts->stats = 0;
	opts->max_retries = -1;
	opts->nack_max = BXIMSG_NACK_MAX;
	opts->tx_timeout = BXIMSG_TX_NET_TIMEOUT;
	opts->tx_timeout_max = BXIMSG_TX_NET_TIMEOUT_MAX;
	opts->tx_timeout_var = true;
	opts->nbufs = BXIMSG_NBUFS;
	opts->wthreads = false;
	opts->transport = &bxipkt_udp;
}

int bximsg_libinit(struct bximsg_options *opts, struct bxipkt_options *pkt_opts,
		   struct timo_ctx *timo, struct bximsg_ctx *ctx)
{
	ctx->timo = timo;
	ctx->opts = *opts;

	if (opts->debug > bximsg_debug)
		bximsg_debug = opts->debug;

	if (opts->wthreads)
		bximsg_init_wthreads();

	srand(time(NULL));

	bxipkt_common_init(pkt_opts, &ctx->pkt_ctx);
	return ctx->opts.transport->libinit(pkt_opts, &ctx->pkt_ctx);
}

void bximsg_libfini(struct bximsg_ctx *ctx)
{
	bximsg_fini_wthreads();
	ctx->opts.transport->libfini(&ctx->pkt_ctx);
}

int bximsg_conn_active(struct bximsg_conn *conn)
{
	if (conn->send_seq != conn->send_ack || conn->recv_seq != conn->recv_ack ||
	    conn->msg_seq != conn->send_seq ||
	    conn->stats[BXIMSG_OUT_MSG_NB] != conn->stats[BXIMSG_SND_END_NB] ||
	    conn->stats[BXIMSG_RCV_START_SUCCESS_NB] != conn->stats[BXIMSG_RCV_END_NB] ||
	    conn->send_qhead != NULL || conn->onqueue || conn->retries)
		return 1;
	return 0;
}

int bximsg_conn_log(struct bximsg_conn *conn, int buf_size, char *buf)
{
	int len;

	len = snprintf(buf, buf_size, "peer: nid = %d, pid = %d", conn->nid, conn->pid);

	if (bximsg_conn_active(conn))
		len += snprintf(buf + len, buf_size - len, ", active");

	return len;
}

void bximsg_log_sent_pkt(struct bxipkt_buf *pkt)
{
#ifdef DEBUG
	char buf[PTL_LOG_BUF_SIZE];
	int buf_len;

	if (bximsg_debug < 3)
		return;

	buf_len = bximsg_conn_log(pkt->conn, sizeof(buf), buf);
	buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len, ": sent: size = %d", pkt->size);
	if (pkt->size > 0) {
		buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len, ", data_seq = %u",
				    pkt->hdr.data_seq);
		if (pkt->hdr.data_seq != pkt->conn->send_seq - 1) {
			snprintf(buf + buf_len, sizeof(buf) - buf_len, "(%d)",
				 pkt->hdr.data_seq - pkt->conn->send_seq + 1);
		}
	} else
		snprintf(buf + buf_len, sizeof(buf) - buf_len, ", empty");

	ptl_log("%s, ack_seq = %u\n", buf, pkt->hdr.ack_seq);
#endif
}

/*
 * Calculate initial sequence number for the given destination. We
 * could use zero, but having different send an receive initial
 * sequence numbers eases debugging.
 */
static int makeseq(int nid, int pid)
{
	return 100 * ((151121 * nid + 19937 * pid) & 0x1ff);
}

/*
 * Calculate the retransmit timeout for the next try
 */
static inline unsigned int get_next_timo(struct bximsg_conn *conn)
{
	uint64_t t;
	uint64_t retry = MIN(31, conn->retries);

	/* Calculate the timeout: (base_timeout * (2^retry)) */
	t = conn->iface->ctx->opts.tx_timeout * (1ULL << retry);

	/* Add a bit of variability (variability < base_timeout) */
	if (conn->iface->ctx->opts.tx_timeout_var)
		t += rand() % conn->iface->ctx->opts.tx_timeout;

	return (unsigned int)MIN(t, conn->iface->ctx->opts.tx_timeout_max);
}

/*
 * Return true if the connection has data: we've a context to process
 * and are not blocking.
 */
static inline int cansend_data(struct bximsg_conn *conn)
{
#ifdef DEBUG
	char buf[PTL_LOG_BUF_SIZE];
#endif

	/* if we're blocked (send quota exceeded) */
	if (seqcmp(conn->send_seq, conn->send_ack) >= conn->iface->ctx->opts.nack_max) {
#ifdef DEBUG
		if (bximsg_debug >= 3) {
			bximsg_conn_log(conn, sizeof(buf), buf);
			ptl_log("%s: blocking\n", buf);
		}
#endif
		return 0;
	}

	/* if there's nothing to send, return 0 */
	if (conn->send_qhead == NULL) {
#ifdef DEBUG
		if (bximsg_debug >= 3) {
			bximsg_conn_log(conn, sizeof(buf), buf);
			ptl_log("%s: nothing to send\n", buf);
		}
#endif
		return 0;
	}
#ifdef DEBUG
	if (bximsg_debug >= 3) {
		bximsg_conn_log(conn, sizeof(buf), buf);
		ptl_log("%s: ready to send\n", buf);
	}
#endif
	return 1;
}

/*
 * Return true if there's an ACK to send
 */
static inline int cansend_ack(struct bximsg_conn *conn)
{
#ifdef DEBUG
	char buf[PTL_LOG_BUF_SIZE];
#endif

	/* if acks need to be send, return 0 */
	if (seqcmp(conn->recv_seq, conn->recv_ack) > 0) {
#ifdef DEBUG
		if (bximsg_debug >= 3) {
			bximsg_conn_log(conn, sizeof(buf), buf);
			ptl_log("%s: ack pending\n", buf);
		}
#endif
		return 1;
	}
#ifdef DEBUG
	if (bximsg_debug >= 3) {
		bximsg_conn_log(conn, sizeof(buf), buf);
		ptl_log("%s: recv_ack = %u, recv_seq = %u: no ack pending\n", buf, conn->recv_ack,
			conn->recv_seq);
	}
#endif
	return 0;
}

/*
 * Return true if there's something to send: data or an empty ack
 * packet.
 */
static inline int cansend(struct bximsg_conn *conn)
{
	return cansend_ack(conn) || cansend_data(conn);
}

/*
 * Return the connection to the given destination. If this is the
 * first call for the given destination, the connection structure
 * will be allocated.
 */
struct bximsg_conn *bximsg_getconn(struct bximsg_iface *iface, int nid, int pid, int vc)
{
	struct bximsg_conn *c, **list;
#ifdef DEBUG
	char buf[PTL_LOG_BUF_SIZE];
#endif

	list = &iface->bximsg_connlist[BXIMSG_HASH(nid, pid, vc)];

	/* try to find an existing connection */
	for (c = *list; c != NULL; c = c->hnext) {
		if (c->nid == nid && c->pid == pid && c->vc == vc)
			return c;
	}

	/* create a new connection */
	c = xmalloc(sizeof(struct bximsg_conn), "bximsg_conn");
	c->iface = iface;
	c->nid = nid;
	c->pid = pid;
	c->vc = vc;
	timo_set(iface->ctx->timo, &c->ret_timo, bximsg_timo, c);
	c->ret_qhead = NULL;
	c->ret_qtail = &c->ret_qhead;
	c->send_qhead = NULL;
	c->send_qtail = &c->send_qhead;
	c->recv_ctx = NULL;
	c->send_seq = c->send_ack = makeseq(iface->nid, iface->pid);
	c->recv_seq = c->recv_ack = makeseq(c->nid, c->pid);
	c->msg_seq = c->send_seq;
	c->synchronizing = iface->nid != c->nid || iface->pid != c->pid;
	c->peer_synchronizing = 0;
	c->rank = -1;
	c->onqueue = 0;
	c->retries = 0;
	memset(c->stats, 0, BXIMSG_MAX_STATS * sizeof(unsigned long));

	/* link connection to the hash list */
	c->hnext = *list;
	*list = c;
#ifdef DEBUG
	if (bximsg_debug >= 3) {
		bximsg_conn_log(c, sizeof(buf), buf);
		ptl_log("%s: created, send_seq = %u, recv_seq = %u\n", buf, c->send_seq,
			c->recv_seq);
	}
#endif
	return c;
}

/*
 * Put a connection on interface send queue (aka list of active
 * connections).
 */
void bximsg_conn_enqueue(struct bximsg_iface *iface, struct bximsg_conn *conn)
{
#ifdef DEBUG
	char buf[PTL_LOG_BUF_SIZE];
#endif

	if (conn->onqueue)
		return;

	/*
	 * move it on interface send queue
	 */
	conn->qnext = NULL;
	conn->qprev = iface->conn_qtail;
	*iface->conn_qtail = conn;
	iface->conn_qtail = &conn->qnext;
	conn->onqueue = 1;
#ifdef DEBUG
	if (bximsg_debug >= 3) {
		bximsg_conn_log(conn, sizeof(buf), buf);
		ptl_log("%s: enqueued\n", buf);
	}
#endif
}

/*
 * Remove the given connection from the interface send queue, i.e. the
 * connection becomes inactive and will not be processed by the send logic.
 */
void bximsg_conn_dequeue(struct bximsg_iface *iface, struct bximsg_conn *conn)
{
#ifdef DEBUG
	char buf[PTL_LOG_BUF_SIZE];
#endif

	if (!conn->onqueue)
		return;

	*conn->qprev = conn->qnext;

	if (conn->qnext == NULL)
		iface->conn_qtail = conn->qprev;
	else
		conn->qnext->qprev = conn->qprev;

	conn->onqueue = 0;
#ifdef DEBUG
	if (bximsg_debug >= 3) {
		bximsg_conn_log(conn, sizeof(buf), buf);
		ptl_log("%s: dequeued\n", buf);
	}
#endif
}

/*
 * Enqueue a new message to the given connection. This makes the
 * connection active, unless it's blocked.
 */
void bximsg_enqueue(struct bximsg_iface *iface, struct swptl_sodata *f, size_t msgsize)
{
	struct bximsg_conn *conn = f->conn;
#ifdef DEBUG
	char buf[PTL_LOG_BUF_SIZE];
#endif

	conn->stats[BXIMSG_OUT_MSG_NB]++;

	f->msgsize = msgsize;

	f->pkt_count = (msgsize + f->hdrsize + iface->mtu - 1) / iface->mtu;
	f->pkt_next = 0;
	f->pkt_acked = 0;
	f->seq = conn->msg_seq;
	conn->msg_seq += f->pkt_count;
#ifdef DEBUG
	if (bximsg_debug >= 3) {
		bximsg_conn_log(conn, sizeof(buf), buf);
		ptl_log("%s: added %lu byte message, %u packets\n", buf, msgsize, f->pkt_count);
	}
#endif
	f->next = NULL;
	*conn->send_qtail = f;
	conn->send_qtail = &f->next;

	if (cansend_data(conn))
		bximsg_conn_enqueue(iface, conn);
}

/*
 * Send as many enqueued packets as the packet interface accepts.
 */
int bximsg_send_do(struct bximsg_iface *iface)
{
	int rc = 0;
	struct bxipkt_buf *pkt;
	struct bximsg_conn *conn;
	struct bxipkt_buf *next_pkt;

	while ((pkt = iface->pkt_qhead) != NULL) {
		if (pkt->send_pending_memcpy != 0)
			break;

		/*
		 * Save a pointer to the next packet for later because
		 * bxipkt_send might put the packet back to the freelist.
		 */
		next_pkt = pkt->next;
		conn = pkt->conn;
		pkt->hdr.ack_seq = conn->recv_seq;
		pkt->hdr.vc = conn->vc;

		if (!iface->ctx->opts.transport->send(iface->pktif, pkt, pkt->size, conn->nid,
						      conn->pid)) {
			conn->stats[BXIMSG_OUT_PKT_ERROR_NB]++;
			break;
		}

		/* save ack we're sending in this packet */
		conn->recv_ack = conn->recv_seq;

		if (!cansend_data(conn))
			bximsg_conn_dequeue(iface, conn);

		conn->stats[BXIMSG_OUT_PKT_NB]++;
		rc = 1;

		iface->pkt_qhead = next_pkt;
		if (iface->pkt_qhead == NULL)
			iface->pkt_qtail = &iface->pkt_qhead;
	}

	return rc;
}

/*
 * Enqueue the given packet.
 */
void bximsg_sendpkt(struct bximsg_iface *iface, struct bxipkt_buf *pkt)
{
	/*
	 * Link to the interface send queue
	 */
	pkt->next = NULL;
	*iface->pkt_qtail = pkt;
	iface->pkt_qtail = &pkt->next;
}

size_t bximsg_pkt_fill(struct bximsg_iface *iface, struct swptl_sodata *f, unsigned char *buf,
		       unsigned int index, volatile uint64_t *pending_memcpy)
{
	size_t todo, msgoffs, size;
	void *data;

	if (index >= f->pkt_count)
		ptl_panic("bximsg_pkt_fill: bad index\n");

	todo = iface->mtu;

	if (index == 0) {
		iface->ops->snd_start(iface->arg, f, buf);
		f->conn->stats[BXIMSG_SND_START_NB]++;

		buf += f->hdrsize;
		todo -= f->hdrsize;

		msgoffs = 0;
	} else
		msgoffs = index * iface->mtu - f->hdrsize;

	if (f->use_async_memcpy == 0)
		pending_memcpy = NULL;

	while (todo > 0) {
		if (msgoffs == f->msgsize)
			break;

		iface->ops->snd_data(iface->arg, f, msgoffs, &data, &size);
		f->conn->stats[BXIMSG_SND_DATA_NB]++;

		if (size > todo)
			size = todo;

		bximsg_async_memcpy(buf, data, size, index, pending_memcpy);

		buf += size;
		msgoffs += size;
		todo -= size;
	}

	return iface->mtu - todo;
}

void bximsg_pkt_handle(struct bximsg_iface *iface, struct swptl_sodata *f, unsigned char *buf,
		       size_t todo, unsigned int index, volatile uint64_t *pending_memcpy)
{
	size_t msgoffs, size;
	void *data;

	if (index >= f->pkt_count)
		ptl_panic("bximsg_pkt_handle: bad index\n");

	if (index == 0) {
		buf += f->hdrsize;
		todo -= f->hdrsize;
		msgoffs = 0;
	} else
		msgoffs = index * iface->mtu - f->hdrsize;

	if (f->use_async_memcpy == 0)
		pending_memcpy = NULL;

	while (todo > 0 && msgoffs < f->msgsize) {
		iface->ops->rcv_data(iface->arg, f, msgoffs, &data, &size);
		f->conn->stats[BXIMSG_RCV_DATA_NB]++;
		if (size > todo)
			size = todo;

		bximsg_async_memcpy(data, buf, size, index, pending_memcpy);

		buf += size;
		msgoffs += size;
		todo -= size;
	}
}

int bximsg_send_data(struct bximsg_iface *iface, struct bximsg_conn *conn)
{
	struct bxipkt_buf *pkt;
	struct swptl_sodata *f;
	unsigned char *buf;
	char msg[PTL_LOG_BUF_SIZE];
#ifdef DEBUG
	int msg_len;
#endif

	/* get a packet buffer */
	pkt = iface->ctx->opts.transport->getbuf(iface->pktif);
	if (pkt == NULL) {
		conn->stats[BXIMSG_GET_BUF_ERROR_NB]++;
		LOGN(4, "bximsg_send: no buffer\n");
		return 0;
	}

	/* initialize the packet structure and the packet header */
	pkt->conn = conn;
	pkt->hdr.data_seq = conn->send_seq;
	/* Start synchronization handshake on first packet to transmit */
	pkt->hdr.flags = conn->synchronizing ? BXIMSG_HDR_FLAG_SYN : 0;
	if (conn->peer_synchronizing) {
		pkt->hdr.flags |= BXIMSG_HDR_FLAG_SYN_ACK;
		conn->peer_synchronizing = 0;
	}
	pkt->send_pending_memcpy = 0;
	buf = pkt->addr;

	/*
	 * There's necesserily a message, otherwise
	 * cansend_data() wouldn't be true.
	 */
	f = conn->send_qhead;
	if (f == NULL) {
		bximsg_conn_log(conn, sizeof(msg), msg);
		ptl_panic("%s: connection has no msg to send\n", msg);
	}
	if (f->conn != conn) {
		bximsg_conn_log(conn, sizeof(msg), msg);
		ptl_panic("%s: connection mismatch\n", msg);
	}

	if (f->pkt_next == 0) {
		if (f->msgsize < bximsg_async_memcpy_min_msg_size)
			f->use_async_memcpy = 0;
		else
			f->use_async_memcpy = 1;
	}

	pkt->size = bximsg_pkt_fill(iface, f, buf, f->pkt_next, &pkt->send_pending_memcpy);

	/* calculate next packet we expect */
	conn->send_seq++;

	/* enqueue the packet to the interface send queue */
	bximsg_sendpkt(iface, pkt);

	/*
	 * if this is the first packet for retransmit, start a
	 * retransmit time-out for the connection
	 */
	if (conn->ret_qhead == NULL)
		timo_add(&conn->ret_timo, get_next_timo(conn));

	/*
	 * Attach packet to the retransmit queue, it will
	 * stay there until it's ack'ed.
	 */
	if (f->pkt_acked == f->pkt_next) {
		f->ret_next = NULL;
		*conn->ret_qtail = f;
		conn->ret_qtail = &f->ret_next;
#ifdef DEBUG
		if (bximsg_debug >= 3) {
			msg_len = bximsg_conn_log(conn, sizeof(msg), msg);
			msg_len += snprintf(msg + msg_len, sizeof(msg) - msg_len, ": ");
			swptl_ctx_log(f, sizeof(msg) - msg_len, msg + msg_len);
			ptl_log("%s: message append to retq\n", msg);
		}
#endif
	}

	/* move to next packet */
	f->pkt_next++;

	if (f->pkt_next == f->pkt_count) {
		/* remove from queue */
		conn->send_qhead = f->next;
		if (conn->send_qhead == NULL)
			conn->send_qtail = &conn->send_qhead;
#ifdef DEBUG
		if (bximsg_debug >= 3) {
			msg_len = bximsg_conn_log(conn, sizeof(msg), msg);
			msg_len += snprintf(msg + msg_len, sizeof(msg) - msg_len, ": ");
			swptl_ctx_log(f, sizeof(msg) - msg_len, msg + msg_len);
			ptl_log("%s: msg done, send ctx cleared\n", msg);
		}
#endif
	}

	if (!cansend(conn))
		bximsg_conn_dequeue(iface, conn);

	return 1;
}

/*
 * Reset sequence numbers for the given connection and enter synchronization
 * state.
 */
static void bximsg_reset_conn(struct bximsg_iface *iface, struct bximsg_conn *conn)
{
	conn->send_seq = conn->send_ack = makeseq(iface->nid, iface->pid);
	conn->recv_seq = conn->recv_ack = makeseq(conn->nid, conn->pid);
	conn->msg_seq = conn->send_seq;
	conn->synchronizing = 1;

	ptl_log("reset connection seq numbers send=%d/recv=%d\n",
		conn->send_seq, conn->recv_seq);
}

int bximsg_send_ack(struct bximsg_iface *iface, struct bximsg_conn *conn)
{
	struct bximsg_hdr hdr;
#ifdef DEBUG
	char buf[PTL_LOG_BUF_SIZE];
#endif

	hdr.data_seq = conn->send_seq;
	hdr.ack_seq = conn->recv_seq;
	/* If we are synchronizing, send a NACK_RST */
	hdr.flags = conn->synchronizing ? BXIMSG_HDR_FLAG_NACK_RST : 0;
	if (conn->peer_synchronizing) {
		hdr.flags |= BXIMSG_HDR_FLAG_SYN_ACK;
		conn->peer_synchronizing = 0;
	}
	hdr.vc = conn->vc;
	hdr.__pad = 0;

	if (!iface->ctx->opts.transport->send_inline(iface->pktif, &hdr, conn->nid, conn->pid)) {
		conn->stats[BXIMSG_OUT_INLINE_PKT_ERROR_NB]++;
		return 0;
	}

	/* save ack we're sending in this packet */
	conn->recv_ack = conn->recv_seq;

	conn->stats[BXIMSG_OUT_INLINE_PKT_NB]++;
#ifdef DEBUG
	if (bximsg_debug >= 3) {
		bximsg_conn_log(conn, sizeof(buf), buf);
		ptl_log("%s: inline sent: data_seq = %u, ack_seq = %u\n", buf, hdr.data_seq,
			hdr.ack_seq);
	}
#endif
	if (!cansend_data(conn))
		bximsg_conn_dequeue(iface, conn);

	return 1;
}

/*
 * Attempt to send: get the first connection in the send queue (ie
 * first active connection) and send a data packet or an ack packet.
 */
int bximsg_send(struct bximsg_iface *iface)
{
	struct bximsg_conn *conn;
	char buf[PTL_LOG_BUF_SIZE];

	/* get the first active connection */
	conn = iface->conn_qhead;
	if (conn == NULL) {
		LOGN(5, "bximsg_send: no active connections\n");
		return 0;
	}

	/* we've data to send (prepare a data + ack packet) */
	if (!cansend(conn)) {
		bximsg_conn_log(conn, sizeof(buf), buf);
		ptl_panic("%s: conn on queue, but nothing to send\n", buf);
	}

	if (cansend_data(conn)) {
		if (bximsg_send_data(iface, conn))
			return 1;
	}

	if (cansend_ack(conn))
		return bximsg_send_ack(iface, conn);

	return 0;
}

/*
 * Process an incoming ACK.
 */
void bximsg_ack(struct bximsg_iface *iface, struct bximsg_conn *conn, unsigned int ack_seq)
{
	struct swptl_sodata *f;
	int delta;
	int pkt_avail;
	int pkt_delta;
#ifdef DEBUG
	char buf[PTL_LOG_BUF_SIZE];
	int buf_len;
#endif

	/* check if we already got it */
	delta = seqcmp(ack_seq, conn->send_ack);
	if (delta <= 0)
		return;

	/* advance send position, restart timeer */
	conn->send_ack = ack_seq;
	if (conn->ret_qhead) {
		timo_del(&conn->ret_timo);
		conn->retries = 0;
		timo_add(&conn->ret_timo, get_next_timo(conn));
	}
#ifdef DEBUG
	if (bximsg_debug >= 3) {
		bximsg_conn_log(conn, sizeof(buf), buf);
		ptl_log("%s: conn->send_ack -> %u\n", buf, conn->send_ack);
	}
#endif
	/* trash unneeded packets from the retransmit buffer */
	for (;;) {
		f = conn->ret_qhead;
		if (f == NULL) {
			timo_del(&conn->ret_timo);
			break;
		}

		pkt_avail = f->pkt_next - f->pkt_acked;

		pkt_delta = delta;
		if (pkt_delta > pkt_avail)
			pkt_delta = pkt_avail;

		f->pkt_acked += pkt_delta;
		delta -= pkt_delta;

		/*
		 * If the packet range is not ack'ed stop here. This
		 * is OK because retransmit queue is sorted
		 * with increasing sequence numbers
		 */
		if (f->pkt_next > f->pkt_acked)
			break;

		/* detach from retransmit queue */
		conn->ret_qhead = f->ret_next;
		if (conn->ret_qhead == NULL)
			conn->ret_qtail = &conn->ret_qhead;
#ifdef DEBUG
		if (bximsg_debug >= 3) {
			buf_len = bximsg_conn_log(conn, sizeof(buf), buf);
			buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len, ": ");
			swptl_ctx_log(f, sizeof(buf) - buf_len, buf + buf_len);
			ptl_log("%s: releasing message\n", buf);
		}
#endif
		if (f->pkt_acked == f->pkt_count) {
			iface->ops->snd_end(iface->arg, f, 0);
			conn->stats[BXIMSG_SND_END_NB]++;
		}
	}

	/* if this unblocks the connection, reactivate it */
	if (cansend_data(conn))
		bximsg_conn_enqueue(iface, conn);
}

/*
 * Connection retransmit time-out expired: i.e. we didn't receive the
 * ACK within the given time frame. Retransmit all packets in the
 * retransmit buffer.
 */
void bximsg_timo(void *arg)
{
	struct bxipkt_buf *pkt;
	struct bximsg_conn *conn = arg;
	struct bximsg_iface *iface = conn->iface;
	struct swptl_sodata *f;
	unsigned int index;
	int max_retries;
	char buf[PTL_LOG_BUF_SIZE];
#ifdef DEBUG
	int buf_len;
#endif

	conn->stats[BXIMSG_RTX_CALL_NB]++;
#ifdef DEBUG
	if (bximsg_debug >= 2) {
		bximsg_conn_log(conn, sizeof(buf), buf);
		ptl_log("%s: timeout expired\n", buf);
	}
#endif
	max_retries = (conn->iface->ctx->opts.max_retries >= 0) ?
			      conn->iface->ctx->opts.max_retries :
		      iface->drain ? BXIMSG_MAX_RETRIES :
				     INT_MAX;

	if (conn->retries < max_retries) {
		f = conn->ret_qhead;
		if (f == NULL) {
			bximsg_conn_log(conn, sizeof(buf), buf);
			ptl_panic("%s: no packets to retransmit\n", buf);
		}

		/*
		 * Resend all packets in the retransmit queue, but stop if we
		 * run out of buffers. We'll retry later anyway.
		 */
		index = f->pkt_acked;
		while (1) {
			pkt = conn->iface->ctx->opts.transport->getbuf(iface->pktif);
			if (pkt == NULL) {
				conn->stats[BXIMSG_GET_BUF_ERROR_NB]++;
				break;
			}

			pkt->hdr.data_seq = f->seq + index;
			pkt->hdr.flags = conn->synchronizing ? BXIMSG_HDR_FLAG_SYN : 0;
			pkt->conn = conn;
			pkt->send_pending_memcpy = 0;
#ifdef DEBUG
			if (bximsg_debug >= 3) {
				buf_len = bximsg_conn_log(conn, sizeof(buf), buf);
				buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len, ": ");
				swptl_ctx_log(f, sizeof(buf) - buf_len, buf + buf_len);
				ptl_log("%s: regen packet %u\n", buf, index);
			}
#endif
			pkt->size = bximsg_pkt_fill(iface, f, pkt->addr, index,
						    &pkt->send_pending_memcpy);
#ifdef DEBUG
			if (bximsg_debug >= 2) {
				bximsg_conn_log(conn, sizeof(buf), buf);
				ptl_log("%s: data = %u: resending\n", buf, pkt->hdr.data_seq);
			}
#endif
			conn->stats[BXIMSG_RTX_PKT_NB]++;
			bximsg_sendpkt(iface, pkt);

			if (++index == f->pkt_next) {
				f = f->ret_next;
				if (f == NULL)
					break;
				index = f->pkt_acked;
			}
		}

		conn->retries++;
		timo_add(&conn->ret_timo, get_next_timo(conn));
		return;
	}

	/*
	 * reached max retransmit count with no progress,
	 * trash the retransmit buffer.
	 */
	conn->stats[BXIMSG_RTX_MAX_RETRIES_NB]++;
	while ((f = conn->ret_qhead) != NULL) {
		conn->ret_qhead = f->ret_next;
#ifdef DEBUG
		if (bximsg_debug >= 3) {
			bximsg_conn_log(conn, sizeof(buf), buf);
			ptl_log("%s: %u..%u: nret max reached\n", buf,
				(uint16_t)(f->pkt_acked + f->seq),
				(uint16_t)(f->pkt_next + f->seq));
		}
#endif
		/* stop waiting for ack */
		conn->send_ack = f->seq + f->pkt_next;
	}
	conn->ret_qtail = &conn->ret_qhead;

	/* abort incoming message in progress */
	if (conn->recv_ctx) {
		iface->ops->rcv_end(iface->arg, conn->recv_ctx, 1);
		conn->stats[BXIMSG_RCV_END_NB]++;
		conn->recv_ctx = NULL;
	}

	while ((f = conn->send_qhead) != NULL) {
		conn->send_qhead = f->next;
		iface->ops->snd_end(iface->arg, f, 1);
		conn->stats[BXIMSG_SND_END_NB]++;
	}
	conn->send_qtail = &conn->send_qhead;

	/* free associated contexts in upper layer */
	iface->ops->conn_err(iface->arg, conn);
	conn->retries = 0;
}

/*
 * Packet input call-back, invoked whenever a new packet is received.
 */
void bximsg_input(void *arg, void *data, size_t size, struct bximsg_hdr *hdr, int nid, int pid,
		  int uid)
{
	struct bximsg_iface *iface = arg;
	struct bximsg_conn *conn;
	struct swptl_sodata *f;
	size_t msgsize;
	int rc;
#ifdef DEBUG
	char buf[PTL_LOG_BUF_SIZE];
#endif

	if (hdr->vc >= BXIMSG_VC_COUNT) {
		ptl_log("%d: bad vc from nid %d, pid = %d\n", hdr->vc, nid, pid);
		return;
	}

	/* find connection this packet belongs to */
	conn = bximsg_getconn(iface, nid, pid, hdr->vc);
#ifdef DEBUG
	if (bximsg_debug >= 3) {
		bximsg_conn_log(conn, sizeof(buf), buf);
		ptl_log("%s: received: size = %lu, data_seq = %u, ack_seq ="
			" %u\n",
			buf, size, hdr->data_seq, hdr->ack_seq);
	}
#endif
	conn->stats[BXIMSG_IN_PKT_NB]++;

	if (hdr->flags & BXIMSG_HDR_FLAG_NACK_RST) {
		bximsg_reset_conn(iface, conn);
		return;
	}
	if (conn->synchronizing) {
		if (hdr->flags & BXIMSG_HDR_FLAG_SYN_ACK) {
			/* End of the synchronization handshake */
			conn->recv_seq = conn->recv_ack = hdr->data_seq;
			conn->synchronizing = 0;
			/* Let the ACK go through */
		} else if (!(hdr->flags & BXIMSG_HDR_FLAG_SYN)) {
			/* We just restarted, and the peer is not synchronizing,
			 * drop the packet and send a NACK_RST */
			conn->recv_seq++; /* Force sending ack */
			goto done_ack;
		}
	}
	if (hdr->flags & BXIMSG_HDR_FLAG_SYN) {
		/* Peer informs us that it has restarted */
		conn->recv_seq = conn->recv_ack = hdr->data_seq;
		/* Let the packet through and remember to send a SYN_ACK */
		conn->peer_synchronizing = 1;
	}

	/* handle the send ack, the rest is receive-specific*/
	bximsg_ack(iface, conn, hdr->ack_seq);

	/* if this is an empty (aka ack-only) packet, we're done */
	if (size == 0)
		return;

	/* closing the interface, don't accept more data */
	if (iface->drain) {
#ifdef DEBUG
		if (bximsg_debug >= 2) {
			bximsg_conn_log(conn, sizeof(buf), buf);
			ptl_log("%s: %u: connection closing, dropped\n", buf, hdr->data_seq);
		}
#endif
		return;
	}

	/* if we missed a previous packet, wait for retransmit */
	if (seqcmp(hdr->data_seq, conn->recv_seq) > 0) {
#ifdef DEBUG
		if (bximsg_debug >= 2) {
			bximsg_conn_log(conn, sizeof(buf), buf);
			ptl_log("%s: %u: lost packet, got %u instead\n", buf, conn->recv_seq,
				hdr->data_seq);
		}
#endif
		return;
	}

	/* if we already got this packet, retransmit ack for it */
	if (seqcmp(hdr->data_seq, conn->recv_seq) < 0) {
		conn->stats[BXIMSG_IN_PKT_DUPLICATES]++;
#ifdef DEBUG
		if (bximsg_debug >= 2) {
			bximsg_conn_log(conn, sizeof(buf), buf);
			ptl_log("%s: %u: duplicate, expected %u\n", buf, hdr->data_seq,
				conn->recv_seq);
		}
#endif

		/*
		 * Move the receive-ack position backwards, this will
		 * trigger (re-)transmission of the old acks
		 */
		conn->recv_ack = hdr->data_seq;
		goto done_ack;
	}

	/* accept the packet */
	conn->recv_seq++;
#ifdef DEBUG
	if (bximsg_debug >= 3) {
		bximsg_conn_log(conn, sizeof(buf), buf);
		ptl_log("%s: conn->recv_seq -> %u\n", buf, conn->recv_seq);
	}
#endif
	f = conn->recv_ctx;
	if (f == NULL) {
		rc = iface->ops->rcv_start(iface->arg, data, size, nid, pid, hdr->vc, uid, &f,
					   &msgsize);

		/*
		 * if we run out of context (receive resources)
		 * then just drop the packet, it will retransmitted
		 * later, hopefully we'll have resources soon
		 */
		if (rc) {
			conn->stats[BXIMSG_RCV_START_SUCCESS_NB]++;
		} else {
#ifdef DEBUG
			if (bximsg_debug >= 2) {
				bximsg_conn_log(conn, sizeof(buf), buf);
				ptl_log("%s: %u: couldn't start, packet"
					" dropped\n",
					buf, hdr->data_seq);
			}
#endif
			conn->recv_seq--;
			conn->stats[BXIMSG_RCV_START_ERROR_NB]++;
			return;
		}

		conn->recv_ctx = f;

		f->pkt_count = (msgsize + f->hdrsize + iface->mtu - 1) / iface->mtu;
		f->pkt_next = 0;
		f->pkt_acked = 0;
		f->msgsize = msgsize;
		f->recv_pending_memcpy = 0;

		if (msgsize < bximsg_async_memcpy_min_msg_size)
			f->use_async_memcpy = 0;
		else
			f->use_async_memcpy = 1;
	}

	bximsg_pkt_handle(iface, f, data, size, f->pkt_next++, &f->recv_pending_memcpy);

	if (f->pkt_count == f->pkt_next) {
		while (f->recv_pending_memcpy != 0)
			;

		conn->recv_ctx = NULL;
		iface->ops->rcv_end(iface->arg, f, 0);
		conn->stats[BXIMSG_RCV_END_NB]++;
	}

done_ack:

	/*
	 * if an ACK needs to be sent back, put the connection on send
	 * queue
	 */
	if (cansend(conn))
		bximsg_conn_enqueue(iface, conn);
}

/*
 * Packet send completion call-back, invoked whenever a packet may be
 * reused.
 */
void bximsg_output(void *arg, struct bxipkt_buf *pkt)
{
	struct bximsg_iface *iface = arg;
#ifdef DEBUG
	struct bximsg_conn *conn = pkt->conn;
	char buf[PTL_LOG_BUF_SIZE];
#endif

	iface->ctx->opts.transport->putbuf(iface->pktif, pkt);
#ifdef DEBUG
	if (bximsg_debug >= 3) {
		bximsg_conn_log(conn, sizeof(buf), buf);
		ptl_log("%s: %u: freeed\n", buf, pkt->hdr.data_seq);
	}
#endif
}

/*
 * Create a message-based interface.
 */
struct bximsg_iface *bximsg_init(struct bximsg_ctx *ctx, void *arg, struct bximsg_ops *ops,
				 int nic_iface, int uid, int pid, int *rnid, int *rpid)
{
	struct bximsg_iface *iface;
	int i;

	iface = xmalloc(sizeof(struct bximsg_iface), "bximsg_iface");
	if (iface == NULL)
		return NULL;

	iface->ctx = ctx;

	for (i = 0; i < BXIMSG_HASHSIZE; i++)
		iface->bximsg_connlist[i] = NULL;

	iface->pktif =
		ctx->opts.transport->init(&ctx->pkt_ctx, 0, nic_iface, uid, pid, ctx->opts.nbufs,
					  iface, bximsg_input, bximsg_output, bximsg_log_sent_pkt,
					  &iface->nid, &iface->pid, &iface->mtu);
	if (iface->pktif == NULL)
		return NULL;

	iface->pkt_qhead = NULL;
	iface->pkt_qtail = &iface->pkt_qhead;

	iface->conn_qhead = NULL;
	iface->conn_qtail = &iface->conn_qhead;

	*rnid = iface->nid;
	*rpid = iface->pid;
	iface->arg = arg;
	iface->ops = ops;
	iface->drain = 0;

	return iface;
}

static void dump_stats(const char *msg, unsigned long *stats)
{
	int i;

	ptl_log("%s\n", msg);
	for (i = 0; i < BXIMSG_MAX_STATS; i++) {
		if (stat_msgs[i] == NULL)
			ptl_panic("stat_msgs[i] == NULL");
		ptl_log("  %s = %lu\n", stat_msgs[i], stats[i]);
	}
}

/*
 * Destroy the given message-based interface.
 */
void bximsg_done(struct bximsg_iface *iface)
{
	struct bximsg_conn *c, **l;
	int i;
	int j;
	unsigned long totals[BXIMSG_MAX_STATS];
	char buf[PTL_LOG_BUF_SIZE];

	iface->drain = 1;

	/*
	 * Drain all connections
	 */
	for (i = 0; i < BXIMSG_HASHSIZE; i++) {
		l = &iface->bximsg_connlist[i];
		for (c = *l; c != NULL; c = c->hnext) {
			if (c->ret_qhead || cansend_ack(c)) {
#ifdef DEBUG
				if (bximsg_debug >= 2) {
					bximsg_conn_log(c, sizeof(buf), buf);
					ptl_log("%s: draining\n", buf);
				}
#endif
				while (c->ret_qhead || cansend_ack(c))
					swptl_dev_progress(iface->arg, 1);
#ifdef DEBUG
				if (bximsg_debug >= 2) {
					bximsg_conn_log(c, sizeof(buf), buf);
					ptl_log("%s: drained\n", buf);
				}
#endif
			}
		}
	}

	/*
	 * Wait buffers to be released
	 */
	while (iface->pkt_qhead)
		bximsg_send_do(iface);

	/*
	 * Close packet interface */
	iface->ctx->opts.transport->done(iface->pktif);

	/*
	 * Free connections
	 */
	memset(totals, 0, BXIMSG_MAX_STATS * sizeof(unsigned long));

	for (i = 0; i < BXIMSG_HASHSIZE; i++) {
		l = &iface->bximsg_connlist[i];
		while ((c = *l) != NULL) {
			*l = c->hnext;

			for (j = 0; j < BXIMSG_MAX_STATS; j++)
				totals[j] += c->stats[j];

			if (iface->ctx->opts.stats >= 2) {
				bximsg_conn_log(c, sizeof(buf), buf);
				dump_stats(buf, c->stats);
			}

			xfree(c);
		}
	}

	if (iface->ctx->opts.stats >= 1) {
		dump_stats("bximsg stats", totals);
	}
	xfree(iface);
}

void bximsg_pkt_dump(struct bxipkt_buf *pkt)
{
}

void bximsg_conn_dump(struct bximsg_conn *conn)
{
	struct swptl_sodata *f;
	char buf[PTL_LOG_BUF_SIZE];

	bximsg_conn_log(conn, sizeof(buf), buf);
	ptl_log("%s\n", buf);

	snprintf(buf, sizeof(buf), "  recv_seq = %u, recv_ack = %u, send_seq = %u, send_ack = %u",
		 conn->recv_seq, conn->recv_ack, conn->send_seq, conn->send_ack);

	dump_stats(buf, conn->stats);

	ptl_log("  retries = %lu\n", conn->retries);

	if (conn->recv_ctx) {
		ptl_log("  receiving message:\n");
		swptl_ctx_dump(conn->recv_ctx);
	}
	ptl_log("  output message queue:\n");
	for (f = conn->send_qhead; f != NULL; f = f->next)
		swptl_ctx_dump(f);
	ptl_log("  retransmit queue:\n");
	for (f = conn->ret_qhead; f != NULL; f = f->ret_next) {
		ptl_log("  data_seq = %u..%u\n", (uint16_t)(f->seq + f->pkt_acked),
			(uint16_t)(f->seq + f->pkt_next));
	}
}

/*
 * Dump interface internal state to the console
 */
void bximsg_dump(struct bximsg_iface *iface)
{
	struct bximsg_conn *conn, *list;
	struct bxipkt_buf *pkt;
	int i;
	char buf[PTL_LOG_BUF_SIZE];

	ptl_log("connections:\n");
	for (i = 0; i < BXIMSG_HASHSIZE; i++) {
		list = iface->bximsg_connlist[i];
		for (conn = list; conn != NULL; conn = conn->hnext)
			bximsg_conn_dump(conn);
	}

	ptl_log("output queue:\n");
	for (pkt = iface->pkt_qhead; pkt != NULL; pkt = pkt->next) {
		bximsg_conn_log(pkt->conn, sizeof(buf), buf);
		ptl_log("%s:  data_seq = %u\n", buf, pkt->hdr.data_seq);
	}

	iface->ctx->opts.transport->dump(iface->pktif);
}

int bximsg_nfds(struct bximsg_iface *iface)
{
	return iface->ctx->opts.transport->nfds(iface->pktif);
}

int bximsg_pollfd(struct bximsg_iface *iface, struct pollfd *pfds)
{
	int events = 0;

	/* produce packets to send */
	bximsg_send(iface);

	/* if there are packets to send, check if we can send */
	if (iface->pkt_qhead != NULL && iface->pkt_qhead->send_pending_memcpy == 0)
		events |= POLLOUT;

	return iface->ctx->opts.transport->pollfd(iface->pktif, pfds, events);
}

int bximsg_revents(struct bximsg_iface *iface, struct pollfd *pfds)
{
	int revents;

	revents = iface->ctx->opts.transport->revents(iface->pktif, pfds);
	if (revents & POLLOUT)
		bximsg_send_do(iface);

	return 0;
}
