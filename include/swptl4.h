#ifndef SWPTL4_H
#define SWPTL4_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <poll.h>
#include <sys/types.h>

#include "portals4.h"
#include "portals4_bxiext.h"

#define SWPTL_EV_STR_SIZE 256

struct swptl_dev;

/*
 * Protocol layer header, must fit in 8-byte BXI header data
 */
struct bximsg_hdr {
	uint16_t data_seq; /* seq of this packet */
	uint16_t ack_seq; /* seq this packet acks */
	uint16_t vc;
	uint16_t __pad;
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

/*
 * Note that this struct is _never_ directly passed, but will instead be embedded as the first field
 * in the transport specific options (see bxipkt_options_udp)
 */
struct bxipkt_options {
	int debug; /* default: 0 */
	uint stats; /* default: false */
};

struct bxipkt_ctx {
	struct bxipkt_options opts;

	void *priv;
};

struct bxipkt_udp_options {
	struct bxipkt_options global;

	bool default_mtu; /* Use the default MTU (ETHERMTU) */
	const char *ip; /* IP address, must be provided */
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
	struct bxipkt_iface *(*init)(struct bxipkt_ctx *ctx, int vn, int nic_iface, int pid,
				     int nbufs, void *arg,
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

extern struct bxipkt_ops bxipkt_udp;

struct bximsg_options {
	int debug; /* default: 0 */
	uint stats; /* default: 0. Can be 1 or 2 depending on the amount of stats required */
	int max_retries; /* default: -1, -1 means INT_MAX retries */
	int nack_max; /* default: BXIMSG_NACK_MAX, maximum of non-acked packets in flight */
	ulong tx_timeout; /* default: BXIMSG_TX_NET_TIMEOUT, base timeout in 24-th of a microsecond
			   */
	ulong tx_timeout_max; /* default: BXIMSG_TX_NET_TIMEOUT_MAX, maximum timeout */
	bool tx_timeout_var; /* default: true, add some randomness to the timeout */
	uint nbufs; /* default: BXIMSG_NBUFS, number of buffers per PID used by the transport layer
		     */
	bool wthreads; /* default: false, enable threaded memcpy */
	struct bxipkt_ops *transport; /* default: UDP, this requires to set the ip as a pkt option
				       */
};

struct swptl_options {
	int debug; /* default: 0 */
};

void bximsg_options_set_default(struct bximsg_options *opts);
void bxipkt_options_set_default(struct bxipkt_options *opts);
void swptl_options_set_default(struct swptl_options *opts);

struct swptl_ctx;

struct swptl_ni;
struct swptl_eq;
struct swptl_md;
struct swptl_me;
struct swptl_ct;

struct swptl_md_params {
	void *start;
	ptl_size_t length;
	unsigned int options;
	struct swptl_eq *eq_handle;
	struct swptl_ct *ct_handle;
};

struct swptl_me_params {
	void *start;
	ptl_size_t length;
	struct swptl_ct *ct_handle;
	ptl_uid_t uid;
	unsigned int options;
	ptl_process_t match_id;
	ptl_match_bits_t match_bits;
	ptl_match_bits_t ignore_bits;
	ptl_size_t min_free;
};

/*
 * Returns PTL_PID_IN_USE or PTL_FAIL
 */
int swptl_dev_new(struct swptl_ctx *ctx, int iface, int pid, size_t rdv_put,
		  struct swptl_dev **dev);

int swptl_func_libinit(struct swptl_options *opts, struct bximsg_options *msg_opts,
		       struct bxipkt_options *transport_opts, struct swptl_ctx **ctx);
void swptl_func_libfini(struct swptl_ctx *ctx);
void swptl_func_abort(struct swptl_ctx *ctx);
int swptl_func_setmemops(struct ptl_mem_ops *ops);

int swptl_func_activate_add(void (*)(void *, unsigned int, int), void *arg,
			    ptl_activate_hook_t *hook);
int swptl_func_activate_rm(ptl_activate_hook_t hook);

int swptl_func_ni_init(struct swptl_dev *dev, unsigned int flags, const ptl_ni_limits_t *desired,
		       ptl_ni_limits_t *actual, struct swptl_ni **handle);
int swptl_func_ni_fini(struct swptl_ni *handle);
int swptl_func_ni_handle(void *handle, struct swptl_ni **ni_handle);
int swptl_func_ni_status(struct swptl_ni *nih, ptl_sr_index_t reg, ptl_sr_value_t *status);
/*
 * Register a callback to be called when attempting to post an event but the corresponding eq
 * has been freed
 */
int swptl_func_ni_register_no_eq_callback(struct swptl_ni *ni, void (*cb)(void *), void *arg);

int swptl_func_setmap(struct swptl_ni *nih, ptl_size_t size, const ptl_process_t *map);
int swptl_func_getmap(struct swptl_ni *nih, ptl_size_t size, ptl_process_t *map,
		      ptl_size_t *retsize);

int swptl_func_pte_alloc(struct swptl_ni *nih, unsigned int opt, struct swptl_eq *eqh,
			 ptl_index_t index, ptl_index_t *retval);
int swptl_func_pte_free(struct swptl_ni *nih, ptl_index_t index);
int swptl_func_pte_enable(struct swptl_ni *nih, ptl_pt_index_t index, int enable, int nbio);

int swptl_func_getuid(struct swptl_ni *nih, ptl_uid_t *uid);
int swptl_func_getid(struct swptl_ni *nih, ptl_process_t *id);
int swptl_func_getphysid(struct swptl_ni *nih, ptl_process_t *id);
int swptl_func_gethwid(struct swptl_ni *nih, uint64_t *hwid, uint64_t *capabilities);

int swptl_func_md_bind(struct swptl_ni *ni, const struct swptl_md_params *mdpar,
		       struct swptl_md **retmd);
int swptl_func_md_release(struct swptl_md *md);

int swptl_func_append(struct swptl_ni *nih, ptl_index_t index, const struct swptl_me_params *mepar,
		      ptl_list_t list, void *uptr, struct swptl_me **mehret, int nbio);

int swptl_func_unlink(struct swptl_me *meh);
int swptl_func_search(struct swptl_ni *nih, ptl_pt_index_t index,
		      const struct swptl_me_params *mepar, ptl_search_op_t sop, void *uptr,
		      int nbio);

int swptl_func_eq_alloc(struct swptl_ni *nih, ptl_size_t count, struct swptl_eq **reteq,
			void (*cb)(void *, struct swptl_eq *), void *arg, int hint);
int swptl_func_eq_free(struct swptl_eq *eqh);
int swptl_func_eq_get(struct swptl_eq *eqh, ptl_event_t *rev);
int swptl_func_eq_poll(struct swptl_ctx *ctx, const struct swptl_eq **eqhlist, unsigned int size,
		       ptl_time_t timeout, ptl_event_t *rev, unsigned int *rwhich);
/*
 * This pair of function allows an application to get back it's context when being notified of
 * EQ completions in a poll.
 */
int swptl_func_eq_attach_ctx(struct swptl_eq *eq, void *context);
void *swptl_func_eq_get_ctx(struct swptl_eq *eq);

int swptl_func_ct_alloc(struct swptl_ni *nih, struct swptl_ct **retct);
int swptl_func_ct_free(struct swptl_ct *cth);
int swptl_func_ct_cancel(struct swptl_ct *cth);
int swptl_func_ct_get(struct swptl_ct *cth, ptl_ct_event_t *rev);
int swptl_func_ct_poll(struct swptl_ctx *ctx, const struct swptl_ct **cthlist,
		       const ptl_size_t *test, unsigned int size, ptl_time_t timeout,
		       ptl_ct_event_t *rev, unsigned int *rwhich);
int swptl_func_ct_op(struct swptl_ct *cth, ptl_ct_event_t delta, int inc);

int swptl_func_put(struct swptl_md *mdh, ptl_size_t loffs, ptl_size_t len, ptl_ack_req_t ack,
		   ptl_process_t dest, ptl_pt_index_t index, ptl_match_bits_t bits,
		   ptl_size_t roffs, void *uptr, ptl_hdr_data_t hdr, int nbio);
int swptl_func_get(struct swptl_md *mdh, ptl_size_t loffs, ptl_size_t len, ptl_process_t dest,
		   ptl_pt_index_t index, ptl_match_bits_t bits, ptl_size_t roffs, void *uptr,
		   int nbio);
int swptl_func_atomic(struct swptl_md *mdh, ptl_size_t loffs, ptl_size_t len, ptl_ack_req_t ack,
		      ptl_process_t dest, ptl_pt_index_t index, ptl_match_bits_t bits,
		      ptl_size_t roffs, void *uptr, ptl_hdr_data_t hdr, ptl_op_t aop,
		      ptl_datatype_t atype, int nbio);
int swptl_func_fetch(struct swptl_md *get_mdh, ptl_size_t get_loffs, struct swptl_md *put_mdh,
		     ptl_size_t put_loffs, ptl_size_t len, ptl_process_t dest, ptl_pt_index_t index,
		     ptl_match_bits_t bits, ptl_size_t roffs, void *uptr, ptl_hdr_data_t hdr,
		     ptl_op_t aop, ptl_datatype_t atype, int nbio);
int swptl_func_swap(struct swptl_md *get_mdh, ptl_size_t get_loffs, struct swptl_md *put_mdh,
		    ptl_size_t put_loffs, ptl_size_t len, ptl_process_t dest, ptl_pt_index_t index,
		    ptl_match_bits_t bits, ptl_size_t roffs, void *uptr, ptl_hdr_data_t hdr,
		    const void *cst, ptl_op_t aop, ptl_datatype_t atype, int nbio);
int swptl_func_atsync(void);
int swptl_func_niatsync(struct swptl_ni *nih);

int swptl_func_trigput(struct swptl_md *mdh, ptl_size_t loffs, ptl_size_t len, ptl_ack_req_t ack,
		       ptl_process_t dest, ptl_pt_index_t index, ptl_match_bits_t bits,
		       ptl_size_t roffs, void *uptr, ptl_hdr_data_t hdr, struct swptl_ct *cth,
		       ptl_size_t thres, int nbio);

int swptl_func_trigget(struct swptl_md *mdh, ptl_size_t loffs, ptl_size_t len, ptl_process_t dest,
		       ptl_pt_index_t index, ptl_match_bits_t bits, ptl_size_t roffs, void *uptr,
		       struct swptl_ct *cth, ptl_size_t thres, int nbio);
int swptl_func_trigatomic(struct swptl_md *mdh, ptl_size_t loffs, ptl_size_t len, ptl_ack_req_t ack,
			  ptl_process_t dest, ptl_pt_index_t index, ptl_match_bits_t bits,
			  ptl_size_t roffs, void *uptr, ptl_hdr_data_t hdr, ptl_op_t aop,
			  ptl_datatype_t atype, struct swptl_ct *cth, ptl_size_t thres, int nbio);
int swptl_func_trigfetch(struct swptl_md *get_mdh, ptl_size_t get_loffs, struct swptl_md *put_mdh,
			 ptl_size_t put_loffs, ptl_size_t len, ptl_process_t dest,
			 ptl_pt_index_t index, ptl_match_bits_t bits, ptl_size_t roffs, void *uptr,
			 ptl_hdr_data_t hdr, ptl_op_t aop, ptl_datatype_t atype,
			 struct swptl_ct *cth, ptl_size_t thres, int nbio);
int swptl_func_trigswap(struct swptl_md *get_mdh, ptl_size_t get_loffs, struct swptl_md *put_mdh,
			ptl_size_t put_loffs, ptl_size_t len, ptl_process_t dest,
			ptl_pt_index_t index, ptl_match_bits_t bits, ptl_size_t roffs, void *uptr,
			ptl_hdr_data_t hdr, const void *cst, ptl_op_t aop, ptl_datatype_t atype,
			struct swptl_ct *cth, ptl_size_t thres, int nbio);
int swptl_func_trigctop(struct swptl_ct *cth, ptl_ct_event_t delta, struct swptl_ct *trig_cth,
			ptl_size_t thres, int set, int nbio);
int swptl_func_nfds(struct swptl_ni *nih);
int swptl_func_pollfd(struct swptl_ni *nih, struct pollfd *pfds, int events);
int swptl_func_revents(struct swptl_ni *nih, struct pollfd *pfds);
void swptl_func_waitcompl(struct swptl_ni *nih, unsigned int txcnt, unsigned int rxcnt);

int ptl_evtostr(unsigned int ni_options, ptl_event_t *e, char *msg);
void ptl_set_log_fn(int (*log_fn)(const char *fmt, ...) __attribute__((format(printf, 1, 2))));

#endif /* SWPTL4_H */
