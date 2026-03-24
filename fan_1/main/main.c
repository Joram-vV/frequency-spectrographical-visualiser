#include <stdio.h>
#include <stdlib.h>
#include "driver/ledc.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LED_PIN GPIO_NUM_2
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_FREQ_HZ 5000
#define LEDC_RESOLUTION LEDC_TIMER_13_BIT

extern const char _binary_2_txt_start[] asm("_binary_2_txt_start");
extern const char _binary_2_txt_end[] asm("_binary_2_txt_end");

static uint32_t brightness_to_duty(uint8_t brightness_pct)
{
    uint32_t max_duty = (1 << LEDC_RESOLUTION) - 1;
    uint32_t duty = (max_duty * brightness_pct) / 100;
    return max_duty - duty; // Inverted for P-channel
}

static uint8_t source_value_to_brightness(long source_value)
{
    static const uint8_t brightness_map[] = {
        0,  // 0
        20, // 1
        30, // 2
        35, // 3
        40, // 4
        50, // 5
        55, // 6
        60, // 7
        70, // 8
    };

    if (source_value < 0 || source_value >= (long)(sizeof(brightness_map) / sizeof(brightness_map[0])))
    {
        return 0;
    }

    return brightness_map[source_value];
}

static void play_brightness_sequence_from_txt(void)
{
    const char *start = _binary_2_txt_start;
    const char *end = _binary_2_txt_end;

    const char *p = start;
    while (p < end)
    {
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
            uint8_t brightness = source_value_to_brightness(third_value);

            ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, brightness_to_duty(brightness)));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
            printf("Brightness: %u%% (source=%ld)\n", brightness, third_value);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

void app_main(void)
{
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
        .duty = brightness_to_duty(100), // Start fully bright
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_conf));

    while (1)
    {
        play_brightness_sequence_from_txt();
    }
}
