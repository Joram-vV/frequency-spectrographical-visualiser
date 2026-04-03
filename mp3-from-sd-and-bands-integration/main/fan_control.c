#include "fan_control.h"
#include "shared_state.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pid_controller.h"

#include "pca9548a.h"
#include "vl6180.h"

static const char *TAG = "fan_control";

// --- Configuration ---
#define FAN_PWM_FREQ 50
#define FAN_PWM_RES LEDC_TIMER_10_BIT
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE

#define I2C_MASTER_SCL_IO 5
#define I2C_MASTER_SDA_IO 6
#define I2C_MASTER_FREQ_HZ 100000

// Define the 7 GPIOs for your 7 fans. (Adjust these pins as needed)
const int FAN_PWM_GPIOS[NUM_BANDS] = {2, 2, 2, 2, 2, 2, 2};

// Type for a structured VL6180 array (Multi-fan)
#if !SINGLE_FAN_TEST_MODE
typedef struct
{
    bool available;
    uint8_t retries;
} channel_t;
channel_t vl6180_channels[NUM_BANDS];
#endif

// State variables
static PIDController fan_pids[NUM_BANDS];
static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t mux_handle = NULL;    // Handle for the PCA9548A, the I2C multiplexer
static i2c_master_dev_handle_t sensor_handle = NULL; // Store handle for 1 sensor, due to the I2C mux only 1 is on the bus at all times

// --- Helper Functions from your code ---
static uint8_t source_value_to_height(int source_value)
{
    static const uint8_t height_map[] = {14, 32, 56, 79, 100, 115, 126, 135, 200};
    if (source_value < 0 || source_value >= 9)
        return 0;
    return height_map[source_value];
}

static uint32_t speed_to_duty(float speed_percent)
{
    if (speed_percent < 0.0f)
        speed_percent = 0.0f;
    if (speed_percent > 100.0f)
        speed_percent = 100.0f;
    uint32_t max_duty = (1 << FAN_PWM_RES) - 1;
    return (uint32_t)((max_duty * speed_percent) / 100.0f);
}

#define CONTROL_LOOP_MS 20
#define PID_OUTPUT_MIN -50.0f
#define PID_OUTPUT_MAX 50.0f
#define FAN_MIN_PERCENT 60.0f
#define FAN_MAX_PERCENT 80.0f

static float clampf(float x, float lo, float hi)
{
    if (x < lo)
        return lo;
    if (x > hi)
        return hi;
    return x;
}

static float map_range(float x, float in_min, float in_max, float out_min, float out_max)
{
    float t = (x - in_min) / (in_max - in_min);
    t = clampf(t, 0.0f, 1.0f);
    return out_min + t * (out_max - out_min);
}

static bool read_height_mm(uint8_t *distance_mm)
{
    uint8_t distance = 0;
    esp_err_t ret = vl6180_read_range(&distance);
    if (ret == ESP_OK)
    {
        *distance_mm = distance;
        return true;
    }
    // We suppress the timeout print here so it doesn't spam your console if a sensor is missing
    return false;
}

// --- Initialization ---
void fan_control_init(void)
{
    ESP_LOGI(TAG, "Initializing Fan Control & I2C...");

    // 1. Setup LEDC Timer (Once for all fans)
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = FAN_PWM_RES,
        .timer_num = LEDC_TIMER,
        .freq_hz = FAN_PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    // 2. Setup LEDC Channels and PIDs for all 7 fans
    // for (int i = 0; i < NUM_BANDS; i++) {
    for (int i = 0; i < 1; i++)
    {
        ledc_channel_config_t channel_conf = {
            .gpio_num = FAN_PWM_GPIOS[i],
            .speed_mode = LEDC_MODE,
            .channel = LEDC_CHANNEL_0 + i,
            .timer_sel = LEDC_TIMER,
            .duty = speed_to_duty(0.0f),
            .hpoint = 0,
        };
        ESP_ERROR_CHECK(ledc_channel_config(&channel_conf));

        // Initialize PID with your custom limits
        pid_init(&fan_pids[i], 1.2f, 0.4f, 0.05f, -50.0f, 50.0f);
    }

    // 3. Setup I2C Master Bus
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

#if !SINGLE_FAN_TEST_MODE
    /* Set up PCA9548A */
    i2c_device_config_t pca_dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCA9548A_DEFAULT_ADDRESS,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &pca_dev_cfg, &mux_handle));
    pca9548a_config_t pca_config = pca9548a_get_default_config(mux_handle, bus_handle);
    ESP_ERROR_CHECK(pca9548a_setup(&pca_config));
