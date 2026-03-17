#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_dsp.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "driver/spi_common.h"
#include "esp_heap_caps.h"
#include <unistd.h>
#include <fcntl.h>

#define PIN_NUM_MISO  13
#define PIN_NUM_MOSI  11
#define PIN_NUM_CLK   12
#define PIN_NUM_CS    10

#define UART_PORT UART_NUM_0
#define BUF_SIZE 2048
#define FFT_SIZE 512

// Allocate with extra padding to avoid buffer overflows
// Use DRAM-capable allocation
static float input[FFT_SIZE + 16];
static float output[FFT_SIZE + 16];

void compute_spectrum()
{
    // Ensure buffer is clean for next FFT
    memset(input + FFT_SIZE, 0, 16 * sizeof(float));  // Clear padding area
    
    // Apply window
    dsps_wind_hann_f32(input, FFT_SIZE);

    // FFT
    dsps_fft2r_fc32(input, FFT_SIZE);
    dsps_bit_rev_fc32(input, FFT_SIZE);
    dsps_cplx2reC_fc32(input, FFT_SIZE);

    // Magnitude
    for (int i = 0; i < FFT_SIZE / 2; i++) {
        float re = input[2*i];
        float im = input[2*i+1];
        output[i] = sqrtf(re*re + im*im);
    }
}

void print_bands()
{
    int bands[7] = {0};

    int band_size = (FFT_SIZE/2) / 7;

    for (int b = 0; b < 7; b++) {
        float sum = 0;
        for (int i = b * band_size; i < (b+1) * band_size; i++) {
            sum += output[i];
        }

        float avg = sum / band_size;

        // Normalize → 0–8 blocks
        int level = (int)(avg / 5000.0);  // adjust scaling - reduced to see more variation
        if (level > 8) level = 8;

        bands[b] = level;
    }

    // Print result
    printf("Bands: ");
    for (int i = 0; i < 7; i++) {
        printf("%d ", bands[i]);
    }
    printf("\n");
}

void app_main(void)
{
    esp_err_t ret;

    // Clear buffers before use
    memset(input, 0, (FFT_SIZE + 16) * sizeof(float));
    memset(output, 0, (FFT_SIZE + 16) * sizeof(float));

    // Initialize FFT
    dsps_fft2r_init_fc32(NULL, FFT_SIZE);

    printf("FFT initialized. Free heap: %ld bytes\n", esp_get_free_heap_size());

    // Give time for system to initialize
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("Initializing SD card...\n");
    vTaskDelay(pdMS_TO_TICKS(500));

    // Initialize SPI bus for SD card
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    spi_host_device_t host_id = SPI2_HOST;
    ret = spi_bus_initialize(host_id, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        printf("Failed to initialize SPI bus: %s\n", esp_err_to_name(ret));
        return;
    }

    printf("Mounting SD card...\n");
    vTaskDelay(pdMS_TO_TICKS(500));

    // Initialize SD card
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host_id;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 1,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t* card;
    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        printf("Failed to mount SD card: %s\n", esp_err_to_name(ret));
        return;
    }

    printf("SD card mounted successfully\n");
    vTaskDelay(pdMS_TO_TICKS(500));

    // Use low-level file operations (open/read/close) to avoid FatFS stdio issues
    printf("Opening PCM file...\n");
    int fd = open("/sdcard/song1.pcm", O_RDONLY);
    if (fd < 0) {
        printf("Failed to open song1.pcm, trying song.pcm\n");
        fd = open("/sdcard/song.pcm", O_RDONLY);
        if (fd < 0) {
            printf("Failed to open PCM file\n");
            esp_vfs_fat_sdcard_unmount("/sdcard", card);
            return;
        }
    }
    printf("PCM file opened with fd=%d\n", fd);
    vTaskDelay(pdMS_TO_TICKS(500));

    // Read entire file into heap buffer (in PSRAM)
    printf("Reading entire file into memory...\n");
    uint8_t* file_buffer = (uint8_t*)malloc(2 * 1024 * 1024); // 2MB buffer in PSRAM
    if (!file_buffer) {
        printf("Failed to allocate file buffer\n");
        close(fd);
        esp_vfs_fat_sdcard_unmount("/sdcard", card);
        return;
    }

    ssize_t total_read = 0;
    ssize_t len;
    while (1) {
        len = read(fd, file_buffer + total_read, 65536); // Read in 64KB chunks
        if (len <= 0) {
            break; // EOF or error
        }
        total_read += len;
        if (total_read >= 2 * 1024 * 1024) {
            printf("File buffer full\n");
            break;
        }
    }
    close(fd);
    esp_vfs_fat_sdcard_unmount("/sdcard", card);
    
    printf("File read complete: %d bytes\n", total_read);
    vTaskDelay(pdMS_TO_TICKS(1000)); // Wait before processing

    // Now process the buffer without any more SD card access
    uint8_t* data = (uint8_t*)malloc(BUF_SIZE);
    if (!data) {
        printf("Failed to allocate processing buffer\n");
        free(file_buffer);
        return;
    }

    int read_count = 0;
    int buffer_pos = 0;

    while (buffer_pos < total_read) {
        vTaskDelay(pdMS_TO_TICKS(100)); // Delay BEFORE processing
        
        // Copy from file buffer to processing buffer
        int remaining = total_read - buffer_pos;
        int to_copy = (remaining > BUF_SIZE) ? BUF_SIZE : remaining;
        memcpy(data, file_buffer + buffer_pos, to_copy);
        buffer_pos += to_copy;

        read_count++;
        printf("Processing chunk %d: %d bytes\n", read_count, to_copy);

        // Convert bytes → float samples (16-bit PCM assumed)
        int samples = to_copy / 2;
        for (int i = 0; i < samples && i < FFT_SIZE; i++) {
            int16_t s = (data[2*i+1] << 8) | data[2*i];
            input[i] = (float)s;
        }
        
        // Pad unused samples with zeros
        for (int i = samples; i < FFT_SIZE; i++) {
            input[i] = 0;
        }

        printf("Chunk %d: Free heap before FFT: %ld bytes\n", read_count, esp_get_free_heap_size());
        printf("About to compute spectrum for chunk %d\n", read_count);
        compute_spectrum();
        printf("Spectrum computed successfully. Free heap after: %ld bytes\n", esp_get_free_heap_size());
        print_bands();

        if (read_count > 200) {
            printf("Stopping after 200 chunks for safety\n");
            break;
        }
    }

    free(data);
    free(file_buffer);
    printf("Playback finished\n");
}