#include <stdio.h>
#include "pca9548a.h"
#include "driver/gpio.h"

static const char *TAG = "PCA9548A";
static pca9548a_config_t _dev_cfg; // Internal persistent config

esp_err_t pca9548a_setup(const pca9548a_config_t *cfg)
{
    if (cfg == NULL || cfg->i2c_dev_handle == NULL)
        return ESP_ERR_INVALID_ARG;

    _dev_cfg = *cfg; // Store the config once
    return ESP_OK;
}

esp_err_t pca9548a_probe()
{
    return i2c_master_probe(_dev_cfg.i2c_bus_handle, PCA9548A_DEFAULT_ADDRESS, _dev_cfg.probe_timeout_ms);
}

/*
 *   @brief Select channel number before I2C transactions.
 *   Used before starting a I2C transaction to a channel (/ bus) via the PCA9548A
 *   @param dev_handle: The PCA9548A's I2C device handle
 *   @param channel: Channel (0-7) on the PCA9548A to select for a I2C transaction
 *   @return
 *   - ESP_OK: Success
 *   @return
 *   - ESP_ERR_INVALID_ARGS: Invalid channel given or invalid device handle
 */
esp_err_t pca9548a_select_channel(uint8_t channel)
{
    if (channel > 7)
        return ESP_ERR_INVALID_ARG;

    uint8_t cmd_byte = (1 << channel);
    esp_err_t ret = i2c_master_transmit(_dev_cfg.i2c_dev_handle, &cmd_byte, 1, 100);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Write failed trying to select channel %d, ERROR: %s", channel, esp_err_to_name(ret));
    }
    return ret;
}

/*
 *   @brief TODO: Gets current value of the PCA9548's control register
 *   @param dev_handle: The PCA9548A's I2C device handle
 *   @param control_register: Buffer for the control registeres byte
 *   @return
 *   - ESP_OK: Success
 *   @return
 *   - ESP_ERR_INVALID_ARGS: Invalid channel given or invalid device handle
 */
esp_err_t pca9548a_get_control_register(uint8_t *control_register)
{
    // TODO
    return ESP_OK;
}

/*
 *   @brief Reset control register to 0 (no selected channel).
 *   Used to deselect all channels in the PCA9548A's control register
 *   @param dev_handle: The PCA9548A's I2C device handle
 *   @return
 *   - ESP_OK: Success
 *   @return
 *   - ESP_ERR_INVALID_ARGS: Invalid device handle
 *   @note For a I2C bus reset the RESET pin of the PCA9548A should be used!
 */
esp_err_t pca9548a_reset_control_register()
{
    uint8_t reset = 0x00;
    esp_err_t ret = i2c_master_transmit(_dev_cfg.i2c_dev_handle, &reset, 1, 100); // Sets control register to 0x00 thereby resetting
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Write failed trying to reset control register, ERROR: %s", esp_err_to_name(ret));
    }
    return ret;
}

/*
 *   @brief Reset PCA9548A's I2C busses
 *   This allows recovery should once of the downstream I2C buses get stuck in a low state.
 *   @param dev_handle: The PCA9548A's I2C device handle
 *   @param reset_gpio: The PCA9548A's RESET gpio
 *   @return
 *   - ESP_OK: Success
 *   @return
 *   - ESP_ERR_INVALID_ARGS: Invalid device handle or RESET gpio
 *   @note the power-on reset deselects all channels and initializes the I2C/SMBus state machine
 */
esp_err_t pca9548a_reset_bus()
{
    if (_dev_cfg.reset_gpio == GPIO_NUM_NC)
        return ESP_ERR_INVALID_ARG;

    gpio_set_level(_dev_cfg.reset_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(_dev_cfg.reset_gpio, 1);

    return ESP_OK;
}