#include <stdint.h>
#include "utils.h"

struct odr_hdr {
	uint32_t bid;
	uint32_t ipaddr;
	uint16_t flags;
	uint16_t payload_len;
};

CASSERT(sizeof(struct odr_hdr) != 12, odr_hdr_size);

#define ODR_FLAG(n)	(1<<(n))
#define ODR_RREQ	ODR_FLAG(0)
#define ODR_RREP	ODR_FLAG(1)
#define ODR_RREP_SENT	ODR_FLAG(2)
#define ODR_HAS_DATA	ODR_FLAG(3)
