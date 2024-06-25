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
