/*
 * Copyright (c) 2018 Bull S.A.S
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bximsg.h"
#include "utils.h"
#include "bximsg_wthr.h"
#include "ptl_log.h"

#ifdef DEBUG
/*
 * log with 'ptl_log' and the given debug level
 */
#define LOGN(n, ...)                                                                               \
	do {                                                                                       \
		if (bximsg_wthr_debug >= (n))                                                      \
			ptl_log(__VA_ARGS__);                                                      \
	} while (0)

#define LOG(...) LOGN(1, __VA_ARGS__)
#else
#define LOGN(n, ...)                                                                               \
	do {                                                                                       \
	} while (0)
#define LOG(...)                                                                                   \
	do {                                                                                       \
	} while (0)
#endif

/* work item */
struct work_item {
	/* Next item in free list or worker queue */
	struct work_item *next;
	const void *src;
	void *dest;
	int len;
	volatile uint64_t *pending_memcpy;
};

struct wthread {
	/* Queues of free and active work items */
	struct work_item *wi_array;
	/* Head of the worker queue */
	struct work_item *workqhead;
	/* Tail of the worker queue */
	struct work_item **workqtail;
	/* List of free work items */
	struct work_item *freehead;
	/* worker threads wait cond */
	pthread_cond_t cond;
	/* The mutex that protect access to this structure */
	pthread_mutex_t mutex;
	/* The thread */
	pthread_t wthread;
};

static struct wthread *wthreads;
static unsigned int num_wthreads;

static volatile int stop;
#ifdef DEBUG
static int bximsg_wthr_debug;
#endif
#include "ptl_getenv.h"

unsigned int bximsg_async_memcpy_min_msg_size;
static unsigned int async_memcpy_min_buf_size;

static int init_wthread(struct wthread *, unsigned int, pthread_attr_t *);
static void *bximsg_handle_work(void *arg);

static int get_nth_cpu_from_cpuset(unsigned int cpu, cpu_set_t *cpus)
{
	unsigned int i;
	unsigned int cpu_found = 0;

	if (cpu >= CPUSET_COUNT(cpus))
		goto error;

	for (i = 0; i < BXIMSG_MAX_CPU_NUM; i++) {
		if (!CPUSET_ISSET(i, cpus))
			continue;

		if (cpu_found == cpu)
			return i;

		cpu_found++;
	}

error:
	LOGN(2, "%s: bad cpu index (%d >= %d)\n", __func__, cpu, CPUSET_COUNT(cpus));
	return -1;
}

static int allocate_cpu_from_cpuset(int cpu, cpu_set_t *cpus, pthread_attr_t *pattr)
{
	cpu_set_t *target_cpu_set;
	int target_cpu = get_nth_cpu_from_cpuset(cpu, cpus);

	if (target_cpu < 0)
		return 1;

	LOGN(3, "%s: cpu: %d allocated for thread: %d\n", __func__, target_cpu, cpu);

	target_cpu_set = CPU_ALLOC(BXIMSG_MAX_CPU_NUM);

	if (target_cpu_set == NULL) {
		LOGN(2, "%s: failed to allocate cpuset\n", __func__);
		return 1;
	}

	CPUSET_ZERO(target_cpu_set);
	CPUSET_SET(target_cpu, target_cpu_set);

	pthread_attr_setaffinity_np(pattr, CPU_SET_SIZE, target_cpu_set);

	CPU_FREE(target_cpu_set);
	return 0;
}

