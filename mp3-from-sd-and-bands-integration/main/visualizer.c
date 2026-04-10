#include "visualizer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_dsp.h"
#include "audio_pipeline.h"
#include "fatfs_stream.h"
#include "mp3_decoder.h"
#include "raw_stream.h"

#include "shared_state.h"

#define FFT_SIZE 1024
#define NUM_BANDS 7
#define VISUALIZER_FPS 1

static float fft_workspace[FFT_SIZE * 2]; 
static float window[FFT_SIZE]; 
static const int msg_limits[8] = {1, 2, 5, 12, 30, 80, 200, 450};

static volatile bool vis_running = false;
static FILE *vis_file = NULL;

static void print_terminal_eq(int *bands) {
    printf("\n");
    for (int b = 0; b < NUM_BANDS; b++) {
        printf("%d ", bands[b]);
    }
    printf("\n");
}

static void get_txt_path(const char* url, char* txt_path) {
    const char *prefix = "file://";
    if (strncmp(url, prefix, strlen(prefix)) == 0) {
        sprintf(txt_path, "/%s", url + strlen(prefix));
    } else {
        strcpy(txt_path, url);
    }
    char *dot = strrchr(txt_path, '.');
    if (dot) strcpy(dot, ".txt");
}

void visualizer_init(void) {
    dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
    dsps_wind_hann_f32(window, FFT_SIZE);
}

void visualizer_preprocess_file(const char* mp3_url) {
    char txt_path[256];
    get_txt_path(mp3_url, txt_path);

    FILE *f_check = fopen(txt_path, "r");
    if (f_check) {
        int file_fps = 0;
        // Read the first line to see if it matches our target FPS
        if (fscanf(f_check, "%d\n", &file_fps) == 1 && file_fps == TARGET_VISUALIZER_FPS) {
            fseek(f_check, 0, SEEK_END);
            long size = ftell(f_check);
            fclose(f_check);
            if (size > 10) return; // Valid file with correct FPS and some data
        } else {
            fclose(f_check); // Close it so we can overwrite it below
        }
        printf("\n[ WARN ] File missing, empty, or wrong FPS. Overwriting...\n");
    }

    printf("\n[ INFO ] Pre-processing new track...\n");
    printf("[ INFO ] This will run as fast as possible. Please wait...\n");

    char *write_buf = (char *)malloc(4096);
    char *pcm_buf = (char *)malloc(4096);
    
    if (!write_buf || !pcm_buf) {
        printf("\n[ ERROR ] Out of memory for pre-processing buffers.\n");
        if (write_buf) free(write_buf);
        if (pcm_buf) free(pcm_buf);
        return;
    }

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    audio_pipeline_handle_t prep_pipeline = audio_pipeline_init(&pipeline_cfg);

    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    audio_element_handle_t fatfs_read = fatfs_stream_init(&fatfs_cfg);
    audio_element_set_uri(fatfs_read, mp3_url);

    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    audio_element_handle_t mp3_dec = mp3_decoder_init(&mp3_cfg);

    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_WRITER;
    audio_element_handle_t raw_write = raw_stream_init(&raw_cfg);

    audio_pipeline_register(prep_pipeline, fatfs_read, "file");
    audio_pipeline_register(prep_pipeline, mp3_dec, "mp3");
    audio_pipeline_register(prep_pipeline, raw_write, "raw");

    const char *link_tag[3] = {"file", "mp3", "raw"};
    audio_pipeline_link(prep_pipeline, &link_tag[0], 3);

    audio_pipeline_run(prep_pipeline);

    FILE *f_txt = fopen(txt_path, "w");
    if (!f_txt) {
        printf("\n[ ERROR ] Failed to create %s on SD Card\n", txt_path);
        free(write_buf);
        free(pcm_buf);
        return;
    }

    int write_pos = 0;
    
    // Write the FPS header as the very first line
    write_pos += snprintf(write_buf + write_pos, 4096 - write_pos, "%d\n", TARGET_VISUALIZER_FPS);

    int bytes_read;
    int loop_counter = 0;
    
    // New averaging variables
    float band_accumulators[7] = {0.0f};
    int frames_in_window = 0;
    float time_accumulated = 0.0f;
    const float target_window_sec = 1.0f / TARGET_VISUALIZER_FPS;
    const float frame_sec = 1024.0f / 44100.0f;

    while ((bytes_read = raw_stream_read(raw_write, pcm_buf, 4096)) > 0) {
        if (bytes_read < 4096) memset(pcm_buf + bytes_read, 0, 4096 - bytes_read);

        float dc_offset = 0.0f;
        int16_t *pcm16 = (int16_t *)pcm_buf;
        
        for (int i = 0; i < FFT_SIZE; i++) {
            float val = (float)pcm16[2 * i] / 32768.0f;
            fft_workspace[i * 2] = val;
            dc_offset += val;
        }
        dc_offset /= FFT_SIZE;

        for (int i = 0; i < FFT_SIZE; i++) {
            fft_workspace[i * 2] = (fft_workspace[i * 2] - dc_offset) * window[i];
            fft_workspace[i * 2 + 1] = 0.0f;
        }

        dsps_fft2r_fc32(fft_workspace, FFT_SIZE);
        dsps_bit_rev_fc32(fft_workspace, FFT_SIZE);

        for (int b = 0; b < 7; b++) {
            float peak_mag = 0.0f;
            for (int i = msg_limits[b]; i < msg_limits[b + 1]; i++) {
                float re = fft_workspace[2 * i];
                float im = fft_workspace[2 * i + 1];
                float mag = sqrtf(re * re + im * im);
                if (mag > peak_mag) peak_mag = mag;
            }

            float normalized = peak_mag / 256.0f;
            float db = 20.0f * log10f(normalized + 1e-6f);
            float float_level = (db + 45.0f) * (8.0f / 45.0f);
            
            if (float_level < 0.0f) float_level = 0.0f;
            if (float_level > 8.0f) float_level = 8.0f;

            // Add the instantaneous level to our accumulator
            band_accumulators[b] += float_level;
        }

        frames_in_window++;
        time_accumulated += frame_sec;

        // Once we've accumulated enough time for our target FPS, average and write
        if (time_accumulated >= target_window_sec) {
            int current_bands[7] = {0};
            for (int b = 0; b < 7; b++) {
                float avg = band_accumulators[b] / frames_in_window;
                current_bands[b] = (int)(avg + 0.5f); 
                band_accumulators[b] = 0.0f;
            }
            
            time_accumulated -= target_window_sec; 
            frames_in_window = 0;

            write_pos += snprintf(write_buf + write_pos, 4096 - write_pos, 
                                  "%d %d %d %d %d %d %d\n",
                                  current_bands[0], current_bands[1], current_bands[2],
                                  current_bands[3], current_bands[4], current_bands[5], current_bands[6]);
        }

        // CRITICAL: This flushes the buffer before it overflows and causes NUL bytes
        if (write_pos >= 4096 - 100) {
            fwrite(write_buf, 1, write_pos, f_txt);
            write_pos = 0;
        }

        if (++loop_counter % 10 == 0) vTaskDelay(pdMS_TO_TICKS(1));
    }

    // The audio stream finished! Write any remaining accumulated data for the last partial frame.
    if (frames_in_window > 0) {
        int current_bands[7] = {0};
        for (int b = 0; b < 7; b++) {
            float avg = band_accumulators[b] / frames_in_window;
            current_bands[b] = (int)(avg + 0.5f); 
        }
        write_pos += snprintf(write_buf + write_pos, 4096 - write_pos, 
                              "%d %d %d %d %d %d %d\n",
                              current_bands[0], current_bands[1], current_bands[2],
                              current_bands[3], current_bands[4], current_bands[5], current_bands[6]);
    }

    // Flush whatever is left to the SD Card
    if (write_pos > 0) {
        fwrite(write_buf, 1, write_pos, f_txt);
    }
    
    // Cleanly close the file
    fflush(f_txt);
    fclose(f_txt);
    
    free(write_buf);
    free(pcm_buf);
    
    audio_pipeline_stop(prep_pipeline);
    audio_pipeline_wait_for_stop(prep_pipeline);
    audio_pipeline_terminate(prep_pipeline);
    audio_pipeline_unregister_more(prep_pipeline, fatfs_read, mp3_dec, raw_write, NULL);
    audio_pipeline_deinit(prep_pipeline);
    audio_element_deinit(fatfs_read);
    audio_element_deinit(mp3_dec);
    audio_element_deinit(raw_write);
    
    printf("\n[ INFO ] Pre-processing complete. Ready.\n");
}

