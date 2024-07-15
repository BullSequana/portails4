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

#ifndef MIDISH_POOL_H
#define MIDISH_POOL_H

#include <stddef.h>

/*
 * entry from the pool. Any real pool entry is cast to this structure
 * by the pool code. The actual size of a pool entry is in 'itemsize'
 * field of the pool structure
 */
struct poolent {
	struct poolent *next;
	size_t version;
};

/*
 * the pool is a linked list of 'itemnum' blocks of size
 * 'itemsize'. The pool name is for debugging prurposes only
 */
struct pool {
	void *data; /* memory block of the pool */
	struct poolent *first; /* head of linked list */
#ifdef POOL_DEBUG
	size_t maxused; /* max pool usage */
	size_t used; /* current pool usage */
	size_t newcnt; /* current items allocated */
	size_t itemnum; /* total number of entries */
	size_t itemsize; /* size of a sigle entry */
#endif
	char *name; /* name of the pool */
};

void pool_init(struct pool *, char *, size_t, size_t);
void pool_done(struct pool *);

void *pool_get(struct pool *);
void pool_put(struct pool *, void *);

static inline int pool_isempty(struct pool *p)
{
	return p->first == NULL;
}

#endif /* MIDISH_POOL_H */
