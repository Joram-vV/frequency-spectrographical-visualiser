#pragma once

#include "driver/i2c.h"
#include "driver/gpio.h"

#define LCD_H_RES           800
#define LCD_V_RES           480
#define LCD_PIXEL_CLOCK_HZ  (16 * 1000 * 1000)
#define LCD_HSYNC           4
#define LCD_HBP             8
#define LCD_HFP             8
#define LCD_VSYNC           4
#define LCD_VBP             8
#define LCD_VFP             8
#define LCD_DATA_WIDTH      16

#define PIN_NUM_BK_LIGHT    2
#define PIN_NUM_HSYNC       39
#define PIN_NUM_VSYNC       41
#define PIN_NUM_DE          40
#define PIN_NUM_PCLK        42

#define PIN_NUM_DATA0       8
#define PIN_NUM_DATA1       3
#define PIN_NUM_DATA2       46
#define PIN_NUM_DATA3       9
#define PIN_NUM_DATA4       1
#define PIN_NUM_DATA5       5
#define PIN_NUM_DATA6       6
#define PIN_NUM_DATA7       7
#define PIN_NUM_DATA8       15
#define PIN_NUM_DATA9       16
#define PIN_NUM_DATA10      4
#define PIN_NUM_DATA11      45
#define PIN_NUM_DATA12      48
#define PIN_NUM_DATA13      47
#define PIN_NUM_DATA14      21
#define PIN_NUM_DATA15      14

#define LCD_BK_LIGHT_ON     1
#define LCD_BK_LIGHT_OFF    0

#define TOUCH_I2C_PORT      I2C_NUM_0
#define TOUCH_I2C_SDA       19
#define TOUCH_I2C_SCL       20
#define TOUCH_I2C_FREQ_HZ   400000
#define TOUCH_RST           38
#define TOUCH_INT           18
#define TOUCH_ADDR_PRIMARY  0x5D
#define TOUCH_ADDR_ALT      0x14
#define TOUCH_SWAP_XY       0
#define TOUCH_MIRROR_X      0
#define TOUCH_MIRROR_Y      0

#define LVGL_TICK_PERIOD_MS 2
#define LVGL_DRAW_BUF_LINES 40
#define LVGL_TASK_STACK     8192
#define LVGL_TASK_PRIORITY  4
#define LVGL_TASK_MIN_DELAY 1
#define LVGL_TASK_MAX_DELAY 16

#define UI_SLEEP_TIMEOUT_MS 40000
#define WAKEUP_BUTTON_GPIO  GPIO_NUM_17
