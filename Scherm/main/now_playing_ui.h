#pragma once

#include <stdint.h>

#include "lvgl.h"

void now_playing_ui_create(lv_obj_t *parent);
void now_playing_ui_set_song_title(const char *title);
void now_playing_ui_set_song_progress(int32_t elapsed_seconds, int32_t duration_seconds);
