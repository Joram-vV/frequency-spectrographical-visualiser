#include "espnow_transport.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "ESPNOW_TRANS";
static QueueHandle_t s_recv_queue = NULL;

// We use the broadcast MAC address so the two devices can find each other 
// without needing hardcoded MAC addresses.
static const uint8_t s_broadcast_mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// ESP-NOW Receive Callback (Runs in the Wi-Fi Task)
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    if (len != sizeof(espnow_packet_t)) {
        // Ignore packets that don't match our protocol size
        return; 
    }

    espnow_packet_t *packet = (espnow_packet_t *)data;

    // Verify the magic number to ensure this isn't random ESP-NOW traffic from another device
    if (packet->magic != ESPNOW_PROTOCOL_MAGIC) {
        return;
    }

    // Safely push the packet into the queue for the main application to process
    if (s_recv_queue != NULL) {
        // Note: Using an ISR-safe-like timeout of 0 because we shouldn't block the Wi-Fi task
        xQueueSend(s_recv_queue, packet, 0); 
    }
}

void espnow_transport_init(void)
{
    // 1. Create the receive queue (holds up to 10 incoming packets)
    s_recv_queue = xQueueCreate(10, sizeof(espnow_packet_t));
    if (s_recv_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create receive queue");
        return;
    }

    // 2. Initialize Wi-Fi in Station Mode (Required for ESP-NOW)
    ESP_ERROR_CHECK(esp_netif_init());
    // Note: esp_event_loop_create_default() might already be called in your main.c. 
    // If it fails with "invalid state", just comment the line below out.
    esp_event_loop_create_default(); 
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 3. Initialize ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    // 4. Add the broadcast peer so we can send data
    esp_now_peer_info_t peer_info = {0};
    peer_info.channel = 0; // Use current channel
    peer_info.encrypt = false;
    peer_info.ifidx = WIFI_IF_STA;
    memcpy(peer_info.peer_addr, s_broadcast_mac, ESP_NOW_ETH_ALEN);
    
    if (esp_now_add_peer(&peer_info) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add broadcast peer");
    } else {
        ESP_LOGI(TAG, "ESP-NOW Transport Initialized");
    }
}

void espnow_transport_send(espnow_packet_t *packet)
{
    // Always enforce the magic number before sending
    packet->magic = ESPNOW_PROTOCOL_MAGIC;
    
    // Send to the broadcast MAC address
    esp_err_t err = esp_now_send(s_broadcast_mac, (const uint8_t *)packet, sizeof(espnow_packet_t));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Send error: %s", esp_err_to_name(err));
    }
}

bool espnow_transport_receive(espnow_packet_t *packet, TickType_t ticks_to_wait)
{
    if (s_recv_queue == NULL) {
        return false;
    }
    // Pull from the FreeRTOS queue
    return (xQueueReceive(s_recv_queue, packet, ticks_to_wait) == pdTRUE);
}