#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "espnow_protocol.h"

// Initialize Wi-Fi and ESP-NOW, and create the receive queue
void espnow_transport_init(void);

// Send a packet over the air
void espnow_transport_send(espnow_packet_t *packet);

// Check if a packet has arrived in the queue. 
// Returns true if a packet was received, false if it timed out.
bool espnow_transport_receive(espnow_packet_t *packet, TickType_t ticks_to_wait);