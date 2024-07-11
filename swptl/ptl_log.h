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

#ifndef PTL_LOG_H
#define PTL_LOG_H

/* Size of a buffer used to log a message */
#define PTL_LOG_BUF_SIZE 256

void ptl_log_init(void);
void ptl_log_close(void);
void ptl_log_flush(void);

extern int (*ptl_log)(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* PTL_LOG_H */