static int init_binding(cpu_set_t **cpus, pthread_attr_t *pattr)
{
	cpu_set_t *main_cpu_set;
	int target_main_cpu;
	const char *env;
	unsigned int main_thread_binding = 1;
	int rc = 1;

	*cpus = CPU_ALLOC(BXIMSG_MAX_CPU_NUM);
	main_cpu_set = CPU_ALLOC(BXIMSG_MAX_CPU_NUM);
	if (*cpus == NULL || main_cpu_set == NULL) {
		LOGN(2, "%s: cpuset allocation failed, no binding\n", __func__);
		goto error;
	}

	/* get cpu set */
	if (sched_getaffinity(0, CPU_SET_SIZE, *cpus)) {
		LOGN(2, "%s: getaffinity failed, no binding\n", __func__);
		goto error;
	}

	LOGN(3, "getaffinity: %d cpu allocated for %d threads\n", CPUSET_COUNT(*cpus),
	     num_wthreads + 1);

	if (CPUSET_COUNT(*cpus) != num_wthreads + 1) {
		LOGN(2, "%s: nb cpu != nb thread, no binding\n", __func__);
		goto error;
	}

	pthread_attr_init(pattr);

	env = ptl_getenv("BXIMSG_MAIN_THREAD_BINDING");
	if (env)
		sscanf(env, "%u", &main_thread_binding);

	if (!main_thread_binding) {
		rc = 0;
		goto error;
	}

	target_main_cpu = get_nth_cpu_from_cpuset(0, *cpus);

	if (target_main_cpu < 0)
		goto error;

	CPUSET_ZERO(main_cpu_set);
	CPUSET_SET(target_main_cpu, main_cpu_set);

	if (!sched_setaffinity(0, CPU_SET_SIZE, main_cpu_set))
		LOGN(3, "cpu: %d allocated for main thread\n", target_main_cpu);

	rc = 0;

error:
	CPU_FREE(main_cpu_set);
	return rc;
}

int bximsg_init_wthreads(void)
{
	int rv = 0;
	int i;
	const char *env;
	unsigned int num_wi = 0;
	unsigned int thread_binding = 1;
	cpu_set_t *cpus = NULL;
	pthread_attr_t attr;

#ifdef DEBUG
	env = getenv("BXIMSG_THREAD_DEBUG");
	if (env)
		sscanf(env, "%d", &bximsg_wthr_debug);
#endif

	num_wthreads = BXIMSG_DEFAULT_WTHREADS;
	env = ptl_getenv("BXIMSG_NUM_WTHREADS");
	if (env)
		sscanf(env, "%u", &num_wthreads);

	if (num_wthreads == 0)
		return 0;

	if (num_wthreads > BXIMSG_MAX_WTHREADS)
		num_wthreads = BXIMSG_MAX_WTHREADS;

	env = ptl_getenv("BXIMSG_NUM_WI");
	if (env)
		sscanf(env, "%u", &num_wi);
	if (num_wi == 0)
		num_wi = BXIMSG_NUM_WI;

	env = ptl_getenv("BXIMSG_THREAD_BINDING");
	if (env)
		sscanf(env, "%u", &thread_binding);

	bximsg_async_memcpy_min_msg_size = BXIMSG_ASYNC_MEMCPY_MIN_MSG_SIZE;
	env = ptl_getenv("BXIMSG_ASYNC_MEMCPY_MIN_MSG_SIZE");
	if (env)
		sscanf(env, "%u", &bximsg_async_memcpy_min_msg_size);

	async_memcpy_min_buf_size = BXIMSG_ASYNC_MEMCPY_MIN_BUF_SIZE;
	env = ptl_getenv("BXIMSG_ASYNC_MEMCPY_MIN_BUF_SIZE");
	if (env)
		sscanf(env, "%u", &async_memcpy_min_buf_size);

	/* initialize work queues */
	wthreads = malloc(num_wthreads * sizeof(*wthreads));
	if (wthreads == NULL) {
		ptl_log("wthreads alloc failed\n");
		return 1;
	}

	if (thread_binding && init_binding(&cpus, &attr))
		thread_binding = 0;

	for (i = 0; i < num_wthreads; i++) {
		if (thread_binding && !allocate_cpu_from_cpuset(i + 1, cpus, &attr))
			rv = init_wthread(&wthreads[i], num_wi, &attr);
		else
			rv = init_wthread(&wthreads[i], num_wi, NULL);

		if (rv != 0) {
			num_wthreads = i;
			bximsg_fini_wthreads();
			break;
		}
	}

	if (cpus)
		CPU_FREE(cpus);

	return rv;
}

void bximsg_fini_wthreads(void)
{
	int i;
	struct wthread *my_wthreads;
	unsigned int my_num_wthreads;

	stop = 1;

	my_num_wthreads = num_wthreads;
	num_wthreads = 0;

	my_wthreads = wthreads;
	wthreads = NULL;

	if (my_num_wthreads == 0)
		return;

	for (i = 0; i < my_num_wthreads; i++) {
		pthread_cond_signal(&my_wthreads[i].cond);
		pthread_join(my_wthreads[i].wthread, NULL);

		pthread_cond_destroy(&my_wthreads[i].cond);
		pthread_mutex_destroy(&my_wthreads[i].mutex);
		if (my_wthreads[i].wi_array)
			free(my_wthreads[i].wi_array);
	}

	free(my_wthreads);
}

