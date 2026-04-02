#pragma once

#include <stdint.h>

#include "lvgl.h"
#include "espnow_protocol.h"

void now_playing_ui_create(lv_obj_t *parent);
void now_playing_ui_set_song_title(const char *title);
void playback_controls_ui_update_status(const tel_status_t *status);
void now_playing_ui_set_song_progress(int32_t elapsed_seconds, int32_t duration_seconds);
