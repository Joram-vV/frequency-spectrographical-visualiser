#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include "esp_err.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pid_controller.h"
#include "vl6180.h"

static const char *TAG = "main";
static const float setpoint_table[] = {14.0f, 32.0f, 56.0f, 79.0f, 100.0f, 115.0f, 126.0f, 135.0f};


#define FAN_PWM_CHANNEL LEDC_CHANNEL_0 // Use PWM channel 0
#define FAN_PWM_FREQ 25000             // 25kHz PWM frequency for 4-wire fans
#define FAN_PWM_RES LEDC_TIMER_10_BIT  // 10-bit resolution (0-1023)
#define FAN_GPIO 2                     // GPIO pin for fan control

PIDController fan_pid; // PID controller instance

#define LED_PIN GPIO_NUM_2
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_FREQ_HZ FAN_PWM_FREQ
#define LEDC_RESOLUTION FAN_PWM_RES

#define I2C_MASTER_SCL_IO 5
#define I2C_MASTER_SDA_IO 6
#define I2C_MASTER_FREQ_HZ 100000 // 100kHz standard mode
#define VL6180_RESET_IO 4

#define PID_OUTPUT_MIN -50.0f
#define PID_OUTPUT_MAX 50.0f
#define FAN_MIN_PERCENT 0.0f
#define FAN_MAX_PERCENT 100.0f

extern const char _binary_2_txt_start[] asm("_binary_2_txt_start");
extern const char _binary_2_txt_end[] asm("_binary_2_txt_end");

static uint32_t speed_to_duty(float speed_percent)
{
    if (speed_percent < 0.0f)
    {
        speed_percent = 0.0f;
    }
    if (speed_percent > 100.0f)
    {
        speed_percent = 100.0f;
    }

    uint32_t max_duty = (1 << FAN_PWM_RES) - 1;
    return (uint32_t)((max_duty * speed_percent) / 100.0f);
}

static float clampf(float x, float lo, float hi)
{
    if (x < lo)
    {
        return lo;
    }
    if (x > hi)
    {
        return hi;
    }
    return x;
}

static float map_range(float x, float in_min, float in_max, float out_min, float out_max)
{
    float t = (x - in_min) / (in_max - in_min);
    t = clampf(t, 0.0f, 1.0f);
    return out_min + t * (out_max - out_min);
}

void set_fan_speed(float speed_percent)
{
    uint32_t duty = speed_to_duty(speed_percent);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

int64_t pulse_in_us(gpio_num_t pin, int level, int64_t timeout_us)
{
    int64_t start = esp_timer_get_time();

    // Wait for any previous pulse to end
    while (gpio_get_level(pin) == level)
    {
        if (esp_timer_get_time() - start > timeout_us)
            return -1;
    }

    // Wait for pulse to start
    while (gpio_get_level(pin) != level)
    {
        if (esp_timer_get_time() - start > timeout_us)
            return -1;
    }

    int64_t pulse_start = esp_timer_get_time();

    // Wait for pulse to end
    while (gpio_get_level(pin) == level)
    {
        if (esp_timer_get_time() - start > timeout_us)
            return -1;
    }

    return esp_timer_get_time() - pulse_start; // pulse width in us
}

uint8_t read_hight(i2c_master_dev_handle_t dev_handle)
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
    return distance;
}

void pid_task(i2c_master_dev_handle_t dev_handle)
{
    const TickType_t xInterval = pdMS_TO_TICKS(20); // 20 ms control loop
    const float dt_s = 0.02f;
    const TickType_t setpoint_step = pdMS_TO_TICKS(2000);
    const int setpoint_count = sizeof(setpoint_table) / sizeof(setpoint_table[0]);
    TickType_t xLastWakeTime = xTaskGetTickCount();
    TickType_t last_setpoint_step = xLastWakeTime;
    int setpoint_idx = 0;
    int setpoint_dir = 1;

    // PID output is centered around 0, then mapped to 0-100% fan command.
    pid_init(&fan_pid, 1.2f, 0.4f, 0.05f, PID_OUTPUT_MIN, PID_OUTPUT_MAX);
    fan_pid.setpoint = setpoint_table[setpoint_idx];
    printf("setpoint: %f\n", fan_pid.setpoint);

    while (1)
    {
        vTaskDelayUntil(&xLastWakeTime, xInterval); // Precise timing
        

        TickType_t now = xTaskGetTickCount();
        if ((now - last_setpoint_step) >= setpoint_step)
        {
            last_setpoint_step = now;
            setpoint_idx += setpoint_dir;


            if (setpoint_idx >= (setpoint_count - 1))
            {
                setpoint_idx = setpoint_count - 1;
                setpoint_dir = -1;
            }
            else if (setpoint_idx <= 0)
            {
                setpoint_idx = 0;
                setpoint_dir = 1;
            }

            fan_pid.setpoint = setpoint_table[setpoint_idx];
            printf("setpoint: %f\n", fan_pid.setpoint);
        }
        printf("setpoint id = %d\n",setpoint_idx);
        uint8_t hight = read_hight(dev_handle);

        if (hight > 400)
        {
            continue;
        }

        // Compute PID output with 20ms time delta (matches control loop)
        float output = pid_compute(&fan_pid, hight, dt_s);
        float fan_percent = map_range(output, PID_OUTPUT_MIN, PID_OUTPUT_MAX, FAN_MIN_PERCENT, FAN_MAX_PERCENT);
        set_fan_speed(fan_percent); // Apply control signal
    }
}

void app_main(void)
{
    /* PWM Timer Setup */
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_RESOLUTION,
        .timer_num = LEDC_TIMER,
        .freq_hz = LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t channel_conf = {
        .gpio_num = LED_PIN,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .duty = speed_to_duty(0.0f), // Start fully off
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_conf));
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
        pid_task(dev_handle);
    }
}
