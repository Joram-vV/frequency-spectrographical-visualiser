#ifndef PCA9548A_H
#define PCA9548A_H

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

#define PCA9548A_DEFAULT_ADDRESS 0x70

typedef struct
{
    i2c_master_dev_handle_t i2c_dev_handle; // For communication
    i2c_master_bus_handle_t i2c_bus_handle; // For probing
    gpio_num_t reset_gpio;
    int probe_timeout_ms;
} pca9548a_config_t;

/**
 * @brief Default configuration helper
 */
static inline pca9548a_config_t pca9548a_get_default_config(i2c_master_dev_handle_t dev_handle, i2c_master_bus_handle_t bus_handle)
{
    return (pca9548a_config_t){
        .i2c_dev_handle = dev_handle,
        .i2c_bus_handle = bus_handle,
        .reset_gpio = GPIO_NUM_NC,
        .probe_timeout_ms = 100};
}

esp_err_t pca9548a_select_channel(uint8_t channel);
esp_err_t pca9548a_get_control_register(uint8_t *control_register);
esp_err_t pca9548a_reset_control_register();
esp_err_t pca9548a_reset_bus();
esp_err_t pca9548a_setup(const pca9548a_config_t *cfg);
esp_err_t pca9548a_probe();

#endif
