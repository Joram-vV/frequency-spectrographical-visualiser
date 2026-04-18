#include "vl6180.h"
#include "esp_log.h"
// #include "freertos/portmacro.h"
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"

static const char *TAG = "VL6180";

#define I2C_MASTER_SCL_IO 22
#define I2C_MASTER_SDA_IO 21
#define I2C_MASTER_FREQ_HZ 100000 // 100kHz standard mode

#define IDENTIFICATION__MODEL_ID                0x000
#define IDENTIFICATION__MODEL_REV_MAJOR         0x001
#define IDENTIFICATION__MODEL_REV_MINOR         0x002
#define IDENTIFICATION__MODULE_REV_MAJOR        0x003
#define IDENTIFICATION__MODULE_REV_MINOR        0x004
#define IDENTIFICATION__DATE_HI                 0x006
#define IDENTIFICATION__DATE_LO                 0x007
#define IDENTIFICATION__TIME                    0x008  // 16-bit

#define SYSTEM__MODE_GPIO0                      0x010
#define SYSTEM__MODE_GPIO1                      0x011
#define SYSTEM__HISTORY_CTRL                    0x012
#define SYSTEM__INTERRUPT_CONFIG_GPIO           0x014
#define SYSTEM__INTERRUPT_CLEAR                 0x015
#define SYSTEM__FRESH_OUT_OF_RESET              0x016
#define SYSTEM__GROUPED_PARAMETER_HOLD          0x017

#define SYSRANGE__START                          0x018
#define SYSRANGE__THRESH_HIGH                    0x019
#define SYSRANGE__THRESH_LOW                     0x01A
#define SYSRANGE__INTERMEASUREMENT_PERIOD       0x01B
#define SYSRANGE__MAX_CONVERGENCE_TIME          0x01C
#define SYSRANGE__CROSSTALK_COMPENSATION_RATE   0x01E  // 16-bit
#define SYSRANGE__CROSSTALK_VALID_HEIGHT        0x021
#define SYSRANGE__EARLY_CONVERGENCE_ESTIMATE    0x022  // 16-bit
#define SYSRANGE__PART_TO_PART_RANGE_OFFSET     0x024
#define SYSRANGE__RANGE_IGNORE_VALID_HEIGHT     0x025
#define SYSRANGE__RANGE_IGNORE_THRESHOLD        0x026  // 16-bit
#define SYSRANGE__MAX_AMBIENT_LEVEL_MULT        0x02C
#define SYSRANGE__RANGE_CHECK_ENABLES           0x02D
#define SYSRANGE__VHV_RECALIBRATE               0x02E
#define SYSRANGE__VHV_REPEAT_RATE               0x031

#define SYSALS__START                            0x038
#define SYSALS__THRESH_HIGH                      0x03A
#define SYSALS__THRESH_LOW                       0x03C
#define SYSALS__INTERMEASUREMENT_PERIOD         0x03E
#define SYSALS__ANALOGUE_GAIN                    0x03F
#define SYSALS__INTEGRATION_PERIOD               0x040

#define RESULT__RANGE_STATUS                     0x04D
#define RESULT__ALS_STATUS                       0x04E
#define RESULT__INTERRUPT_STATUS_GPIO            0x04F
#define RESULT__ALS_VAL                          0x050  // 16-bit
#define RESULT__HISTORY_BUFFER_0                 0x052  // 16-bit
#define RESULT__HISTORY_BUFFER_1                 0x054  // 16-bit
#define RESULT__HISTORY_BUFFER_2                 0x056  // 16-bit
#define RESULT__HISTORY_BUFFER_3                 0x058  // 16-bit
#define RESULT__HISTORY_BUFFER_4                 0x05A  // 16-bit
#define RESULT__HISTORY_BUFFER_5                 0x05C  // 16-bit
#define RESULT__HISTORY_BUFFER_6                 0x05E  // 16-bit
#define RESULT__HISTORY_BUFFER_7                 0x060  // 16-bit
#define RESULT__RANGE_VAL                        0x062
#define RESULT__RANGE_RAW                        0x064
#define RESULT__RANGE_RETURN_RATE                0x066  // 16-bit
#define RESULT__RANGE_REFERENCE_RATE             0x068  // 16-bit
#define RESULT__RANGE_RETURN_SIGNAL_COUNT        0x06C  // 32-bit
#define RESULT__RANGE_REFERENCE_SIGNAL_COUNT     0x070  // 32-bit
#define RESULT__RANGE_RETURN_AMB_COUNT           0x074  // 32-bit
#define RESULT__RANGE_REFERENCE_AMB_COUNT        0x078  // 32-bit
#define RESULT__RANGE_RETURN_CONV_TIME           0x07C  // 32-bit
#define RESULT__RANGE_REFERENCE_CONV_TIME        0x080  // 32-bit

