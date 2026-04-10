#include "playback_controls_ui.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "espnow_protocol.h"
#include "espnow_transport.h"
#include "now_playing_ui.h"
#include "settings.h"

enum {
    CONTROLS_MARGIN_X = 40,
    CONTROLS_TOP_Y = 110,
    CONTROLS_BOTTOM_Y = 390,
    SONG_LIST_WIDTH = 470,
    SONG_ITEM_GAP = 6,
    SONG_ITEM_HEIGHT = 40,
    BUTTON_PANEL_WIDTH = 190,
    BUTTON_HEIGHT = 46,
    BUTTON_GAP = 12,
    CONTROL_BUTTON_COUNT = 4,
    TOUCH_MOVE_THRESHOLD = 8,
};

typedef enum {
    CONTROL_PLAY_SELECTED = 0,
    CONTROL_PLAY_PAUSE,
    CONTROL_PREVIOUS,
    CONTROL_NEXT,
} control_button_id_t;

#define MAX_REMOTE_SONGS 100 // Safe upper limit for UI tracking
static char remote_songs[MAX_REMOTE_SONGS][MAX_SONG_NAME_LEN];
static lv_obj_t *song_buttons[MAX_REMOTE_SONGS];
static lv_obj_t *song_labels[MAX_REMOTE_SONGS];
static int actual_song_count = 0; // Tracks the real total

static lv_obj_t *control_buttons[CONTROL_BUTTON_COUNT];
static lv_obj_t *songs_list_obj;
static int32_t selected_song_index;
static int32_t current_song_index;
static bool is_playing;
static bool middle_touch_active;
static bool middle_touch_moved;
static bool middle_touch_on_songs;
static int32_t middle_touch_song_index;
static int32_t middle_touch_control_index;
static lv_point_t middle_touch_start;
static lv_point_t middle_touch_last;
static char now_playing_title[128];

void set_playing_song(int32_t index, bool playing)
{
	if (actual_song_count == 0 || index < 0 || index >= actual_song_count) {
		return;
	}

	is_playing = playing;

	if (playing) {
		now_playing_ui_set_song_title(remote_songs[index]);
		return;
	}

	snprintf(now_playing_title, sizeof(now_playing_title), "%s - paused", remote_songs[index]);
	now_playing_ui_set_song_title(now_playing_title);
}

static void reset_touch_tracking(void)
{
    middle_touch_active = false;
    middle_touch_moved = false;
    middle_touch_on_songs = false;
    middle_touch_song_index = -1;
    middle_touch_control_index = -1;
}

static bool point_inside_obj(const lv_point_t *point, lv_obj_t *obj)
{
    lv_area_t area;

    if (obj == NULL) {
        return false;
    }

    lv_obj_get_coords(obj, &area);
    return point->x >= area.x1 && point->x <= area.x2 &&
           point->y >= area.y1 && point->y <= area.y2;
}

static int32_t find_touched_song(const lv_point_t *point)
{
    for (int32_t i = 0; i < actual_song_count; i++) {
        if (point_inside_obj(point, song_buttons[i])) {
            return i;
        }
    }
    return -1;
}

static int32_t find_touched_control(const lv_point_t *point)
{
    for (uint32_t i = 0; i < CONTROL_BUTTON_COUNT; i++) {
        if (point_inside_obj(point, control_buttons[i])) {
            return i;
        }
    }
    return -1;
}

