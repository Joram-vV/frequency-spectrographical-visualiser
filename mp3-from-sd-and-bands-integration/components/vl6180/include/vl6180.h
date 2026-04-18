#ifndef VL6180_H
#define VL6180_H

#include "driver/i2c_master.h"

#define VL6180_ADDR 0x29

// Register Definitions
#define REG_FRESH_OUT_OF_RESET 0x016
#define REG_SYSRANGE_START     0x018
#define REG_RESULT_RANGE_VAL   0x062
#define REG_SYSTEM_INTERRUPT   0x015


esp_err_t vl6180_read8(i2c_master_dev_handle_t dev_handle, uint16_t reg, uint8_t *val);
esp_err_t vl6180_init(i2c_master_dev_handle_t dev_handle, gpio_num_t reset_gpio);
esp_err_t vl6180_configure_default(i2c_master_dev_handle_t dev_handle);
esp_err_t vl6180_read_range(i2c_master_dev_handle_t dev_handle, uint8_t *range, uint32_t io_timeout_ms);
esp_err_t vl6180_set_offset(i2c_master_dev_handle_t dev_handle, int offset);


#endif