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
#ifndef BXIMSG_WTHR_H
#define BXIMSG_WTHR_H

#define BXIMSG_DEFAULT_WTHREADS	3
#define BXIMSG_MAX_WTHREADS	7

/* Maximum number of memcpy() requests per worker thread */
#define BXIMSG_NUM_WI		32

/* Minimal message size to activate the copy work requests framework. */
#define BXIMSG_ASYNC_MEMCPY_MIN_MSG_SIZE      (256*1024)

/* Minimal buffer size to activate the copy work requests framework. */
#define BXIMSG_ASYNC_MEMCPY_MIN_BUF_SIZE 4096

/* Maximum number of cpu for the cpu_set_t structure */
#define BXIMSG_MAX_CPU_NUM 300

/* Size of the cpu_set_t structure */
#define CPU_SET_SIZE (CPU_ALLOC_SIZE(BXIMSG_MAX_CPU_NUM))

/* cpuset wrapper macros */
#define CPUSET_COUNT(cpus)	(CPU_COUNT_S((CPU_SET_SIZE), (cpus)))
#define CPUSET_ISSET(c, cpus)	(CPU_ISSET_S((c), (CPU_SET_SIZE), (cpus)))
#define CPUSET_ZERO(cpus)	CPU_ZERO_S((CPU_SET_SIZE), (cpus))
#define CPUSET_SET(c, cpus)	CPU_SET_S((c), (CPU_SET_SIZE), (cpus))

extern unsigned int bximsg_async_memcpy_min_msg_size;

/* initialize and finalize worker threads and work items */
int bximsg_init_wthreads(void);
void bximsg_fini_wthreads(void);
void bximsg_async_memcpy(void *dest, const void *src, size_t len,
			 unsigned int pkt_index,
			 volatile uint64_t *pending_memcpy);
#endif