#define RANGE_SCALER                             0x096  // 16-bit

#define READOUT__AVERAGING_SAMPLE_PERIOD         0x10A
#define FIRMWARE__BOOTUP                         0x119
#define FIRMWARE__RESULT_SCALER                  0x120
#define I2C_SLAVE__DEVICE_ADDRESS                0x212
#define INTERLEAVED_MODE__ENABLE                 0x2A3

esp_err_t vl6180_write8(i2c_master_dev_handle_t dev_handle, uint16_t reg, uint8_t val)
{
    uint8_t data[3] = {(uint8_t)(reg >> 8) & 0xFF, (uint8_t)(reg & 0xFF), val};
    esp_err_t ret = i2c_master_transmit(dev_handle, data, 3, 100);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Write failed at reg 0x%04X: %s (0x%X)", reg, esp_err_to_name(ret), ret);
    }
    return ret;
}

esp_err_t vl6180_write16(i2c_master_dev_handle_t dev_handle, uint16_t reg, uint16_t val)
{
    uint8_t data[4] = {(uint8_t)(reg >> 8) & 0xFF, (uint8_t)(reg & 0xFF), (uint8_t)(val >> 8) & 0xFF, (uint8_t)(val & 0xFF)};
    esp_err_t ret = i2c_master_transmit(dev_handle, data, 4, 100);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Write failed at reg 0x%04X: %s (0x%X)", reg, esp_err_to_name(ret), ret);
    }
    return ret;
}

esp_err_t vl6180_read8(i2c_master_dev_handle_t dev_handle, uint16_t reg, uint8_t *val)
{
    uint8_t reg_addr[2] = {(uint8_t)(reg >> 8) & 0xFF, (uint8_t)(reg & 0xFF)};

    esp_err_t ret = i2c_master_transmit_receive(dev_handle, reg_addr, 2, val, 1, 100);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Read failed at reg 0x%04X: %s (0x%X)", reg, esp_err_to_name(ret), ret);
    }
    return ret;
}

esp_err_t vl6180x_WaitDeviceBooted(i2c_master_dev_handle_t dev_handle)
{
    uint8_t FreshOutReset;
    int ret;
    int retries = 100;
    do
    {
        ret = vl6180_read8(dev_handle, REG_FRESH_OUT_OF_RESET, &FreshOutReset);

        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "Retrying boot read...");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        ESP_LOGD(TAG, "FreshOutOfReset: 0x%02X", FreshOutReset);

        if (FreshOutReset == 1)
            return ESP_OK;

        vTaskDelay(pdMS_TO_TICKS(10));

    } while (--retries > 0);

    return ESP_ERR_TIMEOUT;
}

esp_err_t vl6180_configure_default(i2c_master_dev_handle_t dev_handle){
    
  // "Recommended : Public registers"

  // readout__averaging_sample_period = 48
  vl6180_write8(dev_handle, READOUT__AVERAGING_SAMPLE_PERIOD, 0x30);

  // sysals__analogue_gain_light = 6 (ALS gain = 1 nominal, actually 1.01 according to table "Actual gain values" in datasheet)
  vl6180_write8(dev_handle, SYSALS__ANALOGUE_GAIN, 0x46);

  // sysrange__vhv_repeat_rate = 255 (auto Very High Voltage temperature recalibration after every 255 range measurements)
  vl6180_write8(dev_handle, SYSRANGE__VHV_REPEAT_RATE, 0xFF);

  // sysals__integration_period = 99 (100 ms)
  vl6180_write16(dev_handle, SYSALS__INTEGRATION_PERIOD, 0x0063);

  // sysrange__vhv_recalibrate = 1 (manually trigger a VHV recalibration)
  vl6180_write8(dev_handle, SYSRANGE__VHV_RECALIBRATE, 0x01);


  // "Optional: Public registers"

  // sysrange__intermeasurement_period = 9 (100 ms)
  vl6180_write8(dev_handle, SYSRANGE__INTERMEASUREMENT_PERIOD, 0x09);

  // sysals__intermeasurement_period = 49 (500 ms)
  vl6180_write8(dev_handle, SYSALS__INTERMEASUREMENT_PERIOD, 0x31);

  // als_int_mode = 4 (ALS new sample ready interrupt); range_int_mode = 4 (range new sample ready interrupt)
  vl6180_write8(dev_handle, SYSTEM__INTERRUPT_CONFIG_GPIO, 0x24);


  // Reset other settings to power-on defaults

  // sysrange__max_convergence_time = 49 (49 ms)
  vl6180_write8(dev_handle, SYSRANGE__MAX_CONVERGENCE_TIME, 0x31);

  // disable interleaved mode
  vl6180_write8(dev_handle, INTERLEAVED_MODE__ENABLE, 0);

  return ESP_OK;
}

