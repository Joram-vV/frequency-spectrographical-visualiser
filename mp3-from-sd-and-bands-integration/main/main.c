#include "esp_log.h"
#include "esp_log_level.h"
#include "esp_peripherals.h"
#include "board.h"
#include "periph_sdcard.h"
#include "sdcard_scan.h"
#include "audio_event_iface.h"

#include "sdcard_list.h"
#include "filter_resample.h"

#include "console.h"
#include "visualizer.h"
#include "audio_control.h"
#include "espnow_protocol.h"
#include "espnow_transport.h"
// #include "VL6180X.h"

// VL6180X vl;

static const char *TAG = "[MAIN]";
static playlist_operator_handle_t sdcard_list_handle = NULL;

static void sdcard_url_save_callback(void *user_data, char *url) {
    playlist_operator_handle_t sdcard_handle = (playlist_operator_handle_t)user_data;
    sdcard_list_save(sdcard_handle, url);
}

// State tracker for Play/Pause toggle
static bool is_playing = false; 

static void espnow_command_task(void *pvParameters) {
    espnow_packet_t packet;
    ESP_LOGI(TAG, "ESP-NOW Command Task Started");

    while (1) {
        // Block indefinitely waiting for a packet from the Wi-Fi queue
        if (espnow_transport_receive(&packet, portMAX_DELAY)) {
            
            // We only care about Commands on this device
            if (packet.type == MSG_TYPE_COMMAND) {
                char *url = NULL;

                switch (packet.payload.command.id) {
                    case CMD_PLAY_INDEX:
                        ESP_LOGI(TAG, "Network Command: PLAY INDEX %ld", packet.payload.command.value);
                        // Using sdcard_list_choose from your sdcard_list.c
                        sdcard_list_choose(sdcard_list_handle, packet.payload.command.value, &url);
                        if (url) {
                            audio_control_play_track(url);
                            is_playing = true;
                        }
                        break;

                    case CMD_NEXT:
                        ESP_LOGI(TAG, "Network Command: NEXT");
                        sdcard_list_next(sdcard_list_handle, 1, &url);
                        if (url) {
                            audio_control_play_track(url);
                            is_playing = true;
                        }
                        break;

                    case CMD_PREVIOUS:
                        ESP_LOGI(TAG, "Network Command: PREVIOUS");
                        // --- FIX 1: Used sdcard_list_prev with a positive step ---
                        sdcard_list_prev(sdcard_list_handle, 1, &url); 
                        if (url) {
                            audio_control_play_track(url);
                            is_playing = true;
                        }
                        break;

                    case CMD_PLAY_PAUSE:
                        ESP_LOGI(TAG, "Network Command: PLAY/PAUSE");
                        if (is_playing) {
                            audio_control_pause();
                            is_playing = false;
                        } else {
                            audio_control_resume();
                            is_playing = true;
                        }
                        break;

                    case CMD_SET_VOLUME:
                        ESP_LOGI(TAG, "Network Command: VOLUME %ld", packet.payload.command.value);
                        // Assuming you have access to the board_handle globally, or you can pass it in.
                        // audio_hal_set_volume(board_handle->audio_hal, packet.payload.command.value);
                        break;

                    default:
                        ESP_LOGW(TAG, "Unhandled network command ID: %d", packet.payload.command.id);
                        break;
                }
            }
        }
    }
}

