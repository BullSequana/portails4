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

void bxipkt_options_set_default(struct bxipkt_options *opts)
{
	opts->debug = 0;
	opts->stats = 0;
}

/* Library initialization. */
int bxipkt_common_init(struct bxipkt_options *opts)
{
#ifdef DEBUG
	bxipkt_debug = opts->debug;
#endif

	bxipkt_stats = opts->stats;

	return PTL_OK;
}

void bxipkt_common_fini(void)
{
}
