// #include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"

#include "esp_log.h"

#include "vl6180.h"

static const char *TAG = "I2C_SCANNER";

#define I2C_MASTER_SCL_IO 5
#define I2C_MASTER_SDA_IO 6
#define I2C_MASTER_FREQ_HZ 100000 // 100kHz standard mode
#define VL6180_RESET_IO 4

void app_main(void)
{
  i2c_master_bus_handle_t bus_handle;
  i2c_master_dev_handle_t dev_handle;
  i2c_master_bus_config_t bus_config = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = I2C_NUM_0,
      .scl_io_num = I2C_MASTER_SCL_IO,
      .sda_io_num = I2C_MASTER_SDA_IO,
      .glitch_ignore_cnt = 7,
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = VL6180_ADDR,
      .scl_speed_hz = I2C_MASTER_FREQ_HZ,
  };
  ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));

  vTaskDelay(pdMS_TO_TICKS(500));


  if (vl6180_init(dev_handle, VL6180_RESET_IO) && vl6180_configure_default(dev_handle) == ESP_OK)
  {
    printf("VL6180 Initialized!\n");
  }

  uint8_t dev_id;
  ESP_ERROR_CHECK(vl6180_read8(dev_handle, 0x000, &dev_id));
  ESP_LOGI("DEV TEST", "%x", dev_id);

  while (1)
  {
    uint8_t distance;
    esp_err_t ret = vl6180_read_range(dev_handle, &distance, 100);
    if (ret == ESP_OK)
    {
      printf("Distance: %d mm\n", distance);
    }
    else if (ret == ESP_ERR_TIMEOUT)
    {
      ESP_LOGW(TAG, "TIMEOUT");
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    else
    {
      ESP_LOGE(TAG, "Sensor error: %s", esp_err_to_name(ret));
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}