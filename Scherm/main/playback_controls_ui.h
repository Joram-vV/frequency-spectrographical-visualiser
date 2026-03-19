#pragma once

#include <stdbool.h>

#include "lvgl.h"

void playback_controls_ui_create(lv_obj_t *parent);
bool playback_controls_ui_handle_touch(const lv_point_t *point);
bool playback_controls_ui_touch_update(const lv_point_t *point, bool pressed);
