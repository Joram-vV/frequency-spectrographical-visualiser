#pragma once

#include <stdbool.h>
#include "espnow_protocol.h" // Needed for tel_playlist_t
#include "lvgl.h"

void playback_controls_ui_create(lv_obj_t *parent);
void playback_controls_ui_update_status(const tel_status_t *status);
void playback_controls_ui_add_playlist_chunk(const tel_playlist_t *pl);
bool playback_controls_ui_handle_touch(const lv_point_t *point);
bool playback_controls_ui_touch_update(const lv_point_t *point, bool pressed);

