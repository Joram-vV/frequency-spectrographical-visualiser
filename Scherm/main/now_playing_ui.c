#include "now_playing_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "espnow_protocol.h"
#include "playback_controls_ui.h"
#include "settings.h"

static lv_obj_t *song_title_label;
static lv_obj_t *song_progress_bar;
static lv_obj_t *song_time_label;

enum {
	NOW_PLAYING_MARGIN_X = 40,
	NOW_PLAYING_TOP_MARGIN = 20,
	NOW_PLAYING_PROGRESS_Y = 54,
	NOW_PLAYING_PROGRESS_HEIGHT = 12,
	NOW_PLAYING_TIME_GAP = 10,
};

static void format_song_time(char *buffer, size_t buffer_size, int32_t elapsed_seconds, int32_t duration_seconds)
{
	int32_t elapsed_minutes = elapsed_seconds / 60;
	int32_t elapsed_remainder = elapsed_seconds % 60;
	int32_t duration_minutes = duration_seconds / 60;
	int32_t duration_remainder = duration_seconds % 60;

	snprintf(buffer,
			buffer_size,
			"%d:%02d / %d:%02d",
			(int)elapsed_minutes,
			(int)elapsed_remainder,
			(int)duration_minutes,
			(int)duration_remainder);
}

void now_playing_ui_set_song_title(const char *title)
{
	if (song_title_label == NULL) {
		return;
	}

	lv_label_set_text(song_title_label, title);
}

void now_playing_ui_set_song_progress(int32_t elapsed_seconds, int32_t duration_seconds)
{
	char time_text[32];

	if (song_progress_bar == NULL || song_time_label == NULL) {
		return;
	}

	if (duration_seconds <= 0) {
		duration_seconds = 1;
	}

	elapsed_seconds = LV_CLAMP(0, elapsed_seconds, duration_seconds);

	lv_bar_set_range(song_progress_bar, 0, duration_seconds);
	lv_bar_set_value(song_progress_bar, elapsed_seconds, LV_ANIM_OFF);

	format_song_time(time_text, sizeof(time_text), elapsed_seconds, duration_seconds);
	lv_label_set_text(song_time_label, time_text);
}

// Runs on the LVGL thread via lv_async_call().
static void async_status_update_cb(void *arg)
{
	tel_status_t *status = (tel_status_t *)arg;

	if (status->state == TEL_STATE_STOPPED) {
		now_playing_ui_set_song_title("Player stopped");
		now_playing_ui_set_song_progress(0, 0);
		free(status);
		return;
	}

	if (status->state == TEL_STATE_PAUSED) set_playing_song(status->current_song_index, false);
	else set_playing_song(status->current_song_index, true);

	now_playing_ui_set_song_progress(status->elapsed_seconds, status->duration_seconds);
	free(status);
}

void now_playing_ui_update_status(const tel_status_t *status)
{
	// Network packets are reused, so copy payload before crossing thread boundary.
	tel_status_t *status_copy = malloc(sizeof(tel_status_t));
	if (status_copy) {
		memcpy(status_copy, status, sizeof(tel_status_t));
		lv_async_call(async_status_update_cb, status_copy);
	}
}

void now_playing_ui_create(lv_obj_t *parent)
{
	song_title_label = lv_label_create(parent);
	lv_obj_set_width(song_title_label, LCD_H_RES - (NOW_PLAYING_MARGIN_X * 2));
	lv_label_set_long_mode(song_title_label, LV_LABEL_LONG_DOT);
	lv_obj_align(song_title_label, LV_ALIGN_TOP_LEFT, NOW_PLAYING_MARGIN_X, NOW_PLAYING_TOP_MARGIN);

	song_progress_bar = lv_bar_create(parent);
	lv_obj_set_width(song_progress_bar, LCD_H_RES - (NOW_PLAYING_MARGIN_X * 2));
	lv_obj_set_height(song_progress_bar, NOW_PLAYING_PROGRESS_HEIGHT);
	lv_obj_align(song_progress_bar, LV_ALIGN_TOP_LEFT, NOW_PLAYING_MARGIN_X, NOW_PLAYING_PROGRESS_Y);

	song_time_label = lv_label_create(parent);
	lv_obj_align_to(song_time_label, song_progress_bar, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, NOW_PLAYING_TIME_GAP);

	now_playing_ui_set_song_title("Player stopped");
	now_playing_ui_set_song_progress(0, 0);
}