static void update_song_button_style(uint32_t index)
{
    if (song_buttons[index] == NULL || song_labels[index] == NULL) {
        return;
    }

    bool selected = (int32_t)index == selected_song_index;

    lv_obj_set_style_bg_color(song_buttons[index],
                              selected ? lv_palette_main(LV_PALETTE_BLUE) : lv_color_hex(0x2A2A2A),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(song_buttons[index], LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(song_labels[index], lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void set_selected_song(int32_t index)
{
    if (actual_song_count == 0 || index < 0 || index >= actual_song_count) {
        return;
    }

    selected_song_index = index;
    for (uint32_t i = 0; i < actual_song_count; i++) {
        update_song_button_style(i);
    }
}

static void apply_current_song(int32_t index)
{
    if (actual_song_count == 0 || index < 0 || index >= actual_song_count) {
        return;
    }

    current_song_index = index;
    set_selected_song(index);
    now_playing_ui_set_song_title(remote_songs[index]);
    now_playing_ui_set_song_progress(0, 0); // Reset progress visually
}

static void update_now_playing_title(void)
{
    if (is_playing) {
        now_playing_ui_set_song_title(remote_songs[current_song_index]);
        return;
    }

    snprintf(now_playing_title, sizeof(now_playing_title), "%s - paused", remote_songs[current_song_index]);
    now_playing_ui_set_song_title(now_playing_title);
}

static void snap_song_list_to_bounds(void)
{
    if (songs_list_obj == NULL) {
        return;
    }

    int32_t scroll_y = lv_obj_get_scroll_y(songs_list_obj);
    int32_t max_scroll_y = lv_obj_get_scroll_top(songs_list_obj) + lv_obj_get_scroll_bottom(songs_list_obj);
    if (max_scroll_y < 0) {
        max_scroll_y = 0;
    }

    int32_t target_y = LV_CLAMP(0, scroll_y, max_scroll_y);
    if (target_y != scroll_y) {
        lv_obj_scroll_to_y(songs_list_obj, target_y, LV_ANIM_ON);
    }
}

static void create_song_item(lv_obj_t *parent, uint32_t index, const char* title)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_t *label = lv_label_create(button);

    lv_obj_set_width(button, lv_pct(100));
    lv_obj_set_height(button, SONG_ITEM_HEIGHT);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_label_set_text(label, title);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 12, 0);

    song_buttons[index] = button;
    song_labels[index] = label;
}

static void create_control_button(lv_obj_t *parent, uint32_t index, const char *title)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_t *label = lv_label_create(button);

    lv_obj_set_width(button, lv_pct(100));
    lv_obj_set_height(button, BUTTON_HEIGHT);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_color(button, lv_color_hex(0x303030), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_label_set_text(label, title);
    lv_obj_center(label);

    control_buttons[index] = button;
}

// Runs on the LVGL thread via lv_async_call().
static void async_playlist_update_cb(void *arg)
{
    tel_playlist_t *pl = (tel_playlist_t *)arg;

    for (int i = 0; i < pl->count; i++) {
        int idx = pl->start_index + i;
        if (idx >= MAX_REMOTE_SONGS) {
            break;
        }

        strncpy(remote_songs[idx], pl->songs[i], MAX_SONG_NAME_LEN);

        if (song_buttons[idx] == NULL) {
            create_song_item(songs_list_obj, idx, remote_songs[idx]);
        } else {
            lv_label_set_text(song_labels[idx], remote_songs[idx]);
        }
    }
    actual_song_count = pl->total_songs;
    set_selected_song(selected_song_index);

    free(pl);
}

void playback_controls_ui_add_playlist_chunk(const tel_playlist_t *pl)
{
    // Network packets are reused; copy before dispatching to LVGL thread.
    tel_playlist_t *pl_copy = malloc(sizeof(tel_playlist_t));
    if (pl_copy) {
        memcpy(pl_copy, pl, sizeof(tel_playlist_t));
        lv_async_call(async_playlist_update_cb, pl_copy);
    }
}

static void send_command(command_id_t id, int32_t value)
{
    espnow_packet_t packet = {
        .type = MSG_TYPE_COMMAND,
        .payload.command.id = id,
        .payload.command.value = value,
    };
    espnow_transport_send(&packet);
}

void playback_controls_ui_create(lv_obj_t *parent)
{
    lv_obj_t *songs_panel = lv_obj_create(parent);
    lv_obj_t *songs_title = lv_label_create(songs_panel);
    lv_obj_t *buttons_panel = lv_obj_create(parent);
    lv_obj_t *buttons_column = lv_obj_create(buttons_panel);

    lv_obj_set_size(songs_panel, SONG_LIST_WIDTH, CONTROLS_BOTTOM_Y - CONTROLS_TOP_Y);
    lv_obj_align(songs_panel, LV_ALIGN_TOP_LEFT, CONTROLS_MARGIN_X, CONTROLS_TOP_Y);
    lv_obj_set_style_pad_all(songs_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(songs_title, "songs");
    lv_obj_align(songs_title, LV_ALIGN_TOP_LEFT, 0, 0);

    // Song buttons are appended dynamically from incoming playlist chunks.
    lv_obj_add_flag(songs_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(songs_panel, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_flex_flow(songs_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(songs_panel, SONG_ITEM_GAP, LV_PART_MAIN | LV_STATE_DEFAULT);

    songs_list_obj = songs_panel;

    lv_obj_set_size(buttons_panel, BUTTON_PANEL_WIDTH, CONTROLS_BOTTOM_Y - CONTROLS_TOP_Y);
    lv_obj_align_to(buttons_panel, songs_panel, LV_ALIGN_OUT_RIGHT_TOP, 20, 0);
    lv_obj_set_style_pad_all(buttons_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_width(buttons_column, lv_pct(100));
    lv_obj_set_height(buttons_column, LV_SIZE_CONTENT);
    lv_obj_align(buttons_column, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_pad_all(buttons_column, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(buttons_column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(buttons_column, BUTTON_GAP, LV_PART_MAIN | LV_STATE_DEFAULT);

    create_control_button(buttons_column, CONTROL_PLAY_SELECTED, "Play Selected");
    create_control_button(buttons_column, CONTROL_PLAY_PAUSE, "Play / Pause");
    create_control_button(buttons_column, CONTROL_PREVIOUS, "Previous");
    create_control_button(buttons_column, CONTROL_NEXT, "Next");

    selected_song_index = 0;
    current_song_index = 0;
    is_playing = false;
    reset_touch_tracking();
    set_selected_song(selected_song_index);
}

static bool handle_tap_action(const lv_point_t *point)
{
    int32_t song_index = find_touched_song(point);
    if (song_index >= 0) {
        set_selected_song(song_index);
        return true;
    }

    int32_t control_index = find_touched_control(point);
    if (control_index < 0) {
        return false;
    }

    if (control_index == CONTROL_PLAY_SELECTED) {
        is_playing = true;
        apply_current_song(selected_song_index);
        send_command(CMD_PLAY_INDEX, selected_song_index);
        return true;
    }

    if (control_index == CONTROL_PLAY_PAUSE) {
        is_playing = !is_playing;
        update_now_playing_title();
        send_command(CMD_PLAY_PAUSE, 0);
        return true;
    }

    if (control_index == CONTROL_PREVIOUS) {
        current_song_index = (current_song_index + actual_song_count - 1) % actual_song_count;
        is_playing = true;
        apply_current_song(current_song_index);
        send_command(CMD_PREVIOUS, 0);
        return true;
    }

    if (control_index == CONTROL_NEXT) {
        current_song_index = (current_song_index + 1) % actual_song_count;
        is_playing = true;
        apply_current_song(current_song_index);
        send_command(CMD_NEXT, 0);
        return true;
    }

    return false;
}

bool playback_controls_ui_touch_update(const lv_point_t *point, bool pressed)
{
    if (!pressed) {
        if (middle_touch_on_songs && middle_touch_moved) {
            // Allow temporary overdrag, then snap back when finger is released.
            snap_song_list_to_bounds();
        }

        if (middle_touch_active && !middle_touch_moved) {
            if (middle_touch_on_songs && middle_touch_song_index >= 0) {
                set_selected_song(middle_touch_song_index);
            } else if (middle_touch_control_index >= 0) {
                (void)handle_tap_action(point);
            }
        }

        reset_touch_tracking();
        return false;
    }

    if (!middle_touch_active) {
        middle_touch_active = true;
        middle_touch_moved = false;
        middle_touch_start = *point;
        middle_touch_last = *point;
        middle_touch_song_index = find_touched_song(point);
        middle_touch_control_index = find_touched_control(point);
        middle_touch_on_songs = point_inside_obj(point, songs_list_obj);
        return middle_touch_on_songs || middle_touch_song_index >= 0 || middle_touch_control_index >= 0;
    }

    if (abs(point->x - middle_touch_start.x) > TOUCH_MOVE_THRESHOLD ||
        abs(point->y - middle_touch_start.y) > TOUCH_MOVE_THRESHOLD) {
        middle_touch_moved = true;
    }

    if (middle_touch_on_songs && middle_touch_moved && songs_list_obj != NULL) {
        // Direction intentionally inverted to match current UX preference.
        int32_t delta_y = point->y - middle_touch_last.y;

        // Apply resistance while overscrolling to create an elastic feel.
        if ((lv_obj_get_scroll_top(songs_list_obj) <= 0 && delta_y < 0) ||
            (lv_obj_get_scroll_bottom(songs_list_obj) <= 0 && delta_y > 0)) {
            delta_y /= 3;
        }
        if (delta_y != 0) {
            lv_obj_scroll_by(songs_list_obj, 0, delta_y, LV_ANIM_OFF);
        }
    }

    middle_touch_last = *point;
    return middle_touch_on_songs || middle_touch_song_index >= 0 || middle_touch_control_index >= 0;
}
