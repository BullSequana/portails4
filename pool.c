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
 * a pool is a large memory block (the pool) that is split into
 * small blocks of equal size (pools entries). Its used for
 * fast allocation of pool entries. Free enties are on a singly
 * linked list
 */
#include <string.h>
#include "utils.h"
#include "pool.h"
#include "ptl_log.h"

/*
 * initialises a pool of "itemnum" elements of size "itemsize"
 */
void
pool_init(struct pool *o, char *name, size_t itemsize, size_t itemnum)
{
	size_t i;
	struct poolent *p;

	o->data = xmalloc(itemsize * itemnum, name);
	if (!o->data)
		ptl_panic("pool_init(%s): out of memory\n", name);

	o->first = NULL;
	o->name = name;
#ifdef POOL_DEBUG
	o->itemsize = itemsize;
	o->itemnum = itemnum;
	o->maxused = 0;
	o->used = 0;
	o->newcnt = 0;
#endif

	/*
	 * create a linked list of all entries
	 */
	p = o->data;
	for (i = itemnum; i != 0; i--) {
		p->next = o->first;
		p->version = 0;
		o->first = (struct poolent *)p;
		p = (struct poolent *)((size_t)p + itemsize);
	}
}

/*
 * free the given pool
 */
void
pool_done(struct pool *o)
{
	xfree(o->data);
#ifdef POOL_DEBUG
	if (o->used != 0) {
		ptl_log("pool_done(%s): WARNING %lu items still allocated\n",
			o->name, o->used);
	}
#endif
}

/*
 * allocate an entry from the pool: just unlink
 * it from the free list and return the pointer
 */
void *
pool_get(struct pool *o)
{
	struct poolent *e;

	if (!o->first)
		ptl_panic("pool_get(%s): pool is empty\n", o->name);

	/*
	 * unlink from the free list
	 */
	e = o->first;
	o->first = e->next;

#ifdef POOL_DEBUG
	/*
	 * overwrite the entry with garbage so any attempt to use a
	 * free entry will probably segfault
	 */
	memset((unsigned char *)e + sizeof(struct poolent), 0xd0,
	    o->itemsize - sizeof(struct poolent));

	o->newcnt++;
	o->used++;
	if (o->used > o->maxused)
		o->maxused = o->used;
#endif
	return e;
}

/*
 * free an entry: just link it again on the free list
 */
void
pool_put(struct pool *o, void *p)
{
	struct poolent *e = (struct poolent *)p;

	/*
	 * increment generation version
	 */
	e->version++;

#ifdef POOL_DEBUG
	/*
	 * check if we aren't trying to free more
	 * entries than the poll size
	 */
	if (o->used == 0)
		ptl_panic("pool_put(%s): pool is full\n", o->name);

	o->used--;

	/*
	 * overwrite the entry with garbage so any attempt to use a
	 * free entry will probably segfault
	 */
	memset((unsigned char *)e + sizeof(struct poolent), 0xdf,
	    o->itemsize - sizeof(struct poolent));
#endif

	/*
	 * link on the free list
	 */
	e->next = o->first;
	o->first = e;
}
