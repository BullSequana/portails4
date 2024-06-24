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

struct bxipkt_ops;

/*
 * Note that this struct is _never_ directly passed, but will instead be embedded as the first field
 * in the transport specific options
 */
struct bxipkt_options {
	int debug; /* default: 0 */
	uint stats; /* default: false */
};

struct bxipkt_udp_options {
	struct bxipkt_options global;

	bool default_mtu; /* Use the default MTU (ETHERMTU) */
	const char *ip; /* IP address, must be provided */
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
int swptl_dev_new(struct swptl_ctx *ctx, int iface, int uid, int pid, size_t rdv_put,
		  struct swptl_dev **dev);

int swptl_func_libinit(struct swptl_options *opts, struct bximsg_options *msg_opts,
		       struct bxipkt_options *transport_opts, struct swptl_ctx **ctx);
void swptl_func_libfini(struct swptl_ctx *ctx);
void swptl_func_abort(struct swptl_ctx *ctx);
int swptl_func_setmemops(ptl_mem_ops_t *ops);

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
int swptl_func_pte_enable(struct swptl_ni *nih, ptl_pt_index_t index, int enable);

int swptl_func_getuid(struct swptl_ni *nih, ptl_uid_t *uid);
int swptl_func_getid(struct swptl_ni *nih, ptl_process_t *id);
int swptl_func_getphysid(struct swptl_ni *nih, ptl_process_t *id);
int swptl_func_gethwid(struct swptl_ni *nih, uint64_t *hwid, uint64_t *capabilities);

int swptl_func_md_bind(struct swptl_ni *ni, const struct swptl_md_params *mdpar,
		       struct swptl_md **retmd);
int swptl_func_md_release(struct swptl_md *md);

int swptl_func_append(struct swptl_ni *nih, ptl_index_t index, const struct swptl_me_params *mepar,
		      ptl_list_t list, void *uptr, struct swptl_me **mehret);

int swptl_func_unlink(struct swptl_me *meh);
int swptl_func_search(struct swptl_ni *nih, ptl_pt_index_t index,
		      const struct swptl_me_params *mepar, ptl_search_op_t sop, void *uptr);

int swptl_func_eq_alloc(struct swptl_ni *nih, ptl_size_t count, struct swptl_eq **reteq);
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
		   ptl_size_t roffs, void *uptr, ptl_hdr_data_t hdr);
int swptl_func_get(struct swptl_md *mdh, ptl_size_t loffs, ptl_size_t len, ptl_process_t dest,
		   ptl_pt_index_t index, ptl_match_bits_t bits, ptl_size_t roffs, void *uptr);
int swptl_func_atomic(struct swptl_md *mdh, ptl_size_t loffs, ptl_size_t len, ptl_ack_req_t ack,
		      ptl_process_t dest, ptl_pt_index_t index, ptl_match_bits_t bits,
		      ptl_size_t roffs, void *uptr, ptl_hdr_data_t hdr, ptl_op_t aop,
		      ptl_datatype_t atype);
int swptl_func_fetch(struct swptl_md *get_mdh, ptl_size_t get_loffs, struct swptl_md *put_mdh,
		     ptl_size_t put_loffs, ptl_size_t len, ptl_process_t dest, ptl_pt_index_t index,
		     ptl_match_bits_t bits, ptl_size_t roffs, void *uptr, ptl_hdr_data_t hdr,
		     ptl_op_t aop, ptl_datatype_t atype);
int swptl_func_swap(struct swptl_md *get_mdh, ptl_size_t get_loffs, struct swptl_md *put_mdh,
		    ptl_size_t put_loffs, ptl_size_t len, ptl_process_t dest, ptl_pt_index_t index,
		    ptl_match_bits_t bits, ptl_size_t roffs, void *uptr, ptl_hdr_data_t hdr,
		    const void *cst, ptl_op_t aop, ptl_datatype_t atype);
int swptl_func_atsync(void);
int swptl_func_niatsync(struct swptl_ni *nih);

int swptl_func_trigput(struct swptl_md *mdh, ptl_size_t loffs, ptl_size_t len, ptl_ack_req_t ack,
		       ptl_process_t dest, ptl_pt_index_t index, ptl_match_bits_t bits,
		       ptl_size_t roffs, void *uptr, ptl_hdr_data_t hdr, struct swptl_ct *cth,
		       ptl_size_t thres);

int swptl_func_trigget(struct swptl_md *mdh, ptl_size_t loffs, ptl_size_t len, ptl_process_t dest,
		       ptl_pt_index_t index, ptl_match_bits_t bits, ptl_size_t roffs, void *uptr,
		       struct swptl_ct *cth, ptl_size_t thres);
int swptl_func_trigatomic(struct swptl_md *mdh, ptl_size_t loffs, ptl_size_t len, ptl_ack_req_t ack,
			  ptl_process_t dest, ptl_pt_index_t index, ptl_match_bits_t bits,
			  ptl_size_t roffs, void *uptr, ptl_hdr_data_t hdr, ptl_op_t aop,
			  ptl_datatype_t atype, struct swptl_ct *cth, ptl_size_t thres);
int swptl_func_trigfetch(struct swptl_md *get_mdh, ptl_size_t get_loffs, struct swptl_md *put_mdh,
			 ptl_size_t put_loffs, ptl_size_t len, ptl_process_t dest,
			 ptl_pt_index_t index, ptl_match_bits_t bits, ptl_size_t roffs, void *uptr,
			 ptl_hdr_data_t hdr, ptl_op_t aop, ptl_datatype_t atype,
			 struct swptl_ct *cth, ptl_size_t thres);
int swptl_func_trigswap(struct swptl_md *get_mdh, ptl_size_t get_loffs, struct swptl_md *put_mdh,
			ptl_size_t put_loffs, ptl_size_t len, ptl_process_t dest,
			ptl_pt_index_t index, ptl_match_bits_t bits, ptl_size_t roffs, void *uptr,
			ptl_hdr_data_t hdr, const void *cst, ptl_op_t aop, ptl_datatype_t atype,
			struct swptl_ct *cth, ptl_size_t thres);
int swptl_func_trigctop(struct swptl_ct *cth, ptl_ct_event_t delta, struct swptl_ct *trig_cth,
			ptl_size_t thres, int set);
int swptl_func_nfds(struct swptl_ni *nih);
int swptl_func_pollfd(struct swptl_ni *nih, struct pollfd *pfds, int events);
int swptl_func_revents(struct swptl_ni *nih, struct pollfd *pfds);
void swptl_func_waitcompl(struct swptl_ni *nih, unsigned int txcnt, unsigned int rxcnt);

int PtlEvToStr(unsigned int ni_options, ptl_event_t *e, char *msg);
void ptl_set_log_fn(int (*log_fn)(const char *fmt, ...) __attribute__((format(printf, 1, 2))));

const char *PtlToStr(int rc, ptl_str_type_t type);

struct swptl_dev *swptl_dev_get(struct swptl_ni *ni);
void swptl_dev_del(struct swptl_dev *dev);
int swptl_dev_refs(struct swptl_dev *dev);

#endif /* SWPTL4_H */
