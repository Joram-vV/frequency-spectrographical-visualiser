#include <stdio.h>
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "espnow_protocol.h"
#include "espnow_transport.h"
#include "lvgl_use.h"
#include "playback_controls_ui.h"
#include "now_playing_ui.h"
#include "settings.h"


static const char *TAG = "main";

static void espnow_telemetry_task(void *pvParameters) {
    espnow_packet_t packet;
    ESP_LOGI(TAG, "ESP-NOW Telemetry Task Started");

    while (1) {
        if (espnow_transport_receive(&packet, portMAX_DELAY)) {
            
            if (packet.type != MSG_TYPE_TELEMETRY) continue;
            switch (packet.payload.telemetry.id) {
                case TEL_PLAYBACK_STATUS:
                    {
                        tel_status_t* status = &packet.payload.telemetry.data.status;
                        ESP_LOGI(TAG, "State: %ld, current song: %ld, elapsed seconds: %ld, duration seconds: %ld", status->state, status->current_song_index, status->elapsed_seconds, status->duration_seconds);
                    
                        // Pass it to the UI thread safely
                        now_playing_ui_update_status(status);
                        break;
                    }
                case TEL_PLAYLIST_CHUNK:
                    {
                        tel_playlist_t *pl = &packet.payload.telemetry.data.playlist;
                        ESP_LOGI(TAG, "Received Playlist Chunk: %ld to %ld (Total: %ld)", pl->start_index, pl->start_index + pl->count - 1, pl->total_songs);
                        
                        // Pass it to the UI thread safely
                        playback_controls_ui_add_playlist_chunk(pl);
                        break;
                    }
                default:
            }
        }
    }
}




void app_main(void)
{
    ESP_LOGI(TAG, "Starting");
     gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << WAKEUP_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    ESP_ERROR_CHECK(gpio_wakeup_enable(WAKEUP_BUTTON_GPIO, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
    
    ESP_LOGI(TAG, "Initializing ESP-NOW Transport...");

    espnow_transport_init();

    // Telemetry updates are received on a dedicated task and forwarded to the UI thread.
    xTaskCreate(espnow_telemetry_task, "espnow_rx_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Starting display UI");
    app_ui_start();
     
}
