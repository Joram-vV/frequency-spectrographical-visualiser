#include "vl6180.h"

static const char *TAG = "VL6180";
static vl6180_config_t _dev_cfg; // Internal persistent config

/* Internal helper functions */
esp_err_t vl6180_write8(uint16_t reg, uint8_t val)
{
    uint8_t data[3] = {(uint8_t)(reg >> 8) & 0xFF, (uint8_t)(reg & 0xFF), val};
    esp_err_t ret = i2c_master_transmit(_dev_cfg.i2c_dev_handle, data, 3, 100);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Write failed at reg 0x%04X: %s (0x%X)", reg, esp_err_to_name(ret), ret);
    }
    return ret;
}

esp_err_t vl6180_write16(uint16_t reg, uint16_t val)
{
    uint8_t data[4] = {(uint8_t)(reg >> 8) & 0xFF, (uint8_t)(reg & 0xFF), (uint8_t)(val >> 8) & 0xFF, (uint8_t)(val & 0xFF)};
    esp_err_t ret = i2c_master_transmit(_dev_cfg.i2c_dev_handle, data, 4, 100);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Write failed at reg 0x%04X: %s (0x%X)", reg, esp_err_to_name(ret), ret);
    }
    return ret;
}

esp_err_t vl6180_read8(uint16_t reg, uint8_t *val)
{
    uint8_t reg_addr[2] = {(uint8_t)(reg >> 8) & 0xFF, (uint8_t)(reg & 0xFF)};

    esp_err_t ret = i2c_master_transmit_receive(_dev_cfg.i2c_dev_handle, reg_addr, 2, val, 1, 100);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Read failed at reg 0x%04X: %s (0x%X)", reg, esp_err_to_name(ret), ret);
    }
    return ret;
}

esp_err_t vl6180_wait_device_booted()
{
    uint8_t FreshOutReset;
    int ret;
    int retries = 100;
    do
    {
        ret = vl6180_read8(REG_FRESH_OUT_OF_RESET, &FreshOutReset);

        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "Retrying boot read...");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (FreshOutReset == 1)
            return ESP_OK;

        vTaskDelay(pdMS_TO_TICKS(10));

    } while (--retries > 0);

    return ESP_ERR_TIMEOUT;
}
/* End Internal helper functions*/

/*
 * @brief Probe VL6180 to check whether it's on the bus
 */
esp_err_t vl6180_probe()
{
    return i2c_master_probe(_dev_cfg.i2c_bus_handle, VL6180_I2C_ADDR, _dev_cfg.probe_timeout_ms);
}

/**
 * @brief Performs manual offset calibration for the VL6180X.
 * @note Ensure a target is placed exactly 50mm from the sensor before calling.
 * @param calibrated_offset Pointer to store the calculated offset value.
 * @return ESP_OK on success, or error code from I2C transactions.
 */
esp_err_t vl6180_calibrate_offset(int8_t *calibrated_offset)
{
    esp_err_t ret;
    uint32_t sum_range = 0;
    uint8_t current_range = 0;
    const int samples = 10;
    const int target_distance = 150; // mm, 150mm instead of 50mm from AN4545 since the VL6180 only started reading from 100+ mm

    uint8_t prev_offset;
    vl6180_read8(SYSRANGE__PART_TO_PART_RANGE_OFFSET, &prev_offset);
    ESP_LOGD(TAG, "Previous offset: %d", prev_offset);

    ret = vl6180_write8(SYSRANGE__PART_TO_PART_RANGE_OFFSET, 0x00);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Starting calibration: Place target 50mm away...");
    vTaskDelay(pdMS_TO_TICKS(500));

    for (int i = 0; i < samples; i++)
    {
        ret = vl6180_read_range(&current_range);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Calibration failed at sample %d", i);
            return ret;
        }
        sum_range += current_range;
        ESP_LOGD(TAG, "Sample %d: %d mm", i, current_range);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Formula: Offset = Target Distance - Average Measured Distance
    int16_t average_range = (int16_t)(sum_range / samples);
    int16_t offset_required = target_distance - average_range;

    if (offset_required > 127) offset_required = 127;
    if (offset_required < -128) offset_required = -128;

    int8_t final_offset = (int8_t)offset_required;
    ESP_LOGD(TAG, "Target range: %d | Average range: %d | Offset required %d | Final offset %d", target_distance, average_range, offset_required, final_offset);
    
    ret = vl6180_write8(SYSRANGE__PART_TO_PART_RANGE_OFFSET, (uint8_t)final_offset);
    
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Calibration Complete. Avg: %dmm, New Offset: %d", average_range, final_offset);
        if (calibrated_offset != NULL) {
            *calibrated_offset = final_offset;
        }
    }

    return ret;
}

