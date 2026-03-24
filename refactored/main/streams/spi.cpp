#include "spi.h"

#include "board_def.h"
#include "esp_err.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"

static const char *TAG = "SPI2";

void init_nx_spi() {
    // SPI bus config
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = ESP_SPI_MOSI,
        .miso_io_num = ESP_SPI_MISO,
        .sclk_io_num = ESP_SPI_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SDSPI_DEFAULT_DMA));
}

void create_sd_spi_stream() {
    // SD SPI config
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = ESP_SPI_SD_CS;
    slot_config.host_id = host.slot;

    // Mount config
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_card_t *card;

    esp_err_t ret;
    do {
        ESP_LOGE(TAG, "Failed to mount SD card via SPI");
        ret = esp_vfs_fat_sdspi_mount(
            "/sdcard",
            &host,
            &slot_config,
            &mount_config,
            &card
        );
        vTaskDelay(500);
    while (ret != ESP_OK);
}