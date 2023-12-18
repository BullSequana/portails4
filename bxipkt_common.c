#include <stdio.h>

#include "portals4.h"
#include "bxipkt.h"
#include "ptl_log.h"

#ifdef DEBUG
/*
 * log to stderr, with the give debug level
 */
#define LOGN(n, ...)                                                                               \
	do {                                                                                       \
		if (bxipkt_debug >= (n))                                                           \
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

#include "ptl_getenv.h"

#ifdef DEBUG
int bxipkt_debug = 0;
#endif
unsigned int bxipkt_stats;

/* Library initialization. */
int bxipkt_common_init(void)
{
	const char *env;

#ifdef DEBUG
	env = getenv("BXIPKT_DEBUG");
	if (env)
		sscanf(env, "%u", &bxipkt_debug);
#endif

	env = ptl_getenv("BXIPKT_STATISTICS");
	if (env)
		sscanf(env, "%u", &bxipkt_stats);

	return PTL_OK;
}

void bxipkt_common_fini(void)
{
}
