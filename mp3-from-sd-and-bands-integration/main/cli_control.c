#include "cli_control.h"
#include "visualizer.h"
#include "audio_control.h"
#include "sdcard_list.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "console.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>

static audio_board_handle_t board;
static playlist_operator_handle_t playlist;

static void console_task1(void* param) {
    char cmd[32];
    int cmd_pos = 0;
    bool menu_printed = false; 

    int flags = fcntl(fileno(stdin), F_GETFL);
    fcntl(fileno(stdin), F_SETFL, flags | O_NONBLOCK);

    printf("\033[2J"); 

    while (1) {
        if (!visualizer_is_running()) {
            if (!menu_printed) {
                printf("\n--- Media Controls ---\n");
                printf("Commands: play <song>, pause, resume, stop, next, vol+, vol-, list\n> ");
                fflush(stdout);
                menu_printed = true; 
            }
        } else {
            menu_printed = false; 
        }

        int c = fgetc(stdin);
        if (c != EOF) {
            if (c == '\n' || c == '\r') {
                if (cmd_pos > 0) {
                    cmd[cmd_pos] = '\0'; 
                    cmd_pos = 0;         
                    menu_printed = false; 

                    if (strncmp(cmd, "play", 4) == 0) {
                        if (cmd[4] == ' ' && strlen(cmd) > 5) {
                            char *filename = cmd + 5; 
                            char filepath[256];
                            snprintf(filepath, sizeof(filepath), "/sdcard/%s", filename);
                            FILE *f = fopen(filepath, "r");
                            if (f) {
                                fclose(f);
                                char url[256];
                                snprintf(url, sizeof(url), "file://sdcard/%s", filename);
                                audio_control_play_track(url);
                            } else {
                                printf("\n[ ERROR ] File not found: %s\n", filename);
                            }
                        } else if (cmd[4] == '\0') {
                            if (!visualizer_is_running()) { 
                                char *url = NULL;
                                sdcard_list_current(playlist, &url);
                                if (url) audio_control_play_track(url);
                            } else {
                                printf("\n[ INFO ] Already playing.\n");
                            }
                        } else {
                            printf("\n[ ERROR ] Unknown command: '%s'\n", cmd);
                        }
                    } else if (strcmp(cmd, "pause") == 0) {
                        audio_control_pause();
                    } else if (strcmp(cmd, "resume") == 0) {
                        audio_control_resume();
                    } else if (strcmp(cmd, "stop") == 0) {
                        audio_control_stop();
                    } else if (strcmp(cmd, "next") == 0) {
                        printf("\n[ INFO ] Skipping to next track...\n");
                        char *url = NULL;
                        sdcard_list_next(playlist, 1, &url);
                        audio_control_play_track(url);
                    } else if (strcmp(cmd, "vol+") == 0 || strcmp(cmd, "vol-") == 0) {
                        int vol;
                        audio_hal_get_volume(board->audio_hal, &vol);
                        vol += (strcmp(cmd, "vol+") == 0) ? 10 : -10;
                        if (vol > 100) vol = 100;
                        if (vol < 0) vol = 0;
                        audio_hal_set_volume(board->audio_hal, vol);
                        printf("\n[ INFO ] Volume set to %d%%\n", vol);
                    } else if (strcmp(cmd, "list") == 0) {
                        bool was_running = visualizer_is_running();
                        visualizer_stop(); 
                        printf("\n--- SD Card Playlist ---\n");
                        sdcard_list_show(playlist);
                        if (was_running) {
                            printf("\n(Press Enter to resume visualizer...)");
                            while(1) {
                                int wait_c = fgetc(stdin);
                                if (wait_c == '\n' || wait_c == '\r') break;
                                vTaskDelay(pdMS_TO_TICKS(50));
                            }
                            audio_control_resume();
                        }
                    } else {
                        printf("\n[ ERROR ] Unknown command: '%s'\n", cmd);
                    }
                }
            } else if (c == '\b' || c == 127) { 
                if (cmd_pos > 0) {
                    cmd_pos--;
                    printf("\b \b"); 
                    fflush(stdout);
                }
            } else if (cmd_pos < sizeof(cmd) - 1) {
                cmd[cmd_pos++] = (char)c;
                putchar(c); 
                fflush(stdout);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

void cli_control_init(audio_board_handle_t board_handle, playlist_operator_handle_t sdcard_list_handle) {
    board = board_handle;
    playlist = sdcard_list_handle;
    xTaskCreate(console_task, "console_task", 4096, NULL, 4, NULL);
}