void bximsg_async_memcpy(void *dest, const void *src, size_t len, unsigned int pkt_index,
			 volatile uint64_t *pending_memcpy)
{
	struct wthread *wthread;
	struct work_item *wi = NULL;

	if (num_wthreads == 0 || pending_memcpy == NULL || len < async_memcpy_min_buf_size ||
	    stop) {
		memcpy(dest, src, len);
		return;
	}

	wthread = &wthreads[pkt_index % num_wthreads];
	pthread_mutex_lock(&wthread->mutex);

	wi = wthread->freehead;
	if (wi != NULL) {
		wthread->freehead = wi->next;
	} else {
		/* Unlock as soon as possible */
		pthread_mutex_unlock(&wthread->mutex);
		LOGN(2, "WARNING: empty free list for asynchronous memory copy\n");
		memcpy(dest, src, len);
		return;
	}

	wi->src = src;
	wi->dest = dest;
	wi->len = len;
	wi->pending_memcpy = pending_memcpy;

	/* Increment the number of pending memcpy */
	VAL_ATOMIC_ADD(*pending_memcpy, 1);

	/* Enqueue a work item to be processed by a worker thread */
	wi->next = NULL;
	*wthread->workqtail = wi;
	wthread->workqtail = &wi->next;

	/*
	 * We need to wake up the thread only if we
	 * enqueue the first entry in the worker queue.
	 */
	if (wthread->workqhead->next == NULL)
		pthread_cond_signal(&wthread->cond);

	pthread_mutex_unlock(&wthread->mutex);
}

static int init_wthread(struct wthread *wthread, unsigned int num_wi, pthread_attr_t *attr)
{
	struct work_item *wi;
	int i;

	if (pthread_cond_init(&wthread->cond, NULL)) {
		ptl_log("condition initialization failed\n");
		goto error;
	}

	if (pthread_mutex_init(&wthread->mutex, NULL)) {
		ptl_log("mutex initialization failed\n");
		goto cond_destroy;
	}

	/* allocate free list of work item */
	wthread->wi_array = malloc(sizeof(struct work_item) * num_wi);
	if (wthread->wi_array == NULL) {
		ptl_log("memory allocation failed\n");
		goto mutex_destroy;
	}

	wthread->freehead = wthread->wi_array;
	for (i = 0, wi = wthread->freehead; i < num_wi - 1; i++, wi = wi->next)
		wi->next = &wthread->freehead[i + 1];

	wi->next = NULL;

	wthread->workqhead = NULL;
	wthread->workqtail = &wthread->workqhead;

	if (pthread_create(&wthread->wthread, attr, bximsg_handle_work, wthread)) {
		ptl_log("worker thread creation failed\n");
		goto free_wi_array;
	}

	return 0;

free_wi_array:
	free(wthread->wi_array);
mutex_destroy:
	pthread_mutex_destroy(&wthread->mutex);
cond_destroy:
	pthread_cond_destroy(&wthread->cond);
error:
	return 1;
}

static void *bximsg_handle_work(void *arg)
{
	struct wthread *wthread = arg;
	struct work_item *wi;
	const void *src;
	void *dest;
	int len;
	volatile uint64_t *pending_memcpy;

	LOGN(2, "bximsg worker thread started\n");
	pthread_mutex_lock(&wthread->mutex);

	while (!stop) {
		wi = wthread->workqhead;
		if (wi != NULL) {
			wthread->workqhead = wi->next;
			if (wi->next == NULL)
				wthread->workqtail = &wthread->workqhead;
		} else {
			pthread_cond_wait(&wthread->cond, &wthread->mutex);
			continue;
		}

		/* Save content of work item */
		src = wi->src;
		dest = wi->dest;
		len = wi->len;
		pending_memcpy = wi->pending_memcpy;

		/* Add work item in free list */
		wi->next = wthread->freehead;
		wthread->freehead = wi;

		pthread_mutex_unlock(&wthread->mutex);

		/* handle work item */
		memcpy(dest, src, len);

		/* Decrement the number of pending memcpy */
		VAL_ATOMIC_SUB(*pending_memcpy, 1);

		pthread_mutex_lock(&wthread->mutex);
	}

	pthread_mutex_unlock(&wthread->mutex);

	return 0;
}
