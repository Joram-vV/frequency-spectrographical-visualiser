#include "lvgl_use.h"
#include "espnow_protocol.h"
#include "espnow_transport.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "now_playing_ui.h"
#include "playback_controls_ui.h"
#include "esp_sleep.h"

static const char *TAG = "lvgl";
static _lock_t lvgl_api_lock;
static esp_timer_handle_t lvgl_tick_timer;
static esp_lcd_panel_handle_t lcd_panel;

typedef struct {
	bool initialized;
	uint8_t address;
	bool pressed;
	lv_point_t point;
} gt911_state_t;

static gt911_state_t touch_state;

enum {
	GT911_PRODUCT_ID_REG = 0x8140,
	GT911_STATUS_REG = 0x814E,
	GT911_POINT1_REG = 0x814F,
};

static esp_err_t gt911_read_reg(uint8_t addr, uint16_t reg, uint8_t *data, size_t len)
{
	uint8_t reg_buf[2] = {reg >> 8, reg & 0xFF};
	return i2c_master_write_read_device(
		TOUCH_I2C_PORT,
		addr,
		reg_buf,
		sizeof(reg_buf),
		data,
		len,
		pdMS_TO_TICKS(50));
}

static esp_err_t gt911_write_reg8(uint8_t addr, uint16_t reg, uint8_t value)
{
	uint8_t write_buf[3] = {reg >> 8, reg & 0xFF, value};
	return i2c_master_write_to_device(
		TOUCH_I2C_PORT,
		addr,
		write_buf,
		sizeof(write_buf),
		pdMS_TO_TICKS(50));
}

static lv_point_t map_touch_point(int32_t raw_x, int32_t raw_y)
{
	lv_point_t point = {
		.x = raw_x,
		.y = raw_y,
	};

#if TOUCH_SWAP_XY
	int32_t tmp = point.x;
	point.x = point.y;
	point.y = tmp;
#endif

#if TOUCH_MIRROR_X
	point.x = (LCD_H_RES - 1) - point.x;
#endif

#if TOUCH_MIRROR_Y
	point.y = (LCD_V_RES - 1) - point.y;
#endif

	point.x = LV_CLAMP(0, point.x, LCD_H_RES - 1);
	point.y = LV_CLAMP(0, point.y, LCD_V_RES - 1);
	return point;
}

static void poll_touch_controller(void)
{
	if (!touch_state.initialized) {
		return;
	}

	uint8_t status = 0;
	esp_err_t err = gt911_read_reg(touch_state.address, GT911_STATUS_REG, &status, 1);
	if (err != ESP_OK) {
		return;
	}

	bool buffer_ready = (status & 0x80U) != 0;
	uint8_t touch_count = status & 0x0FU;
	bool pressed = buffer_ready && touch_count > 0;

	if (pressed) {
		uint8_t point_data[7];
		err = gt911_read_reg(touch_state.address, GT911_POINT1_REG, point_data, sizeof(point_data));
		if (err == ESP_OK) {
			int32_t raw_x = point_data[1] | (point_data[2] << 8);
			int32_t raw_y = point_data[3] | (point_data[4] << 8);
			touch_state.point = map_touch_point(raw_x, raw_y);
		}
	}

	touch_state.pressed = pressed;
	(void)gt911_write_reg8(touch_state.address, GT911_STATUS_REG, 0);
}