void visualizer_start(const char* url) {
    char txt_path[256];
    get_txt_path(url, txt_path);
    vis_file = fopen(txt_path, "r");
    if (!vis_file) {
        printf("[ WARN ] Visualizer data missing for this track\n");
    } else {
        printf("\033[2J"); 
        vis_running = true;
    }
}

void visualizer_stop(void) {
    vis_running = false;
    if (vis_file) {
        fclose(vis_file);
        vis_file = NULL;
    }
}

bool visualizer_is_running(void) {
    return vis_running;
}

void visualizer_task(void *pvParameters) {
    int bands[NUM_BANDS];
    int current_file_fps = TARGET_VISUALIZER_FPS; // Fallback default
    TickType_t delay_ticks = pdMS_TO_TICKS(1000 / TARGET_VISUALIZER_FPS);
    bool is_new_file = true;

    while (1) {
        if (vis_running && vis_file != NULL) {
            
            // 1. If we just started playing a new file, read the FPS header first
            if (is_new_file) {
                if (fscanf(vis_file, "%d\n", &current_file_fps) == 1) {
                    if (current_file_fps <= 0) current_file_fps = TARGET_VISUALIZER_FPS; // Sanity check
                    delay_ticks = pdMS_TO_TICKS(1000 / current_file_fps);
                }
                is_new_file = false;
            }

            // 2. Read exactly one line of 7 bands
            if (fscanf(vis_file, "%d %d %d %d %d %d %d", 
                       &bands[0], &bands[1], &bands[2], &bands[3], 
                       &bands[4], &bands[5], &bands[6]) == 7) {
                
                // 3. Push to the globally shared array safely using our Mutex
                if (shared_state_mutex != NULL) {
                    if (xSemaphoreTake(shared_state_mutex, portMAX_DELAY) == pdTRUE) {
                        for (int i = 0; i < NUM_BANDS; i++) {
                            // Convert the 0-8 integer into a float for the PID controller
                            shared_target_heights[i] = (float)bands[i];
                        }
                        xSemaphoreGive(shared_state_mutex); // Always give it back!
                    }
                }

                // Optional: Print to terminal so you can still watch it run in the console
                print_terminal_eq(bands);

                // 4. Wait for the exact duration of this frame before reading the next one
                vTaskDelay(delay_ticks);
                
            } else {
                // We reached the end of the file (or it's corrupt)
                visualizer_stop();
                is_new_file = true; // Reset the flag for the next song
            }
            
        } else {
            // Visualizer is paused or stopped
            is_new_file = true; // Ensure we read the header next time we start
            vTaskDelay(pdMS_TO_TICKS(100)); // Sleep peacefully
        }
    }
}