esp_err_t vl6180_configure_default()
{

    // "Recommended : Public registers"

    // readout__averaging_sample_period = 48
    vl6180_write8(READOUT__AVERAGING_SAMPLE_PERIOD, 0x30);

    // sysals__analogue_gain_light = 6 (ALS gain = 1 nominal, actually 1.01 according to table "Actual gain values" in datasheet)
    vl6180_write8(SYSALS__ANALOGUE_GAIN, 0x46);

    // sysrange__vhv_repeat_rate = 255 (auto Very High Voltage temperature recalibration after every 255 range measurements)
    vl6180_write8(SYSRANGE__VHV_REPEAT_RATE, 0xFF);

    // sysals__integration_period = 99 (100 ms)
    vl6180_write16(SYSALS__INTEGRATION_PERIOD, 0x0063);

    // sysrange__vhv_recalibrate = 1 (manually trigger a VHV recalibration)
    vl6180_write8(SYSRANGE__VHV_RECALIBRATE, 0x01);

    // "Optional: Public registers"

    // sysrange__intermeasurement_period = 9 (100 ms)
    vl6180_write8(SYSRANGE__INTERMEASUREMENT_PERIOD, 0x09);

    // sysals__intermeasurement_period = 49 (500 ms)
    vl6180_write8(SYSALS__INTERMEASUREMENT_PERIOD, 0x31);

    // als_int_mode = 4 (ALS new sample ready interrupt); range_int_mode = 4 (range new sample ready interrupt)
    vl6180_write8(SYSTEM__INTERRUPT_CONFIG_GPIO, 0x24);

    // Reset other settings to power-on defaults

    // sysrange__max_convergence_time = 49 (49 ms)
    vl6180_write8(SYSRANGE__MAX_CONVERGENCE_TIME, 0x31);

    // disable interleaved mode
    vl6180_write8(INTERLEAVED_MODE__ENABLE, 0);

    return ESP_OK;
}

esp_err_t vl6180_get_model_id()
{
    uint8_t mod_id;
    esp_err_t ret = vl6180_read8(IDENTIFICATION__MODEL_ID, &mod_id);
    if (mod_id != VL6180X_MODEL_ID)
    { // 0xB4 = VL6180X
        ESP_LOGE(TAG, "Incorrect model identification number received: 0x%x", &mod_id);
    }
    else
    {
        ESP_LOGD(TAG, "Model identification number: 0x%x", mod_id);
    }
    return ret;
}

esp_err_t vl6180_read_range(uint8_t *range)
{
    vl6180_write8(REG_SYSTEM_INTERRUPT, 0x07);

    esp_err_t err = vl6180_write8(REG_SYSRANGE_START, 0x01);
    if (err != ESP_OK)
        return err;

    uint32_t start_ms = xTaskGetTickCount() * portTICK_PERIOD_MS; // Record start time
    uint8_t status = 0;

    while (true)
    {
        err = vl6180_read8(RESULT__INTERRUPT_STATUS_GPIO, &status);
        if (err != ESP_OK)
            return err;

        if ((status & 0x07) == 0x04) // Bits 2:0 indicate new sample ready
            break;

        uint32_t elapsed = (xTaskGetTickCount() * portTICK_PERIOD_MS) - start_ms;
        if (_dev_cfg.range_timeout_ms > 0 && elapsed > _dev_cfg.range_timeout_ms)
        {
            ESP_LOGE(TAG, "Range timeout! Status: 0x%02X", status);
            return ESP_ERR_TIMEOUT;
        }

        vTaskDelay(pdMS_TO_TICKS(1)); // small delay to avoid busy loop
    }

    err = vl6180_read8(REG_RESULT_RANGE_VAL, range);

    vl6180_write8(REG_SYSTEM_INTERRUPT, 0x07);

    return err;
}

