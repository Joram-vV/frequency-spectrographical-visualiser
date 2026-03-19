#pragma once

#include "settings.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/lock.h>
#include <sys/param.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

void app_ui_start(void);
