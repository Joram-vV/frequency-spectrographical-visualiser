// #include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

// Own libraries
#include "vl6180.h"
#include "pca9548a.h"

// I2C pins
#define I2C_MASTER_SCL_IO 22
#define I2C_MASTER_SDA_IO 21

// Helper functions
void initialize_all_vl6180();
void read_all_vl6180(uint32_t intermeasurement_ms);
void check_vl6180(uint8_t channel_num, esp_err_t err);

static const char *TAG = "I2C_SCANNER";

typedef struct
{
  bool available;
  uint8_t retries;
} channel_t;

channel_t vl6180_channels[8]; // Global

void app_main(void)
{
  /* Setup I2C bus on port 0 */
  i2c_master_bus_handle_t bus_handle;
  i2c_master_bus_config_t bus_config = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = I2C_NUM_0,
      .scl_io_num = I2C_MASTER_SCL_IO,
      .sda_io_num = I2C_MASTER_SDA_IO,
      .glitch_ignore_cnt = 7,
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

  /* Setup PCA9548A: I2C Multiplexer */
  i2c_master_dev_handle_t pca9548a_dev_handle;
  i2c_device_config_t pca9548a_dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = PCA9548A_DEFAULT_ADDRESS,
      .scl_speed_hz = I2C_MASTER_FREQ_HZ,
  };
  ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &pca9548a_dev_cfg, &pca9548a_dev_handle));
  pca9548a_config_t pca_config = pca9548a_get_default_config(pca9548a_dev_handle, bus_handle);
  ESP_ERROR_CHECK(pca9548a_setup(&pca_config));

  /* Setup VL6180: ToF Sensor */
  i2c_master_dev_handle_t vl6180_handle;
  i2c_device_config_t vl6180_i2c_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = VL6180_I2C_ADDR,
      .scl_speed_hz = I2C_MASTER_FREQ_HZ,
  };
  ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &vl6180_i2c_cfg, &vl6180_handle));
  vl6180_config_t vl6180_config = vl6180_get_default_config(vl6180_handle, bus_handle);
  ESP_ERROR_CHECK(vl6180_setup(&vl6180_config));

  vTaskDelay(pdMS_TO_TICKS(500));

  initialize_all_vl6180();

  while (1)
  {
    read_all_vl6180(100);
  }
}

void initialize_all_vl6180()
{
  for (uint8_t i = 0; i < 8; i++)
  {
    if (vl6180_channels[i].available == 0)
    {
      printf("VL6180 Start setting up Channel %d!\n", i);
      ESP_ERROR_CHECK(pca9548a_select_channel(i)); // Select I2C multiplex channel
      vTaskDelay(pdMS_TO_TICKS(5));
      esp_err_t probe_ret = vl6180_probe();
      if (probe_ret == ESP_ERR_NOT_FOUND)
      {
        ESP_LOGD(TAG, "No VL6180 succesfully probed on channel: %d", i);
        vl6180_channels[i].available = false;
        continue;
      }
      else if (probe_ret != ESP_OK)
      {
        ESP_LOGE(TAG, "Error whilst probing VL6180 on channel %d: %s", i, esp_err_to_name(probe_ret));
      }

      vl6180_channels[i].available = true;
      vl6180_channels[i].retries = 0;
      ESP_ERROR_CHECK_WITHOUT_ABORT(vl6180_init());              // Initialize Vl6180
      ESP_ERROR_CHECK_WITHOUT_ABORT(vl6180_configure_default()); // Sets recommended settings of AN4545
      ESP_ERROR_CHECK(vl6180_get_model_id());                    // Test reading of the Vl6180
      printf("VL6180 Set up on Channel %d!\n", i);
    }
  }
}

void read_all_vl6180(uint32_t intermeasurement_ms)
{
  int range_array[8] = {0};
  for (int channel = 0; channel < 8; channel++)
  {
    if (!vl6180_channels[channel].available)
    {
      continue;
    }

    pca9548a_select_channel(channel);

    uint8_t distance;
    esp_err_t ret = vl6180_read_range(&distance);
    if (ret == ESP_OK)
    {
      range_array[channel] = distance;
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
    // check_vl6180(channel, ret);
    vTaskDelay(pdMS_TO_TICKS(intermeasurement_ms));
  }

  ESP_LOGI("VL6180", "Distances: %d %d %d %d %d %d %d %d ", range_array[0], range_array[1], range_array[2], range_array[3], range_array[4], range_array[5], range_array[6], range_array[7]);
}

// Function, used in read_all function, which sets a VL6180 to unavailable upon multiple errors
void check_vl6180(uint8_t channel_num, esp_err_t err)
{
  if (vl6180_channels[channel_num].available == 1)
  {
    if (err != ESP_OK)
    {
      if (vl6180_channels[channel_num].retries < 10)
      {
        vl6180_channels[channel_num].retries++;
        return;
      }

      ESP_LOGD("VL6180_ERROR_CHECK", "Checking Channel %d's VL6180 due to %d retries", channel_num, vl6180_channels[channel_num].retries);
      ESP_ERROR_CHECK(pca9548a_select_channel(channel_num));

      // Set Vl6180 to unavailable on the specific channel when not detected on the channel's bus.
      esp_err_t ret = vl6180_probe();
      if (ret != ESP_OK)
      {
        vl6180_channels[channel_num].retries = 0;
        vl6180_channels[channel_num].available = 0;
        ESP_LOGE("VL6180_ERROR_CHECK", "Channel %d: VL6180 not available on bus, ERROR: %s", channel_num, esp_err_to_name(ret));
        return;
      }
      else
      {
        vl6180_channels[channel_num].retries = 0;
        vl6180_channels[channel_num].available = 0;
        ESP_LOGE("VL6180_ERROR_CHECK", "Channel %d: VL6180 avaiable on bus but communication or initialization error, ERROR: %s", channel_num, esp_err_to_name(ret));
        return;
      }
    }

    vl6180_channels[channel_num].retries = 0; // Reset retries to 0 on succes
  }
}