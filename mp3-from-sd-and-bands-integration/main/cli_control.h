#ifndef CLI_CONTROL_H
#define CLI_CONTROL_H

#include "board.h"
#include "playlist.h"

void cli_control_init(audio_board_handle_t board_handle, playlist_operator_handle_t sdcard_list_handle);

#endif // CLI_CONTROL_H