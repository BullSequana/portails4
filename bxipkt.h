#ifndef BXIPKT_H
#define BXIPKT_H

#include <stdint.h>
#include <poll.h>

#include "include/swptl4.h"

struct bxipkt_iface;

int bxipkt_common_init(void);
void bxipkt_common_fini(void);

extern int bxipkt_debug;
extern unsigned int bxipkt_stats;

#endif
