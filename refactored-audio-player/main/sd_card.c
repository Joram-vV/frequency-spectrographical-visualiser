#include "sd_card.h"

#include "audio_element.h"
#include "esp_err.h"

#include "esp_log.h"

#include "board_def.h"

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"

#include "priorities.h"

#include "audio_control.h"


// SPI bus settings
static const spi_bus_config_t bus_cfg = {
	.mosi_io_num = ESP_SPI_MOSI,
	.miso_io_num = ESP_SPI_MISO,
	.sclk_io_num = ESP_SPI_CLK,
	.quadwp_io_num = -1,
	.quadhd_io_num = -1,
	.max_transfer_sz = 4000,
};

// SD SPI settings
static sdmmc_host_t host = SDSPI_HOST_DEFAULT();
static sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();

// Mount settings
static const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
	.format_if_mount_failed = false,
	.max_files = 5,
	.allocation_unit_size = 16 * 1024,
};

// SD card
static sdmmc_card_t* card = NULL;

void sd_card_manager(void* pv) {
	while (true) {
		char buffer[512];
		bool mounted = esp_vfs_fat_info(SD_CARD_MOUNT_POINT, NULL, NULL) == ESP_OK;
		if (!mounted) {
			// Attempt to mount
			esp_err_t ret = esp_vfs_fat_sdspi_mount(SD_CARD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);

			if (ret == ESP_OK) {
				ESP_LOGI(SD_CARD_TAG, "SD card mounted successfully.");
			}
			else {
				ESP_LOGV(SD_CARD_TAG, "Waiting for SD card... (0x%x)", ret);
				if (audio_element_get_state(audio_control_get_mp3_decoder()) != AEL_STATE_STOPPED) {
					audio_control_stop();
				}
			}
		}
		else {
			esp_err_t err = sdmmc_read_sectors( card, buffer, 0, 1);
			if (err != ESP_OK) {
				ESP_LOGW(SD_CARD_TAG, "SD card removed or error detected. Unmounting...");

				if (audio_element_get_state(audio_control_get_mp3_decoder()) != AEL_STATE_STOPPED) {
					audio_control_stop();
				}

				// Use the official unmount which cleans up the VFS and the card object
				esp_vfs_fat_sdcard_unmount(SD_CARD_MOUNT_POINT, card);
				card = NULL;
			}
		}

		// Poll every 1 second.
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}
TaskHandle_t sd_card_manager_handle;

void init_sd_card_reader(void) {
	// Init SPI
	ESP_LOGI(SD_CARD_TAG, "Initialising SPI host 2 if not initialised.");
	static bool spi_initialized = false;
	if (!spi_initialized) {
		ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SDSPI_DEFAULT_DMA));
		spi_initialized = true;
	}

	// Setting settings
	ESP_LOGI(SD_CARD_TAG, "Finalising SD-card settings.");
	host.slot = SPI2_HOST;
	slot_config.gpio_cs = ESP_SPI_SD_CS;
	slot_config.host_id = host.slot;

	// Creating SD-card management task
	ESP_LOGI(SD_CARD_TAG, "Creating and starting SD-card management task.");
	xTaskCreate(
		sd_card_manager,
		"sd_card_manager",
		4096,
		NULL,
		SD_CARD_MANAGER_PRIORITY,
		&sd_card_manager_handle
	);
}

bool is_sd_card_mounted() {
	return esp_vfs_fat_info(SD_CARD_MOUNT_POINT, NULL, NULL) == ESP_OK;
}
