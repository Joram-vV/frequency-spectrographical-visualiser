/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "console.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "soc/soc_caps.h"
#include "cmd_system.h"
#include "cmd_wifi.h"
#include "cmd_nvs.h"
#include "console_settings.h"
#include "board.h"
#include "playlist.h"
#include "audio_control.h"
#include "sdcard_list.h"
#include "priorities.h"

/*
 * We warn if a secondary serial console is enabled. A secondary serial console is always output-only and
 * hence not very useful for interactive console applications. If you encounter this warning, consider disabling
 * the secondary serial console in menuconfig unless you know what you are doing.
 */
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#if !CONFIG_ESP_CONSOLE_SECONDARY_NONE
#warning "A secondary serial console is not useful when using the console component. Please disable it in menuconfig."
#endif
#endif

static const char* TAG = "CONSOLE";
#define PROMPT_STR "ESP32S3 FSV"

/* Console command history can be stored to and loaded from a file.
 * The easiest way to do this is to use FATFS filesystem on top of
 * wear_levelling library.
 */
#if CONFIG_CONSOLE_STORE_HISTORY

#define MOUNT_PATH "/data"
#define HISTORY_PATH MOUNT_PATH "/history.txt"

static audio_board_handle_t* board;
static playlist_operator_handle_t* playlist;

/** temporary buffer used for command line parsing */
// static char *s_tmp_line_buf;

static void initialize_filesystem(void)
{
    static wl_handle_t wl_handle;
    const esp_vfs_fat_mount_config_t mount_config = {
            .max_files = 4,
            .format_if_mount_failed = true
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(MOUNT_PATH, "storage", &mount_config, &wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        return;
    }
}
#else
#define HISTORY_PATH NULL
#endif // CONFIG_CONSOLE_STORE_HISTORY

static void initialize_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static int play_mp3(int argc, char **argv){

    // printf("You have entered %d arguments:\n", argc);

    // for (int i = 0; i < argc; i++) {
    //     printf("%s\n", argv[i]);
    // }
    if (argc == 1) {
        audio_control_resume();
        return 0;
    }
    else if (argc != 2) {
        return ESP_ERR_INVALID_ARG;
    }

    char url[256];
    if(get_url_from_filename(argv[1], (char *) url, sizeof(url))){
        return ESP_ERR_ADF_NOT_FOUND;
    }
    audio_control_play_track(url);
    return 0;
}

static int skip_song(int argc, char ** argv){
    printf("\n[ INFO ] Skipping to next track...\n");
    char *url = NULL;
    sdcard_list_next(*playlist, 1, &url);
    audio_control_play_track(url);
    return 0;
}

static int list_songs(int argc, char ** argv){
    printf("\n--- SD Card Playlist ---\n");
    sdcard_list_show(*playlist);
    return 0;
}

static int pause_song(int argc, char ** argv){
    audio_control_pause();
    return 0;
}

static int stop_song(int argc, char ** argv){
    audio_control_stop();
    return 0;
}

void register_commands(void){

    /* Register commands */
    esp_console_register_help_command();
    
    const esp_console_cmd_t cmd_play = {
        .command = "play",
        .help = "resume or play specified <song.mp3>",
        .hint = "<song.mp3>",
        .func = &play_mp3,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd_play) );


    const esp_console_cmd_t cmd_pause = {
        .command = "pause",
        .help = "pause the song playing",
        .func = &pause_song
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd_pause) );


    const esp_console_cmd_t cmd_stop = {
        .command = "stop",
        .help = "stop the song playing",
        .func = &stop_song
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd_stop) );


    const esp_console_cmd_t cmd_skip = {
        .command = "skip",
        .help = "skip the song playing",
        .func = &skip_song
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd_skip) );


    const esp_console_cmd_t cmd_list_songs = {
        .command = "list_songs",
        .help = "list available songs on the sd card",
        .func = &list_songs
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd_list_songs) );
    
    register_system_common();
#if SOC_LIGHT_SLEEP_SUPPORTED
    register_system_light_sleep();
#endif
#if SOC_DEEP_SLEEP_SUPPORTED
    register_system_deep_sleep();
#endif
#if (CONFIG_ESP_WIFI_ENABLED || CONFIG_ESP_HOST_WIFI_ENABLED)
    register_wifi();
#endif
    // register_nvs();

}

void console_init(audio_board_handle_t* board_handle, playlist_operator_handle_t* sdcard_list_handle)
{
    board = board_handle;
    playlist = sdcard_list_handle;
    
    initialize_nvs();
    
    #if CONFIG_CONSOLE_STORE_HISTORY
    initialize_filesystem();
    ESP_LOGI(TAG, "Command history enabled");
    #else
    ESP_LOGI(TAG, "Command history disabled");
    #endif
    
    /* Initialize console output periheral (UART, USB_OTG, USB_JTAG) */
    initialize_console_peripheral();
    
    /* Initialize linenoise library and esp_console*/
    initialize_console_library(HISTORY_PATH);

    register_commands();

    printf("\n"
           "Frequency Spectrographical Visualiser (FSV).\n"
           "The codebase for a musicplayer that uses in world visuals\n"
           "for instance moving bars and lights\n"
           "to visualise the frequency spectrogram of the music it plays.\n"
           "\n"
           "Type 'help' to get the list of commands.\n"
           "Use UP/DOWN arrows to navigate through command history.\n"
           "Press TAB when typing command name to auto-complete.\n"
           "Ctrl+C will terminate the console environment.\n");

    if (linenoiseIsDumbMode()) {
        printf("\n"
               "Your terminal application does not support escape sequences.\n"
               "Line editing and history features are disabled.\n"
               "On Windows, try using Windows Terminal or Putty instead.\n");
    }

    xTaskCreate(
		console_task,
		"console",
		4096,
		NULL,
		CONSOLE_PRIORITY,
		NULL
	);
}

void console_task(void* param)
{
    /* Prompt to be printed before each line.
    * This can be customized, made dynamic, etc.
    */
    const char *prompt = setup_prompt(PROMPT_STR ">");
    /* Main loop */
    while(true) {
        /* Get a line using linenoise.
         * The line is returned when ENTER is pressed.
         */
        char* line = linenoise(prompt);

#if CONFIG_CONSOLE_IGNORE_EMPTY_LINES
        if (line == NULL) { /* Ignore empty lines */
            continue;;
        }
#else
        if (line == NULL) { /* Break on EOF or error */
            break;
        }
#endif // CONFIG_CONSOLE_IGNORE_EMPTY_LINES

        /* Add the command to the history if not empty*/
        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line);
#if CONFIG_CONSOLE_STORE_HISTORY
            /* Save command history to filesystem */
            linenoiseHistorySave(HISTORY_PATH);
#endif // CONFIG_CONSOLE_STORE_HISTORY
        }

        /* Try to run the command */
        int ret;
        esp_err_t err = esp_console_run(line, &ret);
        if (err == ESP_ERR_NOT_FOUND) {
            printf("Unrecognized command\n");
        } else if (err == ESP_ERR_INVALID_ARG) {
            // printf("Invalid arguments\n");
            // command was empty
        } else if (err == ESP_OK && ret != ESP_OK) {
            printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
        } else if (err != ESP_OK) {
            printf("Internal error: %s\n", esp_err_to_name(err));
        }
        /* linenoise allocates line buffer on the heap, so need to free it */
        linenoiseFree(line);
    }

    ESP_LOGE(TAG, "Error or end-of-input, terminating console");
    esp_console_deinit();
}
