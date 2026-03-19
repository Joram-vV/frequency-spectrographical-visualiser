#include <stdio.h>

#include "lvgl_use.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting display UI");
    app_ui_start();
}