esp_err_t vl6180_init(i2c_master_dev_handle_t dev_handle, gpio_num_t reset_gpio)
{
    /* Chip enable */
    gpio_set_direction(reset_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(reset_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(reset_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Wait until vl6180 has booted up */
    esp_err_t ret = (vl6180x_WaitDeviceBooted(dev_handle));
    if (ret != ESP_OK)
    {
        ESP_LOGD("WAITDEVICEBOOT RET:", "%s", esp_err_to_name(ret));
    }
    else
    {
        // Recommended initialization settings SR03: see AN4545 Section 9
        vl6180_write8(dev_handle, 0x0207, 0x01);
        vl6180_write8(dev_handle, 0x0208, 0x01);
        vl6180_write8(dev_handle, 0x0096, 0x00);
        vl6180_write8(dev_handle, 0x0097, 0xfd);
        vl6180_write8(dev_handle, 0x00e3, 0x01);
        vl6180_write8(dev_handle, 0x00e4, 0x03);
        vl6180_write8(dev_handle, 0x00e5, 0x02);
        vl6180_write8(dev_handle, 0x00e6, 0x01);
        vl6180_write8(dev_handle, 0x00e7, 0x03);
        vl6180_write8(dev_handle, 0x00f5, 0x02);
        vl6180_write8(dev_handle, 0x00d9, 0x05);
        vl6180_write8(dev_handle, 0x00db, 0xce);
        vl6180_write8(dev_handle, 0x00dc, 0x03);
        vl6180_write8(dev_handle, 0x00dd, 0xf8);
        vl6180_write8(dev_handle, 0x009f, 0x00);
        vl6180_write8(dev_handle, 0x00a3, 0x3c);
        vl6180_write8(dev_handle, 0x00b7, 0x00);
        vl6180_write8(dev_handle, 0x00bb, 0x3c);
        vl6180_write8(dev_handle, 0x00b2, 0x09);
        vl6180_write8(dev_handle, 0x00ca, 0x09);
        vl6180_write8(dev_handle, 0x0198, 0x01);
        vl6180_write8(dev_handle, 0x01b0, 0x17);
        vl6180_write8(dev_handle, 0x01ad, 0x00);
        vl6180_write8(dev_handle, 0x00ff, 0x05);
        vl6180_write8(dev_handle, 0x0100, 0x05);
        vl6180_write8(dev_handle, 0x0199, 0x05);
        vl6180_write8(dev_handle, 0x01a6, 0x1b);
        vl6180_write8(dev_handle, 0x01ac, 0x3e);
        vl6180_write8(dev_handle, 0x01a7, 0x1f);
        vl6180_write8(dev_handle, 0x0030, 0x00);

        // Clear reset flag
        vl6180_write8(dev_handle, REG_FRESH_OUT_OF_RESET, 0x00);
    }

    return ret;
}

esp_err_t vl6180_read_range(i2c_master_dev_handle_t dev_handle, uint8_t *range, uint32_t io_timeout_ms)
{
    // 1. Clear any lingering interrupts first
    vl6180_write8(dev_handle, REG_SYSTEM_INTERRUPT, 0x07);

    // 2. Start single-shot measurement
    esp_err_t err = vl6180_write8(dev_handle, REG_SYSRANGE_START, 0x01);
    if (err != ESP_OK)
        return err;

    uint32_t start_ms = xTaskGetTickCount() * portTICK_PERIOD_MS; // Record start time
    uint8_t status = 0;

    // 3. Poll until new sample ready (bit 2 = 0x04) or timeout
    while (true)
    {
        err = vl6180_read8(dev_handle, RESULT__INTERRUPT_STATUS_GPIO, &status);
        if (err != ESP_OK)
            return err;

        if ((status & 0x07) == 0x04) // Bits 2:0 indicate new sample ready
            break;

        uint32_t elapsed = (xTaskGetTickCount() * portTICK_PERIOD_MS) - start_ms;
        if (io_timeout_ms > 0 && elapsed > io_timeout_ms)
        {
            ESP_LOGE(TAG, "Range timeout! Status: 0x%02X", status);
            return ESP_ERR_TIMEOUT;
        }

        vTaskDelay(pdMS_TO_TICKS(1)); // small delay to avoid busy loop
    }

    // 4. Read range result
    err = vl6180_read8(dev_handle, REG_RESULT_RANGE_VAL, range);

    // 5. Clear interrupt
    vl6180_write8(dev_handle, REG_SYSTEM_INTERRUPT, 0x07);

    return err;
}

esp_err_t vl6180_set_offset(i2c_master_dev_handle_t dev_handle, int offset)
{
    esp_err_t ret = vl6180_write8(dev_handle, SYSRANGE__PART_TO_PART_RANGE_OFFSET, (uint8_t)offset);

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Set Offset: %d", offset);
    }

    return ret;
}