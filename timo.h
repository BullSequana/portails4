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

#ifndef MIDISH_TIMO_H
#define MIDISH_TIMO_H

#include <pthread.h>

struct timo_ctx {
	pthread_mutex_t queue_mutex;
	struct timo *queue;
	unsigned int debug;
};

struct timo {
	struct timo *next;
	unsigned long long expire; /* expire date */
	unsigned set; /* true if the timeout is set */
	void (*cb)(void *arg); /* routine to call on expiration */
	void *arg; /* argument to give to 'cb' */

	struct timo_ctx *ctx;
};

void timo_set(struct timo_ctx *ctx, struct timo *, void (*)(void *), void *);
void timo_add(struct timo *, unsigned);
void timo_del(struct timo *);
void timo_update(struct timo_ctx *ctx);
void timo_init(struct timo_ctx *ctx);
void timo_done(struct timo_ctx *ctx);

#endif /* MIDISH_TIMO_H */
