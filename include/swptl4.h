#ifndef SWPTL4_H
#define SWPTL4_H

#include <stddef.h>
#include <stdint.h>

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

struct bxipkt_ops {
	/* Library initialization. */
	int (*libinit)(void);

	/* Library finalization. */
	void (*libfini)(void);

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
	struct bxipkt_iface *(*init)(int vn, int nic_iface, int pid, int nbufs, void *arg,
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

/*
 * Returns PTL_PID_IN_USE or PTL_FAIL
 */
int swptl_dev_new(int iface, int pid, size_t rdv_put, struct swptl_dev **dev);

int swptl_func_libinit(struct bxipkt_ops *transport);
void swptl_func_libfini(void);
void swptl_func_abort(void);
int swptl_func_setmemops(struct ptl_mem_ops *);

int swptl_func_activate_add(void (*)(void *, unsigned int, int), void *, ptl_activate_hook_t *);
int swptl_func_activate_rm(ptl_activate_hook_t);

int swptl_func_ni_init(struct swptl_dev *, unsigned int, const ptl_ni_limits_t *, ptl_ni_limits_t *,
		       ptl_handle_ni_t *);
int swptl_func_ni_fini(ptl_handle_ni_t);
int swptl_func_ni_handle(ptl_handle_any_t, ptl_handle_ni_t *);
int swptl_func_ni_status(ptl_handle_ni_t, ptl_sr_index_t, ptl_sr_value_t *);

int swptl_func_setmap(ptl_handle_ni_t, ptl_size_t, const union ptl_process *);
int swptl_func_getmap(ptl_handle_ni_t, ptl_size_t, union ptl_process *, ptl_size_t *);

int swptl_func_pte_alloc(ptl_handle_ni_t, unsigned int, ptl_handle_eq_t, ptl_index_t,
			 ptl_index_t *);
int swptl_func_pte_free(ptl_handle_ni_t, ptl_index_t);
int swptl_func_pte_enable(ptl_handle_ni_t, ptl_pt_index_t, int, int);

int swptl_func_getuid(ptl_handle_ni_t, ptl_uid_t *);
int swptl_func_getid(ptl_handle_ni_t, ptl_process_t *);
int swptl_func_getphysid(ptl_handle_ni_t, ptl_process_t *);
int swptl_func_gethwid(ptl_handle_ni_t, uint64_t *, uint64_t *);

int swptl_func_mdbind(ptl_handle_ni_t, const ptl_md_t *, ptl_handle_md_t *);
int swptl_func_mdrelease(ptl_handle_md_t);

int swptl_func_append(ptl_handle_ni_t, ptl_index_t, const struct ptl_me *, ptl_list_t, void *,
		      ptl_handle_me_t *, int);

int swptl_func_unlink(ptl_handle_le_t);
int swptl_func_search(ptl_handle_ni_t, ptl_pt_index_t, const ptl_le_t *, ptl_search_op_t, void *,
		      int);

int swptl_func_eq_alloc(ptl_handle_ni_t, ptl_size_t, ptl_handle_eq_t *,
			void (*)(void *, ptl_handle_eq_t), void *, int);
int swptl_func_eq_free(ptl_handle_eq_t);
int swptl_func_eq_get(ptl_handle_eq_t, ptl_event_t *);
int swptl_func_eq_poll(const ptl_handle_eq_t *, unsigned int, ptl_time_t, ptl_event_t *,
		       unsigned int *);

int swptl_func_ct_alloc(ptl_handle_ni_t, ptl_handle_ct_t *);
int swptl_func_ct_free(ptl_handle_ct_t);
int swptl_func_ct_cancel(ptl_handle_ct_t);
int swptl_func_ct_get(ptl_handle_ct_t, ptl_ct_event_t *);
int swptl_func_ct_poll(const ptl_handle_ct_t *, const ptl_size_t *, unsigned int, ptl_time_t,
		       ptl_ct_event_t *, unsigned int *);
int swptl_func_ct_op(ptl_handle_ct_t, ptl_ct_event_t, int);

int swptl_func_put(ptl_handle_md_t, ptl_size_t, ptl_size_t, ptl_ack_req_t, ptl_process_t,
		   ptl_index_t, ptl_match_bits_t, ptl_size_t, void *, ptl_hdr_data_t, int);
int swptl_func_get(ptl_handle_md_t, ptl_size_t, ptl_size_t, ptl_process_t, ptl_pt_index_t,
		   ptl_match_bits_t, ptl_size_t, void *, int);
int swptl_func_atomic(ptl_handle_md_t, ptl_size_t, ptl_size_t, ptl_ack_req_t, ptl_process_t,
		      ptl_pt_index_t, ptl_match_bits_t, ptl_size_t, void *, ptl_hdr_data_t,
		      ptl_op_t, ptl_datatype_t, int);
int swptl_func_fetch(ptl_handle_md_t, ptl_size_t, ptl_handle_md_t, ptl_size_t, ptl_size_t,
		     ptl_process_t, ptl_pt_index_t, ptl_match_bits_t, ptl_size_t, void *,
		     ptl_hdr_data_t, ptl_op_t, ptl_datatype_t, int);
int swptl_func_swap(ptl_handle_md_t, ptl_size_t, ptl_handle_md_t, ptl_size_t, ptl_size_t,
		    ptl_process_t, ptl_pt_index_t, ptl_match_bits_t, ptl_size_t, void *,
		    ptl_hdr_data_t, const void *, ptl_op_t, ptl_datatype_t, int);
int swptl_func_atsync(void);
int swptl_func_niatsync(ptl_handle_ni_t);

int swptl_func_trigput(ptl_handle_md_t, ptl_size_t, ptl_size_t, ptl_ack_req_t, ptl_process_t,
		       ptl_index_t, ptl_match_bits_t, ptl_size_t, void *, ptl_hdr_data_t,
		       ptl_handle_ct_t, ptl_size_t, int);
int swptl_func_trigget(ptl_handle_md_t, ptl_size_t, ptl_size_t, ptl_process_t, ptl_pt_index_t,
		       ptl_match_bits_t, ptl_size_t, void *, ptl_handle_ct_t, ptl_size_t, int);
int swptl_func_trigatomic(ptl_handle_md_t, ptl_size_t, ptl_size_t, ptl_ack_req_t, ptl_process_t,
			  ptl_pt_index_t, ptl_match_bits_t, ptl_size_t, void *, ptl_hdr_data_t,
			  ptl_op_t, ptl_datatype_t, ptl_handle_ct_t, ptl_size_t, int);
int swptl_func_trigfetch(ptl_handle_md_t, ptl_size_t, ptl_handle_md_t, ptl_size_t, ptl_size_t,
			 ptl_process_t, ptl_pt_index_t, ptl_match_bits_t, ptl_size_t, void *,
			 ptl_hdr_data_t, ptl_op_t, ptl_datatype_t, ptl_handle_ct_t, ptl_size_t,
			 int);
int swptl_func_trigswap(ptl_handle_md_t, ptl_size_t, ptl_handle_md_t, ptl_size_t, ptl_size_t,
			ptl_process_t, ptl_pt_index_t, ptl_match_bits_t, ptl_size_t, void *,
			ptl_hdr_data_t, const void *, ptl_op_t, ptl_datatype_t, ptl_handle_ct_t,
			ptl_size_t, int);
int swptl_func_trigctop(ptl_handle_ct_t, ptl_ct_event_t, ptl_handle_ct_t, ptl_size_t, int, int);
int swptl_func_nfds(ptl_handle_ni_t);
int swptl_func_pollfd(ptl_handle_ni_t, struct pollfd *, int);
int swptl_func_revents(ptl_handle_ni_t, struct pollfd *);
void swptl_func_waitcompl(ptl_handle_ni_t, unsigned int, unsigned int);

int ptl_evtostr(unsigned int ni_options, ptl_event_t *e, char *msg);

#endif /* SWPTL4_H */
