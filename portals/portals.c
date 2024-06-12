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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#include "swptl4.h"

struct linked_list {
	ptl_interface_t num_interface;
	struct swptl_dev *dev;
	struct linked_list *next;
};

static struct linked_list *dev_list = NULL;
/* used to save the swptl_dev object of interfaces already used*/

static struct swptl_ctx *ctx_global;

struct swptl_dev *find_swptl_dev(struct linked_list *start, ptl_interface_t num_interface)
{
	if (!start) {
		return NULL;
	} else {
		if (start->num_interface == num_interface) {
			return start->dev;
		} else {
			return find_swptl_dev(start->next, num_interface);
		}
	}
}

static void add_swptl_dev(struct linked_list **start, struct swptl_dev *dev,
			  ptl_interface_t num_interface)
{
	struct linked_list *new_element = malloc(sizeof(*new_element));
	struct linked_list *current = *start;

	new_element->num_interface = num_interface;
	new_element->dev = dev;
	new_element->next = NULL;
	if (!current) {
		*start = new_element;
		return;
	}
	while (current->next != NULL) {
		current = current->next;
	}
	current->next = new_element;
}

static void remove_swptl_dev(struct linked_list **start, struct swptl_dev *dev)
{
	struct linked_list *current = *start;
	struct linked_list *next = current->next;

	if (current->dev == dev) {
		*start = next;
	} else {
		while (next != NULL && next->dev != dev) {
			struct linked_list *save = next;
			next = next->next;
			current = save;
		}
		if (next == NULL) {
			fprintf(stderr, "There isn't the specified dev in dev_list \n");
			abort();
		} else {
			next = next->next;
			free(current->next);
			current->next = next;
		}
	}
}

int PtlInit(void)
{
	struct swptl_options opts;
	struct bximsg_options msg_opts;
	struct bxipkt_udp_options transport_opts;

	bximsg_options_set_default(&msg_opts);
	bxipkt_options_set_default(&transport_opts.global);
	swptl_options_set_default(&opts);
	transport_opts.ip = "127.0.0";
	/* TODO: allow the user to choose the IP */
	return swptl_func_libinit(&opts, &msg_opts, &transport_opts.global, &ctx_global);
}

void PtlFini(void)
{
	struct linked_list *current = dev_list;
	while (current->next != NULL) {
		struct linked_list *n = current->next;
		free(current);
		current = n;
	}
	return swptl_func_libfini(ctx_global);
}

int PtlNIInit(ptl_interface_t iface, unsigned int options, ptl_pid_t pid,
	      const ptl_ni_limits_t *desired, ptl_ni_limits_t *actual, ptl_handle_ni_t *ni_handle)
{
	struct swptl_dev *dev = find_swptl_dev(dev_list, iface);
	unsigned int flags = options;
	struct swptl_ni *handle;
	int ret = 1;

	if (dev == NULL) {
		int uid = geteuid();
		size_t rdv_put = 0;
		ret = swptl_dev_new(ctx_global, iface, uid, pid, rdv_put, &dev);
		if (ret != PTL_OK) {
			fprintf(stderr, "Impossible to add the specify dev to dev_list");
		}
		add_swptl_dev(&dev_list, dev, iface);
	}
	ret = swptl_func_ni_init(dev, flags, desired, actual, &handle);
	ni_handle->handle = handle;
	return ret;
}

int PtlNIFini(ptl_handle_ni_t ni_handle)
{
	struct swptl_dev *dev = swptl_dev_get(ni_handle.handle);

	int ret = swptl_func_ni_fini(ni_handle.handle);
	if (swptl_dev_refs(dev) == 0) {
		remove_swptl_dev(&dev_list, dev);
		swptl_dev_del(dev);
	}
	return ret;
}

int PtlEQAlloc(ptl_handle_ni_t ni_handle, ptl_size_t c, ptl_handle_eq_t *eq_handle)
{
	struct swptl_ni *nih = ni_handle.handle;
	struct swptl_eq *reteq;

	int ret = swptl_func_eq_alloc(nih, c, &reteq);
	eq_handle->handle = reteq;
	return ret;
}

int PtlEQFree(ptl_handle_eq_t eq_handle)
{
	struct swptl_eq *eqh = eq_handle.handle;

	return swptl_func_eq_free(eqh);
}

