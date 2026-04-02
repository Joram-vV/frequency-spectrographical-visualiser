#include "volume_ui.h"

#include "now_playing_ui.h"
#include "playback_controls_ui.h"
#include "settings.h"

static lv_obj_t *volume_label;
static lv_obj_t *volume_slider;
static int32_t current_volume = 50;

enum {
    VOLUME_UI_MARGIN_X = 40,
    VOLUME_UI_BOTTOM_MARGIN = 28,
    VOLUME_UI_LABEL_GAP = 12,
};

void volume_ui_set_value(int32_t value)
{
    if (volume_slider == NULL || volume_label == NULL) {
        return;
    }

    if (value == current_volume) {
        return;
    }

    current_volume = value;

    lv_slider_set_value(volume_slider, value, LV_ANIM_OFF);
    lv_label_set_text_fmt(volume_label, "volume %d", (int)value);
}

void volume_ui_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_clear_flag(screen,
                      LV_OBJ_FLAG_CLICKABLE |
                          LV_OBJ_FLAG_SCROLLABLE |
                          LV_OBJ_FLAG_SCROLL_ELASTIC |
                          LV_OBJ_FLAG_SCROLL_MOMENTUM |
                          LV_OBJ_FLAG_SCROLL_CHAIN_HOR |
                          LV_OBJ_FLAG_SCROLL_CHAIN_VER |
                          LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_scroll_dir(screen, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    now_playing_ui_create(screen);
    playback_controls_ui_create(screen);

    volume_label = lv_label_create(screen);

    volume_slider = lv_slider_create(screen);
    lv_obj_set_width(volume_slider, LCD_H_RES - 80);
    lv_obj_set_height(volume_slider, 28);
    lv_slider_set_range(volume_slider, 0, 100);
    lv_obj_align(volume_slider, LV_ALIGN_BOTTOM_LEFT, VOLUME_UI_MARGIN_X, -VOLUME_UI_BOTTOM_MARGIN);

    lv_obj_align_to(volume_label, volume_slider, LV_ALIGN_OUT_TOP_LEFT, 0, -VOLUME_UI_LABEL_GAP);

    current_volume = -1;
    volume_ui_set_value(50);

    lv_scr_load(screen);
}
