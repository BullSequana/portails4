#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <pthread.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>

#include "portals4.h"
#include "portals4_bxiext.h"
#include "ptl_log.h"
#include "utils.h"

static pthread_mutex_t log_fd_mutex = PTHREAD_MUTEX_INITIALIZER;
static FILE *log_fd;

static int ptl_log_default(const char *fmt, ...) __attribute__ ((format(printf, 1, 2)));
static int ptl_log_null(const char *fmt, ...);

#ifdef DEBUG
int (*ptl_log)(const char *fmt, ...) = ptl_log_default;
#else
int (*ptl_log)(const char *fmt, ...) = ptl_log_null;
#endif

void ptl_set_log_fn(int (*log_fn)(const char *fmt, ...));

void ptl_set_log_fn(int (*log_fn)(const char *fmt, ...))
{
	if (log_fn != NULL)
		ptl_log = log_fn;
	else
		ptl_log = ptl_log_null;
}

void ptl_log_init()
{
	const char *env;
	char log_path[PATH_MAX];

	if (ptl_log != ptl_log_default)
		return;

	env = getenv("PORTALS4_DEBUG_PATH");
	if (env) {
		snprintf(log_path, sizeof(log_path), "%s.%d", env, getpid());
		log_fd = fopen(log_path, "w");
	}

	if (log_fd == NULL)
		log_fd = stderr;
}

void ptl_log_close()
{
	if (log_fd == NULL)
		return;

	fflush(log_fd);

	if (log_fd != stderr)
		fclose(log_fd);

	log_fd = NULL;
}

void ptl_log_flush()
{
	if (log_fd)
		fflush(log_fd);
}

static int ptl_log_default(const char *fmt, ...)
{
	struct timespec ts;
	char buf[PTL_LOG_BUF_SIZE + 64];
	int buf_len;
	va_list ap;
	int err;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	buf_len = snprintf(buf, sizeof(buf), "%d|%ld|%ld.%09ld: ", getpid(), syscall(__NR_gettid),
			   ts.tv_sec, ts.tv_nsec);

	va_start(ap, fmt);
	buf_len = vsnprintf(buf + buf_len, sizeof(buf) - buf_len, fmt, ap);
	va_end(ap);

	err = pthread_mutex_lock(&log_fd_mutex);
	if (err) {
		fprintf(stderr, "pthread_mutex_lock: %s\n", strerror(err));
		fflush(stderr);
		abort();
	}

	fputs(buf, log_fd ? log_fd : stderr);

	err = pthread_mutex_unlock(&log_fd_mutex);
	if (err) {
		fprintf(stderr, "pthread_mutex_unlock: %s\n", strerror(err));
		fflush(stderr);
		abort();
	}

	return buf_len;
}

static int ptl_log_null(const char *fmt, ...)
{
	return 0;
}
