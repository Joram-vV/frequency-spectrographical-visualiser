#include <stdio.h>
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_dsp.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "driver/spi_common.h"

// SD Card Pins
#define PIN_NUM_MISO  13
#define PIN_NUM_MOSI  11
#define PIN_NUM_CLK   12
#define PIN_NUM_CS    10

#define FFT_SIZE 1024
#define BUF_SIZE (FFT_SIZE * 2) // 2048 bytes
#define NUM_BANDS 7

// Static arrays in fast RAM
static float fft_workspace[FFT_SIZE * 2]; 
static float window[FFT_SIZE]; 
static float band_peaks[NUM_BANDS];

// The Terminal Visualizer
void print_terminal_eq(int *bands)
{
    printf("\033[H"); // Move cursor to top-left without clearing screen
    printf("\n==== ESP32 AUDIO VISUALIZER ====\n\n");

    for (int row = 8; row > 0; row--) {
        printf(" ");
        for (int b = 0; b < NUM_BANDS; b++) {
            if (bands[b] >= row) {
                // Exactly 5 characters wide to match the labels below
                printf(" ██  "); 
            } else {        
                // Exactly 5 spaces wide
                printf("     "); 
            }
        }
        printf("\n");
    }
    printf(" -----------------------------------\n");
    // Aligned perfectly to a 5-character grid
    printf(" SUB  BAS  L-M  MID  H-M  PRS  TRB  \n");
}

// Pre-calculated MSGEQ7 bin boundaries for 44.1kHz / 1024 FFT
// These centers match: 63Hz, 160Hz, 400Hz, 1kHz, 2.5kHz, 6.25kHz, 16kHz
const int msg_limits[8] = {1, 2, 5, 12, 30, 80, 200, 450};

void process_audio_frame(uint8_t *pcm_data)
{
    // 1. PCM to Float & DC Offset Removal
    float dc_offset = 0.0f;
    for (int i = 0; i < FFT_SIZE; i++) {
        int16_t sample = (pcm_data[2 * i + 1] << 8) | pcm_data[2 * i];
        float val = (float)sample / 32768.0f;
        fft_workspace[i * 2] = val;
        dc_offset += val;
    }
    dc_offset /= FFT_SIZE;

    // 2. Apply Window and Prepare Complex Workspace
    for (int i = 0; i < FFT_SIZE; i++) {
        fft_workspace[i * 2] = (fft_workspace[i * 2] - dc_offset) * window[i];
        fft_workspace[i * 2 + 1] = 0.0f;
    }

    // 3. Perform FFT
    dsps_fft2r_fc32(fft_workspace, FFT_SIZE);
    dsps_bit_rev_fc32(fft_workspace, FFT_SIZE);

    int current_bands[7] = {0};

    // 4. Process the 7 MSGEQ7 Bands
    for (int b = 0; b < 7; b++) {
        float peak_mag = 0.0f;
        int start = msg_limits[b];
        int end = msg_limits[b + 1];

        for (int i = start; i < end; i++) {
            float re = fft_workspace[2 * i];
            float im = fft_workspace[2 * i + 1];
            float mag = sqrtf(re * re + im * im);
            if (mag > peak_mag) peak_mag = mag;
        }

        // 5. Convert to DB & Normalize
        float normalized = peak_mag / 256.0f;
        float db = 20.0f * log10f(normalized + 1e-6f);

        // Map -45dB to 0dB onto 0-8 scale
        float float_level = (db + 45.0f) * (8.0f / 45.0f);
        if (float_level < 0.0f) float_level = 0.0f;
        if (float_level > 8.0f) float_level = 8.0f;

        // 6. MSGEQ7 Decay Emulation: Each "read" decays by ~10% 
        // if the new signal is lower than the previous peak.
        if (float_level > band_peaks[b]) {
            band_peaks[b] = float_level;
        } else {
            band_peaks[b] *= 0.90f; // 10% decay per datasheet 
        }

        current_bands[b] = (int)(band_peaks[b] + 0.5f);
    }

    print_terminal_eq(current_bands);
}

void app_main(void)
{
    // Initialize standard FFT and Window
    dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
    dsps_wind_hann_f32(window, FFT_SIZE);

    // Mount SD Card
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    spi_bus_initialize(SPI2_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 1
    };
    sdmmc_card_t* card;
    esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);

    FILE *f = fopen("/sdcard/song1.pcm", "rb");
    if (f == NULL) {
        printf("Failed to open song1.pcm\n");
        return;
    }

    // Allocate buffer on the heap
    uint8_t *read_buffer = (uint8_t *)malloc(BUF_SIZE);
    
    // Clear the terminal screen once before we start drawing
    printf("\033[2J");

    // The simplest possible reading loop
    while (fread(read_buffer, 1, BUF_SIZE, f) == BUF_SIZE) {
        process_audio_frame(read_buffer);
        
        // Wait exactly 23ms (the time it takes to play 1024 samples)
        vTaskDelay(pdMS_TO_TICKS(23)); 
    }

    free(read_buffer);
    fclose(f);
    esp_vfs_fat_sdcard_unmount("/sdcard", card);
    printf("\nPlayback finished\n");
}