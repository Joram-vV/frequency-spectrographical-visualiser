#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "I2C_SCANNER";

#define I2C_MASTER_SCL_IO           22      
#define I2C_MASTER_SDA_IO           21      
#define I2C_MASTER_FREQ_HZ          100000  // 100kHz standard mode

void app_main(void)
{
    // 1. Configure the I2C Master Bus
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

    ESP_LOGI(TAG, "Starting I2C scan...");
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");

    for (int i = 0; i < 128; i += 16) {
        printf("%02x: ", i);
        for (int j = 0; j < 16; j++) {
            uint8_t address = i + j;
            
            // Skip reserved addresses (0x00-0x07 and 0x78-0x7F)
            if (address < 0x08 || address > 0x77) {
                printf("   ");
                continue;
            }

            // 2. Probe the address
            // i2c_master_probe checks if a device ACKs its address
            esp_err_t ret = i2c_master_probe(bus_handle, address, 100);

            if (ret == ESP_OK) {
                printf("%02x ", address);
            } else {
                printf("-- ");
            }
        }
        printf("\n");
    }

    ESP_LOGI(TAG, "Scan completed.");

    // Clean up (optional if the app stays running)
    // ESP_ERROR_CHECK(i2c_del_master_bus(bus_handle));
}