static esp_err_t init_touch_controller(void)
{
	i2c_config_t i2c_config = {
		.mode = I2C_MODE_MASTER,
		.sda_io_num = TOUCH_I2C_SDA,
		.scl_io_num = TOUCH_I2C_SCL,
		.sda_pullup_en = GPIO_PULLUP_ENABLE,
		.scl_pullup_en = GPIO_PULLUP_ENABLE,
		.master.clk_speed = TOUCH_I2C_FREQ_HZ,
	};

	ESP_ERROR_CHECK(i2c_param_config(TOUCH_I2C_PORT, &i2c_config));
	ESP_ERROR_CHECK(i2c_driver_install(TOUCH_I2C_PORT, i2c_config.mode, 0, 0, 0));

	gpio_config_t rst_gpio_config = {
		.mode = GPIO_MODE_OUTPUT,
		.pin_bit_mask = 1ULL << TOUCH_RST,
	};
	ESP_ERROR_CHECK(gpio_config(&rst_gpio_config));

	gpio_set_level(TOUCH_RST, 0);
	vTaskDelay(pdMS_TO_TICKS(10));
	gpio_set_level(TOUCH_RST, 1);
	vTaskDelay(pdMS_TO_TICKS(60));

	if (TOUCH_INT >= 0) {
		gpio_reset_pin(TOUCH_INT);
	}

	uint8_t product_id[5] = {0};
	esp_err_t err = gt911_read_reg(TOUCH_ADDR_PRIMARY, GT911_PRODUCT_ID_REG, product_id, 4);
	if (err == ESP_OK) {
		touch_state.address = TOUCH_ADDR_PRIMARY;
	} else {
		err = gt911_read_reg(TOUCH_ADDR_ALT, GT911_PRODUCT_ID_REG, product_id, 4);
		if (err == ESP_OK) {
			touch_state.address = TOUCH_ADDR_ALT;
		} else {
			return err;
		}
	}

	touch_state.initialized = true;
	ESP_LOGI(TAG, "GT911 ready on 0x%02X, product id %.4s", touch_state.address, product_id);
	return ESP_OK;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
	esp_lcd_panel_handle_t panel = drv->user_data;
	esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
	lv_disp_flush_ready(drv);
}