int PtlPTAlloc(ptl_handle_ni_t ni_handle, unsigned int options, ptl_handle_eq_t eq_handle,
	       ptl_pt_index_t pt_index_req, ptl_pt_index_t *pt_index)
{
	struct swptl_ni *nih = ni_handle.handle;
	struct swptl_eq *eqh = eq_handle.handle;

	int ret = swptl_func_pte_alloc(nih, options, eqh, pt_index_req, pt_index);
	return ret;
}

int PtlPTFree(ptl_handle_ni_t ni_handle, ptl_pt_index_t pt_index)
{
	struct swptl_ni *nih = ni_handle.handle;

	return swptl_func_pte_free(nih, pt_index);
}

int PtlLEAppend(ptl_handle_ni_t ni_handle, ptl_pt_index_t pt_index, const ptl_le_t *le,
		ptl_list_t ptl_list, void *user_ptr, ptl_handle_le_t *le_handle)
{
	struct swptl_ni *nih = ni_handle.handle;
	struct swptl_me_params mepar = { .start = le->start,
					 .length = le->length,
					 .ct_handle = le->ct_handle.handle,
					 .uid = le->uid,
					 .options = le->options };
	struct swptl_me *mehret;

	int ret = swptl_func_append(nih, pt_index, &mepar, ptl_list, user_ptr, &mehret);
	le_handle->handle = mehret;
	return ret;
}

int PtlMEAppend(ptl_handle_ni_t ni_handle, ptl_pt_index_t pt_index, const ptl_le_t *me,
		ptl_list_t ptl_list, void *user_ptr, ptl_handle_le_t *me_handle)
{
	struct swptl_ni *nih = ni_handle.handle;
	struct swptl_me_params mepar = { .start = me->start,
					 .length = me->length,
					 .ct_handle = me->ct_handle.handle,
					 .uid = me->uid,
					 .options = me->options,
					 .match_id = me->match_id,
					 .match_bits = me->match_bits,
					 .ignore_bits = me->ignore_bits,
					 .min_free = me->min_free };
	struct swptl_me *mehret;

	int ret = swptl_func_append(nih, pt_index, &mepar, ptl_list, user_ptr, &mehret);
	me_handle->handle = mehret;
	return ret;
}

int PtlEQWait(ptl_handle_eq_t eq_handle, ptl_event_t *event)
{
	const struct swptl_eq *eqhlist = eq_handle.handle;
	unsigned int size = 1;
	ptl_time_t timeout = PTL_TIME_FOREVER;
	unsigned int rwhich;

	int ret = swptl_func_eq_poll(ctx_global, &eqhlist, size, timeout, event, &rwhich);
	return ret;
}

int PtlEQPoll(const ptl_handle_eq_t *eq_handles, unsigned int size, ptl_time_t timeout,
	      ptl_event_t *event, unsigned int *which)
{
	const struct swptl_eq **eqhlist = malloc(sizeof(*eqhlist) * size);
	for (int i = 0; i < size; i++) {
		eqhlist[i] = eq_handles[i].handle;
	}
	int ret = swptl_func_eq_poll(ctx_global, eqhlist, size, timeout, event, which);
	free((void *)eqhlist);
	return ret;
}

int PtlEQGet(ptl_handle_eq_t eq_handle, ptl_event_t *event)
{
	struct swptl_eq *eqh = eq_handle.handle;

	return swptl_func_eq_get(eqh, event);
}

int PtlPut(ptl_handle_md_t md_handle, ptl_size_t local_offset, ptl_size_t length,
	   ptl_ack_req_t ack_req, ptl_process_t target_id, ptl_pt_index_t pt_index,
	   ptl_match_bits_t match_bits, ptl_size_t remote_offset, void *user_ptr,
	   ptl_hdr_data_t hdr_data)
{
	struct swptl_md *mdh = md_handle.handle;

	return swptl_func_put(mdh, local_offset, length, ack_req, target_id, pt_index, match_bits,
			      remote_offset, user_ptr, hdr_data);
}

int PtlGetUid(ptl_handle_ni_t ni_handle, ptl_uid_t *uid)
{
	struct swptl_ni *nih = ni_handle.handle;

	return swptl_func_getuid(nih, uid);
}

int PtlGetPhysId(ptl_handle_ni_t ni_handle, ptl_process_t *id)
{
	struct swptl_ni *nih = ni_handle.handle;

	return swptl_func_getphysid(nih, id);
}

int PtlGetId(ptl_handle_ni_t ni_handle, ptl_process_t *id)
{
	return swptl_func_getid(ni_handle.handle, id);
}

