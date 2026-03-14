// nat_router.h
#pragma once

#include <stddef.h>

void nat_router_init(void);
void nat_router_handle_packet(void *packet, size_t len);