static void increase_lvgl_tick(void *arg)
{
	(void)arg;
	lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void sleep_until_wakeup_button(void)
{
	ESP_LOGI(TAG, "No touch for %d ms, entering light sleep", UI_SLEEP_TIMEOUT_MS);

	gpio_set_level(PIN_NUM_BK_LIGHT, LCD_BK_LIGHT_OFF);
	(void)esp_lcd_panel_disp_on_off(lcd_panel, false);

	esp_timer_stop(lvgl_tick_timer);

	vTaskDelay(pdMS_TO_TICKS(100));

	touch_state.pressed = false;

	// enter sleep

	(void)esp_light_sleep_start();

	// exit sleep

	ESP_ERROR_CHECK(esp_lcd_rgb_panel_restart(lcd_panel));
	(void)esp_lcd_panel_disp_on_off(lcd_panel, true);
	gpio_set_level(PIN_NUM_BK_LIGHT, LCD_BK_LIGHT_ON);
	ESP_LOGI(TAG, "Woke up on GPIO %d", WAKEUP_BUTTON_GPIO);

	esp_timer_restart(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000);

	static espnow_packet_t packet = {
		.type = MSG_TYPE_REQUEST
	};
	espnow_transport_send(&packet);
}

static void lvgl_port_task(void *arg)
{
	int64_t last_touch_activity_us = esp_timer_get_time();
	bool touch_pressed_filtered = false;
	uint8_t touch_release_stable_count = 0;

	(void)arg;

	while (1) {
		uint32_t delay_ms;
		bool middle_pressed_region;
		bool raw_pressed;

		poll_touch_controller();
		raw_pressed = touch_state.pressed;
		if (raw_pressed) {
			last_touch_activity_us = esp_timer_get_time();
		}

		if (raw_pressed) {
			touch_pressed_filtered = true;
			touch_release_stable_count = 0;
		} else if (touch_pressed_filtered) {
			touch_release_stable_count++;
			if (touch_release_stable_count >= 3) {
				touch_pressed_filtered = false;
				touch_release_stable_count = 0;
			}
		}

		middle_pressed_region = touch_pressed_filtered && touch_state.point.y < (LCD_V_RES - 100);

		_lock_acquire(&lvgl_api_lock);
		(void)playback_controls_ui_touch_update(&touch_state.point, middle_pressed_region);
		delay_ms = lv_timer_handler();
		_lock_release(&lvgl_api_lock);

		delay_ms = MAX(delay_ms, LVGL_TASK_MIN_DELAY);
		delay_ms = MIN(delay_ms, LVGL_TASK_MAX_DELAY);

		if ((esp_timer_get_time() - last_touch_activity_us) >= ((int64_t)UI_SLEEP_TIMEOUT_MS * 1000)) {
			sleep_until_wakeup_button();
			last_touch_activity_us = esp_timer_get_time();
			touch_pressed_filtered = false;
			touch_release_stable_count = 0;
		}

		vTaskDelay(pdMS_TO_TICKS(delay_ms));
	}
}

static void ui_init_task(void *arg)
{
	(void)arg;
	lv_obj_t *screen;

	_lock_acquire(&lvgl_api_lock);
	screen = lv_obj_create(NULL);
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
	lv_scr_load(screen);
	_lock_release(&lvgl_api_lock);

	gpio_set_level(PIN_NUM_BK_LIGHT, LCD_BK_LIGHT_ON);
	ESP_LOGI(TAG, "LVGL screen ready");
	vTaskDelete(NULL);
}

static void init_backlight(void)
{
	gpio_config_t bk_gpio_config = {
		.mode = GPIO_MODE_OUTPUT,
		.pin_bit_mask = 1ULL << PIN_NUM_BK_LIGHT,
	};

	ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
	gpio_set_level(PIN_NUM_BK_LIGHT, LCD_BK_LIGHT_OFF);
}

static void init_display(esp_lcd_panel_handle_t panel)
{
	static lv_disp_draw_buf_t draw_buf;
	static lv_disp_drv_t disp_drv;

	size_t draw_buffer_pixels = LCD_H_RES * LVGL_DRAW_BUF_LINES;
	lv_color_t *buf1 = heap_caps_malloc(
		draw_buffer_pixels * sizeof(lv_color_t),
		MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

	assert(buf1);

	lv_disp_draw_buf_init(&draw_buf, buf1, NULL, draw_buffer_pixels);

	lv_disp_drv_init(&disp_drv);
	disp_drv.hor_res = LCD_H_RES;
	disp_drv.ver_res = LCD_V_RES;
	disp_drv.flush_cb = lvgl_flush_cb;
	disp_drv.draw_buf = &draw_buf;
	disp_drv.user_data = panel;
	lv_disp_drv_register(&disp_drv);
}

static esp_lcd_panel_handle_t init_rgb_panel(void)
{
	esp_lcd_panel_handle_t panel = NULL;
	esp_lcd_rgb_panel_config_t panel_config = {
		.data_width = LCD_DATA_WIDTH,
		.dma_burst_size = 64,
		.num_fbs = 1,
		.clk_src = LCD_CLK_SRC_DEFAULT,
		.disp_gpio_num = -1,
		.pclk_gpio_num = PIN_NUM_PCLK,
		.vsync_gpio_num = PIN_NUM_VSYNC,
		.hsync_gpio_num = PIN_NUM_HSYNC,
		.de_gpio_num = PIN_NUM_DE,
		.data_gpio_nums = {
			PIN_NUM_DATA0,
			PIN_NUM_DATA1,
			PIN_NUM_DATA2,
			PIN_NUM_DATA3,
			PIN_NUM_DATA4,
			PIN_NUM_DATA5,
			PIN_NUM_DATA6,
			PIN_NUM_DATA7,
			PIN_NUM_DATA8,
			PIN_NUM_DATA9,
			PIN_NUM_DATA10,
			PIN_NUM_DATA11,
			PIN_NUM_DATA12,
			PIN_NUM_DATA13,
			PIN_NUM_DATA14,
			PIN_NUM_DATA15,
		},
		.timings = {
			.pclk_hz = LCD_PIXEL_CLOCK_HZ,
			.h_res = LCD_H_RES,
			.v_res = LCD_V_RES,
			.hsync_back_porch = LCD_HBP,
			.hsync_front_porch = LCD_HFP,
			.hsync_pulse_width = LCD_HSYNC,
			.vsync_back_porch = LCD_VBP,
			.vsync_front_porch = LCD_VFP,
			.vsync_pulse_width = LCD_VSYNC,
			.flags.pclk_active_neg = true,
		},
		.flags.fb_in_psram = true,
	};

	ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel));
	ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
	ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

	return panel;
}

void app_ui_start(void)
{
	const esp_timer_create_args_t lvgl_tick_timer_args = {
		.callback = increase_lvgl_tick,
		.name = "lvgl_tick",
	};

	ESP_LOGI(TAG, "Initializing LCD backlight");
	init_backlight();

	ESP_LOGI(TAG, "Initializing LCD panel");
	lcd_panel = init_rgb_panel();

	ESP_LOGI(TAG, "Initializing GT911 touch");
	ESP_ERROR_CHECK(init_touch_controller());

	ESP_LOGI(TAG, "Initializing LVGL");
	lv_init();
	init_display(lcd_panel);
	ESP_ERROR_CHECK(esp_lcd_rgb_panel_restart(lcd_panel));

	ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

	xTaskCreate(lvgl_port_task, "lvgl", LVGL_TASK_STACK, NULL, LVGL_TASK_PRIORITY, NULL);
	xTaskCreate(ui_init_task, "ui_init", 4096, NULL, LVGL_TASK_PRIORITY, NULL);
}