int PtlMDBind(ptl_handle_ni_t ni_handle, const ptl_md_t *md, ptl_handle_md_t *md_handle)
{
	struct swptl_ni *ni = ni_handle.handle;
	const struct swptl_md_params mdpar = { .start = md->start,
					       .length = md->length,
					       .options = md->options,
					       .eq_handle = md->eq_handle.handle,
					       .ct_handle = md->ct_handle.handle };
	struct swptl_md *retmd;

	int ret = swptl_func_md_bind(ni, &mdpar, &retmd);
	md_handle->handle = retmd;
	return ret;
}

int PtlMDRelease(ptl_handle_md_t md_handle)
{
	struct swptl_md *md = md_handle.handle;

	return swptl_func_md_release(md);
}

int PtlLESearch(ptl_handle_ni_t ni_handle, ptl_pt_index_t pt_index, const ptl_le_t *le,
		ptl_search_op_t ptl_search_op, void *user_ptr)
{
	struct swptl_ni *nih = ni_handle.handle;
	struct swptl_me_params mepar = { .start = le->start,
					 .length = le->length,
					 .ct_handle = le->ct_handle.handle,
					 .uid = le->uid,
					 .options = le->options };
	return swptl_func_search(nih, pt_index, &mepar, ptl_search_op, user_ptr);
}

int PtlMESearch(ptl_handle_ni_t ni_handle, ptl_pt_index_t pt_index, const ptl_me_t *me,
		ptl_search_op_t ptl_search_op, void *user_ptr)
{
	struct swptl_ni *nih = ni_handle.handle;
	struct swptl_me_params mepar = { .start = me->start,
					 .length = me->length,
					 .ct_handle = me->ct_handle.handle,
					 .uid = me->uid,
					 .options = me->options,
					 .match_id = me->match_id,
					 .match_bits = me->match_bits,
					 .ignore_bits = me->ignore_bits,
					 .min_free = me->min_free };
	return swptl_func_search(nih, pt_index, &mepar, ptl_search_op, user_ptr);
}

int PtlLEUnlink(ptl_handle_le_t le_handle)
{
	struct swptl_me *meh = le_handle.handle;

	return swptl_func_unlink(meh);
}

int PtlMEUnlink(ptl_handle_le_t me_handle)
{
	struct swptl_me *meh = me_handle.handle;

	return swptl_func_unlink(meh);
}

int PtlSetMap(ptl_handle_ni_t ni_handle, ptl_size_t map_size, const ptl_process_t *mapping)
{
	struct swptl_ni *nih = ni_handle.handle;

	return swptl_func_setmap(nih, map_size, mapping);
}

int PtlGetMap(ptl_handle_ni_t ni_handle, ptl_size_t map_size, ptl_process_t *mapping,
	      ptl_size_t *actual_map_size)
{
	struct swptl_ni *nih = ni_handle.handle;

	return swptl_func_getmap(nih, map_size, mapping, actual_map_size);
}

int PtlPTEnable(ptl_handle_ni_t ni_handle, ptl_pt_index_t pt_index)
{
	struct swptl_ni *nih = ni_handle.handle;

	return swptl_func_pte_enable(nih, pt_index, true);
}

int PtlPTDisable(ptl_handle_ni_t ni_handle, ptl_pt_index_t pt_index)
{
	struct swptl_ni *nih = ni_handle.handle;

	return swptl_func_pte_enable(nih, pt_index, false);
}

int PtlNIHandle(ptl_handle_any_t handle, ptl_handle_ni_t *ni_handle)
{
	struct swptl_ni *nih = ni_handle->handle;

	return swptl_func_ni_handle(&handle, &nih);
}

int PtlCTAlloc(ptl_handle_ni_t ni_handle, ptl_handle_ct_t *ct_handle)
{
	struct swptl_ni *nih = ni_handle.handle;
	struct swptl_ct *retct;

	int ret = swptl_func_ct_alloc(nih, &retct);
	ct_handle->handle = retct;
	return ret;
}

int PtlCTFree(ptl_handle_ct_t ct_handle)
{
	struct swptl_ct *cth = ct_handle.handle;

	return swptl_func_ct_free(cth);
}

int PtlCTCancelTriggered(ptl_handle_ct_t ct_handle)
{
	struct swptl_ct *cth = ct_handle.handle;

	return swptl_func_ct_cancel(cth);
}

