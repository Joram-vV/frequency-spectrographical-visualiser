#ifndef MP3_PLAYER_CLI
#define MP3_PLAYER_CLI

#include "esp_err.h"

#include "audio_element.h"
#include "audio_pipeline.h"

#include "sdcard_list.h"

#include "board.h"

esp_err_t init_cli(
    audio_pipeline_handle_t pipeline, audio_element_handle_t i2s_stream_writer,
    audio_element_handle_t fatfs_stream_reader, playlist_operator_handle_t sdcard_list_handle,
    audio_board_handle_t board_handle
);

#endif