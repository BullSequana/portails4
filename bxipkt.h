#ifndef BXIPKT_H
#define BXIPKT_H

#include <stdint.h>
#include <poll.h>

#include "include/swptl4.h"

struct bxipkt_iface;

int bxipkt_common_init(struct bxipkt_options *opts, struct bxipkt_ctx *ctx);
void bxipkt_common_fini(struct bxipkt_ctx *ctx);

extern int bxipkt_debug;

#endif