/*
* @brief Sets up the configuration of the VL6180
* @note This function MUST be called before any other! Otherwise many errors will occur!
*/
esp_err_t vl6180_setup(const vl6180_config_t *cfg)
{
    if (cfg == NULL || cfg->i2c_dev_handle == NULL)
        return ESP_ERR_INVALID_ARG;

    _dev_cfg = *cfg; // Store the config once
    return ESP_OK;
}

/*
 * @brief Initializes the VL6180 sensor.
 * * This function sets up the recommended initialization settings and optionally
 * resets the vl6180 via the provided GPIO pin, which should be connected to the GPIO0 pin.
 * @param i2c_dev_handle Handle to the initialized I2C master device.
 * @param reset_gpio The GPIO number used for hardware reset. On the VL6180 GPIO0. Use GPIO_NUM_NC if not used.
 * @return
 * - ESP_OK: Success
 * @return
 * - ESP_ERR_TIMEOUT: If the VL6180 has already been initialized or is still in reset mode.
 * @return
 * - ESP_ERR_INVALID_ARG: If the handle is NULL
 */
esp_err_t vl6180_init(){
    /* Chip enable if a reset_gpio is given */
    if (_dev_cfg.reset_gpio != GPIO_NUM_NC)
    {
        gpio_set_direction(_dev_cfg.reset_gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(_dev_cfg.reset_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(_dev_cfg.reset_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    /* Wait until vl6180 has booted up */
    esp_err_t ret = (vl6180_wait_device_booted(_dev_cfg.i2c_dev_handle));
    if (ret != ESP_OK)
    {
        if (ret == ESP_ERR_TIMEOUT)
        {
            ESP_LOGD(TAG, "Device already initialized or still in reset mode, check SHUTDOWN or GPIO0 pin (%s)", esp_err_to_name(ret));
        }
        else
        {
            ESP_LOGD(TAG, "Unexpected error during vl6180x_WaitDeviceBooted, RET = %s", esp_err_to_name(ret));
        }
    }
    else
    {
        // Recommended initialization settings SR03: see AN4545 Section 9
        vl6180_write8(0x0207, 0x01);
        vl6180_write8(0x0208, 0x01);
        vl6180_write8(0x0096, 0x00);
        vl6180_write8(0x0097, 0xfd);
        vl6180_write8(0x00e3, 0x01);
        vl6180_write8(0x00e4, 0x03);
        vl6180_write8(0x00e5, 0x02);
        vl6180_write8(0x00e6, 0x01);
        vl6180_write8(0x00e7, 0x03);
        vl6180_write8(0x00f5, 0x02);
        vl6180_write8(0x00d9, 0x05);
        vl6180_write8(0x00db, 0xce);
        vl6180_write8(0x00dc, 0x03);
        vl6180_write8(0x00dd, 0xf8);
        vl6180_write8(0x009f, 0x00);
        vl6180_write8(0x00a3, 0x3c);
        vl6180_write8(0x00b7, 0x00);
        vl6180_write8(0x00bb, 0x3c);
        vl6180_write8(0x00b2, 0x09);
        vl6180_write8(0x00ca, 0x09);
        vl6180_write8(0x0198, 0x01);
        vl6180_write8(0x01b0, 0x17);
        vl6180_write8(0x01ad, 0x00);
        vl6180_write8(0x00ff, 0x05);
        vl6180_write8(0x0100, 0x05);
        vl6180_write8(0x0199, 0x05);
        vl6180_write8(0x01a6, 0x1b);
        vl6180_write8(0x01ac, 0x3e);
        vl6180_write8(0x01a7, 0x1f);
        vl6180_write8(0x0030, 0x00);

        // Clear reset flag
        vl6180_write8(REG_FRESH_OUT_OF_RESET, 0x00);

        ESP_LOGD(TAG, "Finished initialization");
    }

    return ret;
}
