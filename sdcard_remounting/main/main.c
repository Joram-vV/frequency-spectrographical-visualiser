#include "esp_err.h"
#include "esp_log.h"
#include "esp_log_level.h"

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"

#include "playlist.h"
#include "sd_card.h"
#include "sdcard_list.h"
#include "sdcard_scan.h"

#define MAIN_TAG "[MAIN]"

static void sdcard_url_save_callback(void *user_data, char *url) {
	playlist_operator_handle_t sdcard_handle = (playlist_operator_handle_t)user_data;
	sdcard_list_save(sdcard_handle, url);
}

static void list_songs(playlist_operator_handle_t playlist) {
	int total_songs = sdcard_list_get_url_num(playlist);
	if (total_songs <= 0) ESP_LOGI(MAIN_TAG, "No songs.");
	else {
		ESP_LOGI(MAIN_TAG, "Songs: ");
		for (int i = 0; i < total_songs; i++) {
			char *url = NULL;
			if (sdcard_list_choose(playlist, i, &url) == ESP_OK && url != NULL) {
				char *filename = strrchr(url, '/');
				filename = filename ? filename + 1 : url;

				ESP_LOGI(MAIN_TAG, "Found %s at spot %d in the playlist.");
			}
		}
	}
}

void app_main(void) {
	esp_log_level_set("*", ESP_LOG_WARN);
	esp_log_level_set(MAIN_TAG, ESP_LOG_INFO);
	esp_log_level_set(SD_CARD_TAG, ESP_LOG_INFO);
	esp_log_level_set("PLAYLIST_SDCARD", ESP_LOG_INFO);

	ESP_LOGI(MAIN_TAG, "Initialising system...");

	ESP_LOGI(MAIN_TAG, "Initialising SD-card reader.");
	init_sd_card_reader();

	ESP_LOGI(MAIN_TAG, "Creating playlist");
	playlist_operator_handle_t playlist = NULL;

	while (true) {
		if (!is_sd_card_mounted()) {
			ESP_LOGW(MAIN_TAG, "Waiting for SD card to be mounted...");
			if (playlist != NULL) {
				// If the card was pulled, we should probably destroy the old handle
				// sdcard_list_destroy(playlist);
				playlist = NULL;
			}
			vTaskDelay(pdMS_TO_TICKS(2000));
			continue;
		}

		if (playlist == NULL) {
			ESP_LOGI(MAIN_TAG, "SD mounted. Creating playlist handle...");
			if (sdcard_list_create(&playlist) != ESP_OK) {
				ESP_LOGE(MAIN_TAG, "Failed to create playlist. Retrying...");
				vTaskDelay(pdMS_TO_TICKS(2000));
				continue;
			}
		}

		if (sdcard_list_get_url_num(playlist) <= 0) {
			ESP_LOGI(MAIN_TAG, "Scanning SD-card.");
			ESP_ERROR_CHECK(sdcard_scan(sdcard_url_save_callback, SD_CARD_MOUNT_POINT, 0, (const char *[]) {"mp3"}, 1, playlist));
		}
		list_songs(playlist);

		vTaskDelay(pdMS_TO_TICKS(5000));
	}
}