int PtlCTGet(ptl_handle_ct_t ct_handle, ptl_ct_event_t *event)
{
	struct swptl_ct *cth = ct_handle.handle;

	return swptl_func_ct_get(cth, event);
}

int PtlCTPoll(const ptl_handle_ct_t *ct_handles, const ptl_size_t *tests, unsigned int size,
	      ptl_time_t timeout, ptl_ct_event_t *event, unsigned int *which)
{
	const struct swptl_ct **cth = malloc(sizeof(*cth) * size);
	for (int i = 0; i < size; i++) {
		cth[i] = ct_handles[i].handle;
	}
	int ret = swptl_func_ct_poll(ctx_global, cth, tests, size, timeout, event, which);
	free((void *)cth);
	return ret;
}

int PtlCTWait(ptl_handle_ct_t ct_handle, ptl_size_t test, ptl_ct_event_t *event)
{
	const struct swptl_ct *cth = ct_handle.handle;
	unsigned int rwhich;

	return swptl_func_ct_poll(ctx_global, &cth, &test, 1, PTL_TIME_FOREVER, event, &rwhich);
}

int PtlCTInc(ptl_handle_ct_t ct_handle, ptl_ct_event_t increment)
{
	struct swptl_ct *ct = ct_handle.handle;

	return swptl_func_ct_op(ct, increment, 1);
}

int PtlCTSet(ptl_handle_ct_t ct_handle, ptl_ct_event_t increment)
{
	struct swptl_ct *ct = ct_handle.handle;

	return swptl_func_ct_op(ct, increment, 0);
}

void PtlAbort(void)
{
	return swptl_func_abort(ctx_global);
}

int PtlGet(ptl_handle_md_t md_handle, ptl_size_t local_offset, ptl_size_t length,
	   ptl_process_t target_id, ptl_pt_index_t pt_index, ptl_match_bits_t match_bits,
	   ptl_size_t remote_offset, void *user_ptr)
{
	struct swptl_md *mdh = md_handle.handle;

	return swptl_func_get(mdh, local_offset, length, target_id, pt_index, match_bits,
			      remote_offset, user_ptr);
}

int PtlAtomic(ptl_handle_md_t md_handle, ptl_size_t local_offset, ptl_size_t length,
	      ptl_ack_req_t ack_req, ptl_process_t target_id, ptl_pt_index_t pt_index,
	      ptl_match_bits_t match_bits, ptl_size_t remote_offset, void *user_ptr,
	      ptl_hdr_data_t hdr_data, ptl_op_t operation, ptl_datatype_t datatype)
{
	struct swptl_md *mdh = md_handle.handle;

	return swptl_func_atomic(mdh, local_offset, length, ack_req, target_id, pt_index,
				 match_bits, remote_offset, user_ptr, hdr_data, operation,
				 datatype);
}

int PtlFetchAtomic(ptl_handle_md_t get_md_handle, ptl_size_t local_get_offset,
		   ptl_handle_md_t put_md_handle, ptl_size_t local_put_offset, ptl_size_t length,
		   ptl_process_t target_id, ptl_pt_index_t pt_index, ptl_match_bits_t match_bits,
		   ptl_size_t remote_offset, void *user_ptr, ptl_hdr_data_t hdr_data,
		   ptl_op_t operation, ptl_datatype_t datatype)
{
	struct swptl_md *mdh = get_md_handle.handle;
	struct swptl_md *put_mdh = put_md_handle.handle;

	return swptl_func_fetch(mdh, local_get_offset, put_mdh, local_put_offset, length, target_id,
				pt_index, match_bits, remote_offset, user_ptr, hdr_data, operation,
				datatype);
}

int PtlSwap(ptl_handle_md_t get_md_handle, ptl_size_t local_get_offset,
	    ptl_handle_md_t put_md_handle, ptl_size_t local_put_offset, ptl_size_t length,
	    ptl_process_t target_id, ptl_pt_index_t pt_index, ptl_match_bits_t match_bits,
	    ptl_size_t remote_offset, void *user_ptr, ptl_hdr_data_t hdr_data, const void *operand,
	    ptl_op_t operation, ptl_datatype_t datatype)
{
	struct swptl_md *mdh = get_md_handle.handle;
	struct swptl_md *put_mdh = put_md_handle.handle;

	return swptl_func_swap(mdh, local_get_offset, put_mdh, local_put_offset, length, target_id,
			       pt_index, match_bits, remote_offset, user_ptr, hdr_data, operand,
			       operation, datatype);
}
