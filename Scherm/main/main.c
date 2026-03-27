#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "espnow_protocol.h"
#include "espnow_transport.h"
#include "lvgl_use.h"
#include "playback_controls_ui.h"

static const char *TAG = "main";

static void espnow_telemetry_task(void *pvParameters) {
    espnow_packet_t packet;
    ESP_LOGI(TAG, "ESP-NOW Telemetry Task Started");

    while (1) {
        if (espnow_transport_receive(&packet, portMAX_DELAY)) {
            
            if (packet.type != MSG_TYPE_TELEMETRY) continue;
            // If it's a chunk of the playlist
            if (packet.payload.telemetry.id != TEL_PLAYLIST_CHUNK) continue;

            tel_playlist_t *pl = &packet.payload.telemetry.data.playlist;
            ESP_LOGI(TAG, "Received Playlist Chunk: %ld to %ld (Total: %ld)", 
                        pl->start_index, pl->start_index + pl->count - 1, pl->total_songs);
            
            // Pass it to the UI thread safely
            playback_controls_ui_add_playlist_chunk(pl);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing ESP-NOW Transport...");
    espnow_transport_init();

    // --- NEW: Start the Telemetry Listener ---
    xTaskCreate(espnow_telemetry_task, "espnow_rx_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Starting display UI");
    app_ui_start();
}
