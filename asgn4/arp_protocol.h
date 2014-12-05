#pragma once
#include <stdint.h>
//areq reply format: struct hwaddr

struct arp {
	uint16_t hatype;
	uint16_t ptype;
	uint8_t hlen;
	uint8_t plen;
	uint16_t oper;
	uint32_t id;
	uint8_t data[1024];
}__attribute__ ((__packed__));

#define ARP_MAGIC 0x501D
