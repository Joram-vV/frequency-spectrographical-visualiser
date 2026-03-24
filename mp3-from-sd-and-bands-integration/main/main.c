#include "esp_log.h"
#include "esp_log_level.h"
#include "esp_peripherals.h"
#include "board.h"
#include "periph_sdcard.h"
#include "sdcard_scan.h"
#include "audio_event_iface.h"

#include "sdcard_list.h"
#include "filter_resample.h"

#include "console.h"
#include "visualizer.h"
#include "audio_control.h"

static const char *TAG = "[MAIN]";
static playlist_operator_handle_t sdcard_list_handle = NULL;

static void sdcard_url_save_callback(void *user_data, char *url) {
    playlist_operator_handle_t sdcard_handle = (playlist_operator_handle_t)user_data;
    sdcard_list_save(sdcard_handle, url);
}

void app_main(void) {
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);
    esp_log_level_set("ADC_BTN", ESP_LOG_ERROR);
    esp_log_level_set("AUDIO_EVT", ESP_LOG_ERROR);
    esp_log_level_set("PLAYLIST_SDCARD", ESP_LOG_INFO);

    ESP_LOGI(TAG, "Initializing System...");

    // peripherals & SD card
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
    audio_board_key_init(set);
    audio_board_sdcard_init(set, SD_MODE_SPI);

    sdcard_list_create(&sdcard_list_handle);
    sdcard_scan(sdcard_url_save_callback, "/sdcard", 0, (const char *[]) {"mp3"}, 1, sdcard_list_handle);

    // 2. Audio Hardware
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);

    // 3. Modules Initialization
    visualizer_init();
    audio_control_init();
    console_init(board_handle, sdcard_list_handle);
    // cli_control_init(board_handle, sdcard_list_handle);

    xTaskCreate(visualizer_task, "visualizer_task", 4096, NULL, 5, NULL);

    // 4. Event Listener setup
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(audio_control_get_pipeline(), evt);

    ESP_LOGI(TAG, "System Ready. Waiting for commands.");

    xTaskCreate(console_task, "console_task", 4096, NULL, 4, NULL);

    // 5. Main Event Loop
    while (1) {
        audio_event_iface_msg_t msg;
        if (audio_event_iface_listen(evt, &msg, portMAX_DELAY) != ESP_OK) continue;

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT) {
            if (msg.source == (void *) audio_control_get_mp3_decoder() && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
                audio_element_info_t music_info = {0};
                audio_element_getinfo(audio_control_get_mp3_decoder(), &music_info);
                audio_element_setinfo(audio_control_get_i2s_writer(), &music_info);
                rsp_filter_set_src_info(audio_control_get_rsp_handle(), music_info.sample_rates, music_info.channels);
                continue;
            }

            if (msg.source == (void *) audio_control_get_i2s_writer() && msg.cmd == AEL_MSG_CMD_REPORT_STATUS) {
                if (audio_element_get_state(audio_control_get_i2s_writer()) == AEL_STATE_FINISHED) {
                    printf("\n[ INFO ] Track Finished. Waiting for command...\n");
                    audio_control_stop();
                }
            }
        }
    }
}