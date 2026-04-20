#include "esp_log.h"
#include "esp_log_level.h"
#include "esp_peripherals.h"
#include "board.h"
#include "audio_element.h"

#include "freertos/idf_additions.h"
#include "sdcard_scan.h"
#include "audio_event_iface.h"

#include "sd_card.h"
#include "sdcard_list.h"
#include "filter_resample.h"
#include <stdbool.h>

#include "console.h"
#include "visualizer.h"
#include "audio_control.h"
#include "espnow_protocol.h"
#include "espnow_transport.h"
#include "priorities.h"

#include "fan_control.h"

#include "shared_state.h"

#include "esp_task_wdt.h"

// Actual instantiation of the shared variables
float shared_target_heights[NUM_BANDS] = {0.0f};
SemaphoreHandle_t shared_state_mutex = NULL;

static const char *TAG = "[MAIN]";
static playlist_operator_handle_t sdcard_list_handle = NULL;

static void sdcard_url_save_callback(void *user_data, char *url) {
	playlist_operator_handle_t sdcard_handle = (playlist_operator_handle_t)user_data;
	sdcard_list_save(sdcard_handle, url);
}

// State tracker for Play/Pause toggle
static bool is_playing = false;

static void broadcast_playlist(void) {
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
			} else if (packet.type == MSG_TYPE_REQUEST) {
				broadcast_playlist();
			}
		}
	}
}

static void espnow_telemetry_task(void *pvParameters) {
	espnow_packet_t packet;
	packet.magic = ESPNOW_PROTOCOL_MAGIC;
	packet.type = MSG_TYPE_TELEMETRY;
	packet.payload.telemetry.id = TEL_PLAYBACK_STATUS;

	while (1) {
		vTaskDelay(pdMS_TO_TICKS(500));

		// 1. Get info from the I2S WRITER for PLAYBACK PROGRESS
		audio_element_info_t i2s_info = {0};
		audio_element_handle_t i2s_writer = audio_control_get_i2s_writer();
		audio_element_getinfo(i2s_writer, &i2s_info);

		// 2. State Determination
		static audio_element_state_t prev_state = AEL_STATE_NONE;
		audio_element_state_t el_state = audio_element_get_state(i2s_writer);
		if (prev_state != AEL_STATE_RUNNING && el_state != AEL_STATE_RUNNING) {
			prev_state = el_state;
			continue;
		}
		prev_state = el_state;

		if (el_state == AEL_STATE_RUNNING) packet.payload.telemetry.data.status.state = TEL_STATE_PLAYING;
		else if (el_state == AEL_STATE_PAUSED) packet.payload.telemetry.data.status.state = TEL_STATE_PAUSED;
		else packet.payload.telemetry.data.status.state = TEL_STATE_STOPPED;

		// 3. Calculate Elapsed Seconds (Using I2S PCM data)
		if (i2s_info.sample_rates > 0 && i2s_info.channels > 0 && i2s_info.bits > 0) {
			uint32_t pcm_bps = i2s_info.sample_rates * i2s_info.channels * i2s_info.bits / 8;
			packet.payload.telemetry.data.status.elapsed_seconds = (int32_t)(i2s_info.byte_pos / pcm_bps);
		} else {
			packet.payload.telemetry.data.status.elapsed_seconds = 0;
		}

		// 5. Set song index
		packet.payload.telemetry.data.status.current_song_index = (int32_t) sdcard_list_get_url_id(sdcard_list_handle);

		// 6. Calculate Total Duration (Using MP3 Bitrate and File Size)
		packet.payload.telemetry.data.status.duration_seconds = get_song_duration();

		ESP_LOGI(TAG, "\nData: { state = %d, song = %d, elapsed = %d, duration = %d }\n\n",
				packet.payload.telemetry.data.status.state,
		packet.payload.telemetry.data.status.current_song_index,
		packet.payload.telemetry.data.status.elapsed_seconds,
		packet.payload.telemetry.data.status.duration_seconds);

		espnow_transport_send(&packet);
	}
}

