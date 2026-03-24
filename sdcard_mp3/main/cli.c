#include "cli.h"

#include <string.h>
#include "audio_element.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

audio_pipeline_handle_t player_pipeline;
audio_element_handle_t player_i2s_stream_writer, player_fatfs_stream_reader;
playlist_operator_handle_t player_sdcard_list_handle;

void console_task(void* param) {
    char cmd[32];

    while (1) {
        printf("\nEnter command (play, pause, next, vol+, vol-):\n> ");
        fflush(stdout);

        if (fgets(cmd, sizeof(cmd), stdin) != NULL) {

            // Remove newline
            cmd[strcspn(cmd, "\r\n")] = 0;

            if (strcmp(cmd, "play") == 0) {
                printf("▶ Play\n");
                if (audio_element_get_state(player_i2s_stream_writer) == AEL_STATE_PAUSED) {
                    audio_pipeline_resume(player_pipeline);
                }
                else audio_pipeline_run(player_pipeline);

            } else if (strcmp(cmd, "pause") == 0) {
                printf("⏸ Pause\n");
                audio_pipeline_pause(player_pipeline);

            } else if (strcmp(cmd, "next") == 0) {
                printf("⏭ Next track\n");

                char *url = NULL;
                audio_pipeline_stop(player_pipeline);
                audio_pipeline_wait_for_stop(player_pipeline);
                audio_pipeline_terminate(player_pipeline);

                sdcard_list_next(player_sdcard_list_handle, 1, &url);
                printf("Now playing: %s\n", url);

                audio_element_set_uri(player_fatfs_stream_reader, url);
                audio_pipeline_reset_ringbuffer(player_pipeline);
                audio_pipeline_reset_elements(player_pipeline);
                audio_pipeline_run(player_pipeline);

            } else if (strcmp(cmd, "vol+") == 0) {
                int vol;
                audio_board_handle_t board = (audio_board_handle_t)param;
                audio_hal_get_volume(board->audio_hal, &vol);
                vol = (vol + 10 > 100) ? 100 : vol + 10;
                audio_hal_set_volume(board->audio_hal, vol);
                printf("Volume: %d%%\n", vol);

            } else if (strcmp(cmd, "vol-") == 0) {
                int vol;
                audio_board_handle_t board = (audio_board_handle_t)param;
                audio_hal_get_volume(board->audio_hal, &vol);
                vol = (vol - 10 < 0) ? 0 : vol - 10;
                audio_hal_set_volume(board->audio_hal, vol);
                printf("Volume: %d%%\n", vol);

            } else if (strcmp(cmd, "list") == 0) {
                printf("\n--- Track List ---\n");

                sdcard_list_show(player_sdcard_list_handle);

                printf("------------------\n");
            } else {
                printf("Unknown command\n");
            }
        }

        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

esp_err_t init_cli(
    audio_pipeline_handle_t pipeline, audio_element_handle_t i2s_stream_writer,
    audio_element_handle_t fatfs_stream_reader, playlist_operator_handle_t sdcard_list_handle,
    audio_board_handle_t board_handle
) {
    player_pipeline = pipeline;
    player_i2s_stream_writer = i2s_stream_writer;
    player_fatfs_stream_reader = fatfs_stream_reader;
    player_sdcard_list_handle = sdcard_list_handle;

    xTaskCreate(console_task, "console_task", 4096, board_handle, 5, NULL);

    return 0;
}