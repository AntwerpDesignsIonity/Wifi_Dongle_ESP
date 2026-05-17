// nat_router.c
// ESP32 NAT and routing logic for USB <-> WiFi dongle
#include "nat_router.h"
#include "esp_log.h"

static const char *TAG = "NAT_ROUTER";

void nat_router_init(void) {
    ESP_LOGI(TAG, "NAT router initialized (stub)");
    // TODO: Set up NAT between WiFi and USB interfaces
}

void nat_router_handle_packet(void *packet, size_t len) {
    // TODO: Implement packet forwarding and NAT
    (void)packet;
    (void)len;
}
