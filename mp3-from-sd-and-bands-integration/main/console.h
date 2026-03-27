#ifndef CONSOLE_H
#define CONSOLE_H

void console_init(audio_board_handle_t board_handle, playlist_operator_handle_t sdcard_list_handle);

void console_task(void* param);

#endif // CONSOLE_H