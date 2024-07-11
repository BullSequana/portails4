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
#ifndef BXIMSG_WTHR_H
#define BXIMSG_WTHR_H

#define BXIMSG_DEFAULT_WTHREADS 3
#define BXIMSG_MAX_WTHREADS 7

/* Maximum number of memcpy() requests per worker thread */
#define BXIMSG_NUM_WI 32

/* Minimal message size to activate the copy work requests framework. */
#define BXIMSG_ASYNC_MEMCPY_MIN_MSG_SIZE (256 * 1024)

/* Minimal buffer size to activate the copy work requests framework. */
#define BXIMSG_ASYNC_MEMCPY_MIN_BUF_SIZE 4096

/* Maximum number of cpu for the cpu_set_t structure */
#define BXIMSG_MAX_CPU_NUM 300

/* Size of the cpu_set_t structure */
#define CPU_SET_SIZE (CPU_ALLOC_SIZE(BXIMSG_MAX_CPU_NUM))

/* cpuset wrapper macros */
#define CPUSET_COUNT(cpus) (CPU_COUNT_S((CPU_SET_SIZE), (cpus)))
#define CPUSET_ISSET(c, cpus) (CPU_ISSET_S((c), (CPU_SET_SIZE), (cpus)))
#define CPUSET_ZERO(cpus) CPU_ZERO_S((CPU_SET_SIZE), (cpus))
#define CPUSET_SET(c, cpus) CPU_SET_S((c), (CPU_SET_SIZE), (cpus))

extern unsigned int bximsg_async_memcpy_min_msg_size;

/* initialize and finalize worker threads and work items */
int bximsg_init_wthreads(void);
void bximsg_fini_wthreads(void);
void bximsg_async_memcpy(void *dest, const void *src, size_t len, unsigned int pkt_index,
			 volatile uint64_t *pending_memcpy);
#endif