void broadcast_playlist(void) {
    if (sdcard_list_handle == NULL) {
        ESP_LOGW(TAG, "Cannot broadcast: SD card list not initialized");
        return;
    }

    int total_songs = sdcard_list_get_url_num(sdcard_list_handle);
    if (total_songs <= 0) {
        ESP_LOGI(TAG, "No songs to broadcast");
        return;
    }

    ESP_LOGI(TAG, "Broadcasting playlist (%d songs)...", total_songs);

    espnow_packet_t packet;
    packet.magic = ESPNOW_PROTOCOL_MAGIC;
    packet.type = MSG_TYPE_TELEMETRY;
    packet.payload.telemetry.id = TEL_PLAYLIST_CHUNK;
    
    int chunk_count = 0;
    int current_start_index = 0;

    for (int i = 0; i < total_songs; i++) {
        char *url = NULL;
        if (sdcard_list_choose(sdcard_list_handle, i, &url) == ESP_OK && url != NULL) {
            
            // Extract just the filename from the URL (e.g., "file://sdcard/song.mp3" -> "song.mp3")
            char *filename = strrchr(url, '/');
            filename = filename ? filename + 1 : url;

            // On the first item of a new chunk, initialize the packet header
            if (chunk_count == 0) {
                packet.payload.telemetry.data.playlist.start_index = current_start_index;
                packet.payload.telemetry.data.playlist.total_songs = total_songs;
                // Clear the array to prevent garbage data
                memset(packet.payload.telemetry.data.playlist.songs, 0, sizeof(packet.payload.telemetry.data.playlist.songs));
            }

            // Copy the filename into the packet array
            strncpy(packet.payload.telemetry.data.playlist.songs[chunk_count], filename, MAX_SONG_NAME_LEN - 1);
            packet.payload.telemetry.data.playlist.songs[chunk_count][MAX_SONG_NAME_LEN - 1] = '\0'; // Ensure null-termination
            
            chunk_count++;

            // If the chunk is full, OR this is the very last song, send the packet!
            if (chunk_count == MAX_SONGS_PER_PACKET || i == total_songs - 1) {
                packet.payload.telemetry.data.playlist.count = chunk_count;
                
                espnow_transport_send(&packet);
                
                // CRITICAL: Give the Wi-Fi hardware and FreeRTOS queues a tiny 
                // moment to process the send before blasting the next chunk.
                vTaskDelay(pdMS_TO_TICKS(20)); 
                
                current_start_index += chunk_count;
                chunk_count = 0;
            }
        }
    }
    ESP_LOGI(TAG, "Finished broadcasting playlist.");
}

void app_main(void) {
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);
    esp_log_level_set("ADC_BTN", ESP_LOG_ERROR);
    esp_log_level_set("AUDIO_EVT", ESP_LOG_ERROR);
    esp_log_level_set("PLAYLIST_SDCARD", ESP_LOG_INFO);

    ESP_LOGI(TAG, "Initializing System...");

    // peripherals & SD card
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
    audio_board_key_init(set);
    audio_board_sdcard_init(set, SD_MODE_SPI);

    sdcard_list_create(&sdcard_list_handle);
    sdcard_scan(sdcard_url_save_callback, "/sdcard", 0, (const char *[]) {"mp3"}, 1, sdcard_list_handle);

    // 2. Audio Hardware
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);

    // 3. Modules Initialization
    visualizer_init();
    audio_control_init();
    console_init(board_handle, sdcard_list_handle);

    ESP_LOGI(TAG, "Initializing ESP-NOW Transport...");
    espnow_transport_init();
    
    // Pass board_handle if you need it for volume control, otherwise NULL is fine
    xTaskCreate(espnow_command_task, "espnow_cmd_task", 4096, NULL, 5, NULL);

    // --- NEW: Push the playlist to the GUI ---
    // At this point, the SD card is scanned and the network is up.
    // We delay briefly just in case the GUI node is still booting up its Wi-Fi.
    vTaskDelay(pdMS_TO_TICKS(1000)); 
    broadcast_playlist();
    // -----

    xTaskCreate(visualizer_task, "visualizer_task", 4096, NULL, 5, NULL);

    // 4. Event Listener setup
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(audio_control_get_pipeline(), evt);

    ESP_LOGI(TAG, "System Ready. Waiting for commands.");

    xTaskCreate(console_task, "console_task", 4096, NULL, 4, NULL);

    // 5. Main Event Loop
    while (1) {
        audio_event_iface_msg_t msg;
        if (audio_event_iface_listen(evt, &msg, portMAX_DELAY) != ESP_OK) continue;

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT) {
            if (msg.source == (void *) audio_control_get_mp3_decoder() && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
                audio_element_info_t music_info = {0};
                audio_element_getinfo(audio_control_get_mp3_decoder(), &music_info);
                audio_element_setinfo(audio_control_get_i2s_writer(), &music_info);
                rsp_filter_set_src_info(audio_control_get_rsp_handle(), music_info.sample_rates, music_info.channels);
                continue;
            }

            if (msg.source == (void *) audio_control_get_i2s_writer() && msg.cmd == AEL_MSG_CMD_REPORT_STATUS) {
                if (audio_element_get_state(audio_control_get_i2s_writer()) == AEL_STATE_FINISHED) {
                    printf("\n[ INFO ] Track Finished. Waiting for command...\n");
                    audio_control_stop();
                    // --- FIX 2: Tell the telemetry task to stop broadcasting progress ---
                    is_playing = false;
                }
            }
        }
    }
}