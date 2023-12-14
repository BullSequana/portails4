/*
 * Copyright (c) 2003-2010 Alexandre Ratchov <alex@caoua.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * trivial timeouts implementation.
 *
 * A timeout is used to schedule the call of a routine (the callback)
 * there is a global list of timeouts that is processed inside the
 * event loop ie mux_run(). Timeouts work as follows:
 *
 *	first the timo structure must be initialized with timo_set()
 *
 *	then the timeout is scheduled (only once) with timo_add()
 *
 *	if the timeout expires, the call-back is called; then it can
 *	be scheduled again if needed. It's OK to reschedule it again
 *	from the callback
 *
 *	the timeout can be aborted with timo_del(), it is OK to try to
 *	abort a timout that has expired
 *
 */
#include <sys/time.h>
#include <string.h>
#include "utils.h"
#include "timo.h"
#include "ptl_log.h"

unsigned timo_debug = 0;

pthread_mutex_t timo_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
struct timo *timo_queue;


/*
 * Retrieve the time elapsed since the beginning of the program,
 * in microsecondes.
 */
unsigned long long timo_gettime(void)
{
	unsigned long long date;
	struct timeval tv;

	if (gettimeofday(&tv, NULL) < 0)
		ptl_panic("get_tim_date: gettimeofday failed\n");

	date = tv.tv_sec * 1000000L + tv.tv_usec;

	return date;
}

/*
 * initialise a timeout structure, arguments are callback and argument
 * that will be passed to the callback
 */
void
timo_set(struct timo *o, void (*cb)(void *), void *arg)
{
	o->cb = cb;
	o->arg = arg;
	o->set = 0;
}

/*
 * schedule the callback in 'delta' 24-th of microseconds. The timeout
 * must not be already scheduled
 */
void
timo_add(struct timo *o, unsigned delta)
{
	struct timo **i;
	unsigned long long expire;

	ptl_mutex_lock(&timo_queue_mutex, __func__);

#ifdef TIMO_DEBUG
	if (o->set)
		ptl_panic("timo_add: already set\n");

	if (delta == 0)
		ptl_panic("timo_add: zero timeout is evil\n");
#endif
	expire = timo_gettime() + delta;
	for (i = &timo_queue; *i != NULL; i = &(*i)->next) {
		if ((*i)->expire > expire)
			break;
	}
	o->set = 1;
	o->expire = expire;
	o->next = *i;
	*i = o;

	ptl_mutex_unlock(&timo_queue_mutex, __func__);
}

/*
 * abort a scheduled timeout
 */
void
timo_del(struct timo *o)
{
	struct timo **i;

	ptl_mutex_lock(&timo_queue_mutex, __func__);

	for (i = &timo_queue; *i != NULL; i = &(*i)->next) {
		if (*i == o) {
			*i = o->next;
			o->set = 0;
			ptl_mutex_unlock(&timo_queue_mutex, __func__);
			return;
		}
	}

	ptl_mutex_unlock(&timo_queue_mutex, __func__);

	if (timo_debug)
		ptl_log("timo_del: not found\n");
}

/*
 * routine to be called by the timer when 'delta' 24-th of microsecond
 * elapsed. This routine updates time referece used by timeouts and
 * calls expired timeouts
 */
void
timo_update()
{
	struct timo *to;
	unsigned long long now;

	now = timo_gettime();

	ptl_mutex_lock(&timo_queue_mutex, __func__);

	/*
	 * remove from the queue and run expired timeouts
	 */
	while (timo_queue != NULL) {
		if (now < timo_queue->expire)
			break;
		to = timo_queue;
		timo_queue = to->next;
		to->set = 0;
		ptl_mutex_unlock(&timo_queue_mutex, __func__);
		to->cb(to->arg);
		ptl_mutex_lock(&timo_queue_mutex, __func__);
	}

	ptl_mutex_unlock(&timo_queue_mutex, __func__);
}

/*
 * initialize timeout queue
 */
void
timo_init(void)
{
	timo_queue = NULL;
}

/*
 * destroy timeout queue
 */
void
timo_done(void)
{
	if (timo_queue != NULL)
		ptl_panic("timo_done: timo_queue not empty!\n");

	timo_queue = (struct timo *)0xdeadbeef;
}