void app_main(void) {
	esp_log_level_set("*", ESP_LOG_WARN);
	esp_log_level_set(TAG, ESP_LOG_INFO);
	esp_log_level_set("ADC_BTN", ESP_LOG_ERROR);
	esp_log_level_set("AUDIO_EVT", ESP_LOG_ERROR);
	esp_log_level_set(SD_CARD_TAG, ESP_LOG_INFO);
	esp_log_level_set("PLAYLIST_SDCARD", ESP_LOG_INFO);

	ESP_LOGI(TAG, "Initializing System...");

	ESP_LOGI(TAG, "Setting up watchdog.");
	if (esp_task_wdt_deinit() != ESP_OK) return;
	esp_task_wdt_config_t wdt_config = {
		.timeout_ms = 5000,
		.idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
		.trigger_panic = true
	};
	ESP_ERROR_CHECK(esp_task_wdt_init(&wdt_config));
	ESP_LOGI(TAG, "Watchdog ready.");

	// Initialize the shared state mutex
	shared_state_mutex = xSemaphoreCreateMutex();
	if (shared_state_mutex == NULL) {
		printf("[ERROR] Failed to create shared state mutex!\n");
		return; // Halt if we can't create the mutex
	}

	ESP_LOGI(TAG, "Initialising SD-card reader.");
	init_sd_card_reader();

	// 2. Audio Hardware
	audio_board_handle_t board_handle = audio_board_init();
	audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);

	// 3. Modules Initialization
	visualizer_init();
	audio_control_init();
	console_init(board_handle, sdcard_list_handle);


	ESP_LOGI(TAG, "Setting up fans.");
	fan_control_init();

	// Start the real-time fan control loop!
	xTaskCreatePinnedToCore(fan_control_task, "fan_control_task", 4096, NULL, FAN_CONTROLLER_PRIORITY, NULL, 1);

	ESP_LOGI(TAG, "Initializing ESP-NOW Transport...");
	espnow_transport_init();

	// Pass board_handle if you need it for volume control, otherwise NULL is fine
	xTaskCreate(espnow_command_task, "espnow_cmd_task", 8192, NULL, ESPNOW_COMMAND_HANDLER_PRIORITY, NULL);

	xTaskCreate(visualizer_task, "visualizer_task", 4096, NULL, VISUALISER_PRIORITY, NULL);

	// 4. Event Listener setup
	audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
	audio_pipeline_set_listener(audio_control_get_pipeline(), evt);

	ESP_LOGI(TAG, "System Ready. Waiting for commands.");

	xTaskCreate(console_task, "console_task", 4096, NULL, CONSOLE_PRIORITY, NULL);

	// Inside app_main, near your other task creations:
	xTaskCreate(espnow_telemetry_task, "telemetry_task", 4096, NULL, TELEMETRY_TASK_PRIORITY, NULL);

	// // broadcast playlist to online devices
	// vTaskDelay(500);
	// broadcast_playlist();

	// 5. Main Event Loop
	while (1) {
		if (!is_sd_card_mounted()) {
			ESP_LOGW(TAG, "Waiting for SD card to be mounted...");
			if (sdcard_list_handle != NULL) {
				// If the card was pulled, we should probably destroy the old handle
				// sdcard_list_destroy(playlist);
				sdcard_list_handle = NULL;
			}
			vTaskDelay(pdMS_TO_TICKS(100));
			continue;
		}

		if (sdcard_list_handle == NULL) {
			ESP_LOGI(TAG, "SD mounted. Creating playlist handle...");
			if (sdcard_list_create(&sdcard_list_handle) != ESP_OK) {
				ESP_LOGE(TAG, "Failed to create playlist. Retrying...");
				vTaskDelay(pdMS_TO_TICKS(100));
				continue;
			}
		}

		if (sdcard_list_get_url_num(sdcard_list_handle) <= 0) {
			ESP_LOGI(TAG, "Scanning SD-card.");
			ESP_ERROR_CHECK(sdcard_scan(sdcard_url_save_callback, SD_CARD_MOUNT_POINT, 0, (const char *[]) {"mp3"}, 1, sdcard_list_handle));
			broadcast_playlist();
		}

		audio_event_iface_msg_t msg;
		if (audio_event_iface_listen(evt, &msg, pdMS_TO_TICKS(200)) != ESP_OK) continue;

		if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT) {
			if (msg.source == (void *) audio_control_get_mp3_decoder() && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
				audio_element_info_t music_info = {0};
				audio_element_getinfo(audio_control_get_mp3_decoder(), &music_info);
				audio_element_setinfo(audio_control_get_i2s_writer(), &music_info);
				rsp_filter_set_src_info(audio_control_get_rsp_handle(), music_info.sample_rates, music_info.channels);
				ESP_LOGI(TAG, "[ * ] Receive music info from music decoder, sample_rates=%d, bits=%d, ch=%d", music_info.sample_rates, music_info.bits, music_info.channels);
				ESP_LOGI(TAG, "[ * ] Receive music info from music decoder, byte_pos=%d, total_bytes=%d, bps=%d, duration=%d", music_info.byte_pos, music_info.total_bytes, music_info.bps, music_info.duration);
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