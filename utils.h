/*	$OpenBSD$	*/
/*
 * Copyright (c) 2003-2012 Alexandre Ratchov <alex@caoua.org>
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

#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <pthread.h>

#include "ptl_log.h"

#define count_of(array) (sizeof(array) / sizeof(array[0]))
#define align_to(x, page_size) (((size_t)(x) + page_size - 1) & ~(page_size - 1))

#ifdef __cplusplus
extern "C" {
#endif

void ptl_panic(const char *fmt, ...) __attribute__((noreturn));

void *xmalloc(size_t, char *);
char *xstrdup(char *, char *);
void xfree(void *);

static inline void ptl_mutex_lock(pthread_mutex_t *mutex, const char *function_name)
{
	int err;

	err = pthread_mutex_lock(mutex);
	if (err)
		ptl_panic("%s: pthread_mutex_lock: %s\n", function_name, strerror(err));
}

static inline void ptl_mutex_unlock(pthread_mutex_t *mutex, const char *function_name)
{
	int err;

	err = pthread_mutex_unlock(mutex);
	if (err)
		ptl_panic("%s: pthread_mutex_unlock: %s\n", function_name, strerror(err));
}

#ifdef __cplusplus
}
#endif

#endif
