#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/ledc.h"
#include "driver/i2c_master.h"

#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pid_controller.h"
#include "vl6180.h"

static const char *TAG = "main";
extern const char _binary_2_txt_start[] asm("_binary_2_txt_start");
extern const char _binary_2_txt_end[] asm("_binary_2_txt_end");

#define FAN_PWM_FREQ 25000             // 25kHz PWM frequency for 4-wire fans
#define FAN_PWM_RES LEDC_TIMER_10_BIT  // 10-bit resolution (0-1023)

static PIDController fan_pid;

#define FAN_PWM_GPIO GPIO_NUM_2
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_RESOLUTION FAN_PWM_RES

#define I2C_MASTER_SCL_IO 5
#define I2C_MASTER_SDA_IO 6
#define I2C_MASTER_FREQ_HZ 100000 // 100kHz standard mode
#define VL6180_RESET_IO 4
#define CONTROL_LOOP_MS 20
#define SETPOINT_STEP_MS 1000

#define PID_OUTPUT_MIN -50.0f
#define PID_OUTPUT_MAX 50.0f
#define FAN_MIN_PERCENT 0.0f
#define FAN_MAX_PERCENT 100.0f

static uint8_t source_value_to_height(long source_value)
{
    static const uint8_t height_map[] = {
        14,  // 0
        32, // 1
        56, // 2
        79, // 3
        100, // 4
        115, // 5
        126, // 6
        135, // 7
        200, // 8
    };

    if (source_value < 0 || source_value >= (long)(sizeof(height_map) / sizeof(height_map[0])))
    {
        return 0;
    }

    return height_map[source_value];
}

typedef struct
{
    const char *cursor;
} setpoint_sequence_state_t;

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

static void set_fan_speed(float speed_percent)
{
    uint32_t duty = speed_to_duty(speed_percent);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

static bool read_height_mm(i2c_master_dev_handle_t dev_handle, uint8_t *distance_mm)
{
    uint8_t distance = 0;
    esp_err_t ret = vl6180_read_range(dev_handle, &distance, 100);
    if (ret == ESP_OK)
    {
        printf("Distance: %d mm\n", distance);
        *distance_mm = distance;
        return true;
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
    return false;
}

static bool parse_next_setpoint_from_txt(setpoint_sequence_state_t *state, float *setpoint_mm)
{
    const char *start = _binary_2_txt_start;
    const char *end = _binary_2_txt_end;
    const char *p = state->cursor;

    if (p < start || p >= end)
    {
        p = start;
    }

    while (1)
    {
        if (p >= end)
        {
            p = start;
            if (p >= end)
            {
                return false;
            }
        }

        int token_idx = 0;
        long third_value = 0;

        while (p < end && *p != '\n' && *p != '\r')
        {
            char *next = NULL;
            long value = strtol(p, &next, 10);
            if (next == p)
            {
                p++;
                continue;
            }

            token_idx++;
            if (token_idx == 3)
            {
                third_value = value;
            }
            p = next;
        }

        while (p < end && (*p == '\n' || *p == '\r'))
        {
            p++;
        }

        if (token_idx >= 3)
        {
            uint8_t height_mm = source_value_to_height(third_value);
            state->cursor = p;
            *setpoint_mm = (float)height_mm;
            return true;
        }
    }
}
void pid_task(i2c_master_dev_handle_t dev_handle)
{
    const TickType_t xInterval = pdMS_TO_TICKS(CONTROL_LOOP_MS);
    const float dt_s = CONTROL_LOOP_MS / 1000.0f;
    const TickType_t setpoint_step = pdMS_TO_TICKS(SETPOINT_STEP_MS);
    TickType_t xLastWakeTime = xTaskGetTickCount();
    TickType_t last_setpoint_step = xLastWakeTime;
    setpoint_sequence_state_t seq_state = {
        .cursor = _binary_2_txt_start,
    };
    float initial_setpoint = 0.0f;
    bool have_initial_setpoint = parse_next_setpoint_from_txt(&seq_state, &initial_setpoint);

    // PID output is centered around 0, then mapped to 0-100% fan command.
    pid_init(&fan_pid, 1.2f, 0.4f, 0.05f, PID_OUTPUT_MIN, PID_OUTPUT_MAX);
    fan_pid.setpoint = have_initial_setpoint ? initial_setpoint : 0.0f;
    printf("setpoint: %f\n", fan_pid.setpoint);

    while (1)
    {
        vTaskDelayUntil(&xLastWakeTime, xInterval);

        TickType_t now = xTaskGetTickCount();
        if ((now - last_setpoint_step) >= setpoint_step)
        {
            last_setpoint_step = now;
            float next_setpoint = 0.0f;
            if (parse_next_setpoint_from_txt(&seq_state, &next_setpoint))
            {
                fan_pid.setpoint = next_setpoint;
                printf("setpoint: %f\n", fan_pid.setpoint);
            }
        }

        uint8_t height_mm = 0;
        if (read_height_mm(dev_handle, &height_mm))
        {
            float output = pid_compute(&fan_pid, height_mm, dt_s);
            float fan_percent = map_range(output, PID_OUTPUT_MIN, PID_OUTPUT_MAX, FAN_MIN_PERCENT, FAN_MAX_PERCENT);
            set_fan_speed(fan_percent);
        }
    }
}

void app_main(void)
{
    /* PWM Timer Setup */
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_RESOLUTION,
        .timer_num = LEDC_TIMER,
        .freq_hz = FAN_PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t channel_conf = {
        .gpio_num = FAN_PWM_GPIO,
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

    pid_task(dev_handle);
}
