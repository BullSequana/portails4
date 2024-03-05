#ifndef SWPTL4_TRANSPORT_H
#define SWPTL4_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <poll.h>
#include <sys/types.h>

#include "swptl4.h"

/*
 * Protocol layer header, must fit in 8-byte BXI header data
 */
struct bximsg_hdr {
	uint16_t data_seq; /* seq of this packet */
	uint16_t ack_seq; /* seq this packet acks */
	uint16_t vc;
	uint8_t flags;
#define BXIMSG_HDR_FLAG_SYN 0x01
#define BXIMSG_HDR_FLAG_SYN_ACK 0x02
#define BXIMSG_HDR_FLAG_NACK_RST 0x04

	uint8_t __pad;
};

struct bxipkt_buf {
	struct bxipkt_buf *next;
	struct bximsg_hdr hdr;
	void *addr; /* mmap()ed buffer addr */
	unsigned int index; /* bxipkt md index */
	struct bximsg_conn *conn; /* packet owner */
	int size; /* all headers included */
	unsigned int is_small_pkt; /* indicate if use "small message" channel */
	/* The number of pending memcpy, only used when sending data. */
	volatile uint64_t send_pending_memcpy;
};

struct bxipkt_ctx {
	struct bxipkt_options opts;

	void *priv;
};

struct bxipkt_ops {
	/* Library initialization. */
	int (*libinit)(struct bxipkt_options *opts, struct bxipkt_ctx *ctx);

	/* Library finalization. */
	void (*libfini)(struct bxipkt_ctx *ctx);

	/*
	 * Create a bxi packet interface and return a pointer to it.
	 *
	 *  vn: vn use for communication: 1 service, 0 == compute
	 *
	 *  uid:    user id
	 *
	 *  pid:    desired pid or 0 to dynamically allocate one
	 *
	 *      nbufs:  desired number of buffers count
	 *
	 *  arg:    pointer to pass as first argument to input and output
	 *      call-backs
	 *
	 *  input:  call-back invoked whenever a packet is received,
	 *      arguments are as follows:
	 *
	 *      arg:    pointer passed to bxipkt_init()
	 *      data:   received payload
	 *      size:   size of received payload
	 *      hdr:    hdr data received inline
	 *      nid:    nid the packet come from
	 *      pid:    pid the packet come from
	 *      uid:    uid the packet come from
	 *
	 *  output: call-back invoked when packet was sent.
	 *      Arguments are as follows:
	 *
	 *      arg:    pointer passed to bxipkt_init()
	 *
	 *  sent_pkt: call-back invoked when a packet has been send
	 *      Arguments are as follows:
	 *
	 *      pkt:    a packet
	 *
	 *  nic_iface: desired nic interface (0 for bxi0)
	 *
	 *  rnid:   pointer to location where the local nid is stored
	 *
	 *  rpid:   pointer to location where the local pid is stored
	 *
	 *  rmtu:   max transfer unit, ie buffer sizes
	 *
	 */
	struct bxipkt_iface *(*init)(struct bxipkt_ctx *ctx, int vn, int nic_iface, int uid,
				     int pid, int nbufs, void *arg,
				     void (*input)(void *arg, void *data, size_t size,
						   struct bximsg_hdr *hdr, int nid, int pid,
						   int uid),
				     void (*output)(void *arg, struct bxipkt_buf *),
				     void (*sent_pkt)(struct bxipkt_buf *pkt), int *rnid, int *rpid,
				     int *rmtu);

	/*
	 * Destroy the given interface created by bxipkt_init().
	 */
	void (*done)(struct bxipkt_iface *iface);

	/*
	 * Start sending a packet. Return 0 if the packet couldn't be
	 * sent, in which case the caller may retry later.
	 *
	 *  iface:  interface that will send the packet
	 *
	 *  buf:    buffer structure returned by bxipkt_getbuf()
	 *
	 *  len:    payload length
	 *
	 *  nid:    destination nid
	 *
	 *  pid:    destination pid
	 *
	 *  uptr:   pointer passed to the output call-back
	 */
	int (*send)(struct bxipkt_iface *iface, struct bxipkt_buf *buf, size_t len, int nid,
		    int pid);
	/*
	 * Start sending a small packet (< 64). Return 0 if the packet couldn't
	 * be sent, in which case the caller may retry later.
	 *
	 *  iface:  interface that will send the packet
	 *
	 *  hdr_data: user data included in the message header (64 bits)
	 *
	 *  nid:    destination nid
	 *
	 *  pid:    destination pid
	 */
	int (*send_inline)(struct bxipkt_iface *iface, struct bximsg_hdr *hdr_data, int nid,
			   int pid);

	/*
	 * Allocate a send buffer. Return the buffer structure passed as argment
	 * to bxipkt_send(). Returns NULL, if no buffers are available anymore.
	 *
	 *  iface:  interface that will use the send buffer
	 *
	 */
	struct bxipkt_buf *(*getbuf)(struct bxipkt_iface *iface);

	/*
	 * Free the given send buffer.
	 *
	 *  iface:  interface to return the buffer to
	 *
	 *  buf:    buffer to free
	 *
	 */
	void (*putbuf)(struct bxipkt_iface *iface, struct bxipkt_buf *buf);

	/*
	 * Dump state of the given interface
	 */
	void (*dump)(struct bxipkt_iface *iface);

	int (*nfds)(struct bxipkt_iface *iface);
	int (*pollfd)(struct bxipkt_iface *iface, struct pollfd *pfds, int events);
	int (*revents)(struct bxipkt_iface *iface, struct pollfd *pfds);
};

#endif /* SWPTL4_TRANSPORT_H */