#endif

    /* Set up VL6180 sensor libraries functions & i2c */
    i2c_device_config_t vl_dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = VL6180_I2C_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &vl_dev_cfg, &sensor_handle));
    vl6180_config_t vl_config = vl6180_get_default_config(sensor_handle, bus_handle);
    ESP_ERROR_CHECK(vl6180_setup(&vl_config));

    // 4. Setup Devices based on Mode
#if SINGLE_FAN_TEST_MODE

    ESP_LOGI(TAG, "Running in SINGLE FAN TEST MODE (Bypassing Mux)");

    vTaskDelay(pdMS_TO_TICKS(500));

    if (vl6180_init() != ESP_OK || ESP_ERR_TIMEOUT)
    {
        ESP_LOGE(TAG, "Failed to initialize VL6180 in single sensor mode");
    }
    ESP_ERROR_CHECK(vl6180_configure_default()); // Sets recommended settings of AN4545
    ESP_ERROR_CHECK(vl6180_get_model_id());      // Test reading of the Vl6180

#else
    ESP_LOGI(TAG, "Running in MULTI FAN MODE (Using Mux)");

    vTaskDelay(pdMS_TO_TICKS(500));

    for (int i = 0; i < NUM_BANDS; i++)
    {
        ESP_LOGD(TAG, "VL6180 Start setting up Channel %d!\n", i);
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

        esp_err_t ret_init = vl6180_init();
        if(ret_init != ESP_OK || ESP_ERR_TIMEOUT){
            ESP_LOGE(TAG, "Failed setting up VL6180 on channel %d: %s", i, esp_err_to_name(ret_init));
        }else if (ret_init != ESP_ERR_TIMEOUT){
            ESP_LOGD(TAG, "VL6180 on Channel %d already initialized, skipping default configuration.", i);
            ESP_ERROR_CHECK(vl6180_configure_default());            // Sets recommended settings of AN4545, when not initialized
        }
        ESP_ERROR_CHECK(vl6180_get_model_id());                     // Test reading of the Vl6180
        ESP_LOGD(TAG,"VL6180 Set up on Channel %d!\n", i);
    }

#endif
}

void fan_control_task(void *pvParameters)
{
    const TickType_t xInterval = pdMS_TO_TICKS(CONTROL_LOOP_MS);
    const float dt_s = CONTROL_LOOP_MS / 1000.0f;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    // Local array to hold the heights so we don't lock up the Mutex for long
    float local_target_bands[NUM_BANDS] = {0};

    while (1)
    {
        vTaskDelayUntil(&xLastWakeTime, xInterval);

        // 1. Safely grab the latest 0-8 band data from the visualizer
        if (shared_state_mutex != NULL)
        {
            // We use a timeout of 0 (no blocking). If the visualizer is currently writing to it,
            // we just skip reading this cycle and use the old values to keep the 20ms loop perfectly timed.
            if (xSemaphoreTake(shared_state_mutex, 0) == pdTRUE)
            {
                for (int i = 0; i < NUM_BANDS; i++)
                {
                    local_target_bands[i] = shared_target_heights[i];
                }
                xSemaphoreGive(shared_state_mutex);
            }
        }

        // 2. Process each of the 7 fans
        for (uint8_t i = 0; i < NUM_BANDS; i++)
        {

            // Map the 0-8 visualizer level to a physical mm setpoint
            int band_level = (int)(local_target_bands[i] + 0.5f);
            fan_pids[i].setpoint = (float)source_value_to_height(band_level);

            uint8_t height_mm = 0;
            bool read_success = false;

#if SINGLE_FAN_TEST_MODE
            // In test mode, only read the sensor we initialized (index 6)
            // But we will use the target data from band 6 to drive it so you can see it react to the music!
            if (i == 0)
            {
                read_success = read_height_mm(&height_mm);
            }
#else
            // In full hardware mode, switch the Mux and read the corresponding sensor

            if (vl6180_channels[i].available)
            {
                ESP_ERROR_CHECK(pca9548a_select_channel(i));
                read_success = read_height_mm(&height_mm);
            }
#endif

            // 3. Compute PID and update PWM
            ESP_LOGD(TAG, "Sensor read success: %d, Height: %d mm", read_success, height_mm);

            if (read_success)
            {
                float output = pid_compute(&fan_pids[i], height_mm, dt_s);
                float fan_percent = map_range(output, PID_OUTPUT_MIN, PID_OUTPUT_MAX, FAN_MIN_PERCENT, FAN_MAX_PERCENT);

                uint32_t duty = speed_to_duty(fan_percent);
                ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_0 + i, duty);
                ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_0 + i);
            }
        }
    }
}