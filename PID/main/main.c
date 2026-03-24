#include <stdio.h>
#include <stdlib.h>
#include "driver/ledc.h"
#include "driver/adc.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pid_controller.h"




#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include <stdint.h>

#define FAN_PWM_CHANNEL LEDC_CHANNEL_0      // Use PWM channel 0
#define FAN_PWM_FREQ    25000               // 25kHz PWM frequency for 4-wire fans
#define FAN_PWM_RES     LEDC_TIMER_10_BIT   // 10-bit resolution (0-1023)
#define FAN_GPIO        2                   // GPIO pin for fan control


PIDController fan_pid;  // PID controller instance


#define LED_PIN GPIO_NUM_2
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_FREQ_HZ FAN_PWM_FREQ
#define LEDC_RESOLUTION FAN_PWM_RES

// #define LED_PIN GPIO_NUM_2
// #define LEDC_CHANNEL LEDC_CHANNEL_0
// #define LEDC_TIMER LEDC_TIMER_0
// #define LEDC_MODE LEDC_LOW_SPEED_MODE
// #define FAN_PWM_FREQ 5000
// #define LEDC_RESOLUTION LEDC_TIMER_13_BIT


// ultrasoon defines
const int trigPin = 35;
const int echoPin = 36;

//define sound speed in cm/uS
#define SOUND_SPEED 0.034

long duration;
float distanceCm;


extern const char _binary_2_txt_start[] asm("_binary_2_txt_start");
extern const char _binary_2_txt_end[] asm("_binary_2_txt_end");

static uint32_t speed_to_duty(float speed_percent)
{
    if (speed_percent < 0.0f) {
        speed_percent = 0.0f;
    }
    if (speed_percent > 100.0f) {
        speed_percent = 100.0f;
    }

    uint32_t max_duty = (1 << FAN_PWM_RES) - 1;
    return (uint32_t)((max_duty * speed_percent) / 100.0f);
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
    while (gpio_get_level(pin) == level) {
        if (esp_timer_get_time() - start > timeout_us) return -1;
    }

    // Wait for pulse to start
    while (gpio_get_level(pin) != level) {
        if (esp_timer_get_time() - start > timeout_us) return -1;
    }

    int64_t pulse_start = esp_timer_get_time();

    // Wait for pulse to end
    while (gpio_get_level(pin) == level) {
        if (esp_timer_get_time() - start > timeout_us) return -1;
    }

    return esp_timer_get_time() - pulse_start; // pulse width in us
}


float read_hightS() {

    gpio_set_level(trigPin, 0);
    esp_rom_delay_us(2);
    gpio_set_level(trigPin, 1);
    esp_rom_delay_us(10);
    gpio_set_level(trigPin, 0); // Reads the echoPin, returns the sound wave travel time in microseconds
    duration = pulse_in_us(echoPin, 1, 30000); // 30 ms timeout
    if (duration < 0) {
        printf("distance read timeout\n");
        return -1.0f;
    }
    distanceCm = duration * SOUND_SPEED/2;
    printf("distance: %f CM\n", distanceCm);


    return  distanceCm;
}


void pid_task(void* arg) {
    const TickType_t xInterval = pdMS_TO_TICKS(20); // 20 ms control loop
    const float dt_s = 0.02f;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    // Initialize PID with Kp=2.0, Ki=0.5, Kd=1.0
    // Output limits: 0-100% (fan speed range)
    pid_init(&fan_pid, 8.0, 0.0, 0.0, 0.0, 100.0);
    fan_pid.setpoint = 8.0f; 

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, xInterval);  // Precise timing

        float hight = read_hightS();
        if (hight < 0.0f || hight > 400.0f) {
            continue;
        }
        // Compute PID output with 20ms time delta (matches control loop)
        float output = pid_compute(&fan_pid, hight, dt_s);
        printf("output: %f procent \n", output);
        uint32_t duty = speed_to_duty(output);
        printf("duty = %lu\n", (unsigned long)duty);
        set_fan_speed(output);  // Apply control signal
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

    gpio_reset_pin(trigPin);
    gpio_set_direction(trigPin, GPIO_MODE_OUTPUT);
    gpio_reset_pin(echoPin);
    gpio_set_direction(echoPin, GPIO_MODE_INPUT);
     xTaskCreate(pid_task,       // Task function
                "pid_task",     // Task name
                4096,          // Stack size (bytes)
                NULL,           // Parameters
                5,             // Priority (higher = more urgent)
                NULL);         // Task handle (not used)

    // while (1)
    // {
       


    //     // play_brightness_sequence_from_txt();
    // }
}
