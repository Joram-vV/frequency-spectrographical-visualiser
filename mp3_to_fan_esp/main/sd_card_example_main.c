/* SD card and FAT filesystem example with MP3 frequency analysis.
   This example uses SPI peripheral to communicate with SD card and analyzes MP3 files.

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.

   FREQUENCY BAND CONFIGURATION:
   ============================
   To modify the frequency bands, edit the FREQUENCY_BANDS array below.
   Each band is defined as {low_freq, high_freq, "description"}

   Current 7-band configuration (standard audio equalizer bands):
   - Band 0: Sub-bass (20-60 Hz) - deep bass, felt more than heard
   - Band 1: Bass (60-250 Hz) - fundamental bass frequencies
   - Band 2: Low midrange (250-500 Hz) - warm, muddy sounds
   - Band 3: Midrange (500-2000 Hz) - vocal and instrument fundamentals
   - Band 4: Upper midrange (2000-4000 Hz) - sibilance, clarity
   - Band 5: Presence (4000-6000 Hz) - detail, articulation
   - Band 6: Brilliance (6000-20000 Hz) - air, sparkle, brightness

   To change the number of bands:
   1. Update NUM_BANDS constant
   2. Update FREQUENCY_BANDS array
   3. Update generate_frequency_bands() function logic

   For true accuracy, this code would need actual MP3 decoding and FFT analysis.
   Currently it generates realistic simulated data based on music frequency characteristics.
*/

#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <math.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "sd_test_io.h"
#include "esp_timer.h"
#include "esp_dsp.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if SOC_SDMMC_IO_POWER_EXTERNAL
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#endif

#define EXAMPLE_MAX_CHAR_SIZE    64
#define SAMPLE_RATE 44100
#define FFT_SIZE 1024
#define NUM_BANDS 7
#define SAMPLES_PER_SECOND 2

// Configurable frequency bands (in Hz)
// You can easily modify these ranges to change the frequency bands
// Format: {low_freq, high_freq, description}
typedef struct {
    int low_freq;
    int high_freq;
    const char* description;
} freq_band_t;

static const freq_band_t FREQUENCY_BANDS[NUM_BANDS] = {
    {20, 60, "Sub-bass"},           // 0: Sub-bass (20-60 Hz)
    {60, 250, "Bass"},              // 1: Bass (60-250 Hz)
    {250, 500, "Low midrange"},     // 2: Low midrange (250-500 Hz)
    {500, 2000, "Midrange"},        // 3: Midrange (500-2000 Hz)
    {2000, 4000, "Upper midrange"}, // 4: Upper midrange (2000-4000 Hz)
    {4000, 6000, "Presence"},       // 5: Presence (4000-6000 Hz)
    {6000, 20000, "Brilliance"}     // 6: Brilliance (6000-20000 Hz)
};
static const char *TAG = "mp3_analyzer";

// Function to log frequency band configuration
static void log_frequency_bands(void) {
    ESP_LOGI(TAG, "Frequency band configuration:");
    for (int i = 0; i < NUM_BANDS; i++) {
        ESP_LOGI(TAG, "  Band %d: %s (%d-%d Hz)",
                 i, FREQUENCY_BANDS[i].description,
                 FREQUENCY_BANDS[i].low_freq, FREQUENCY_BANDS[i].high_freq);
    }
}

#define MOUNT_POINT "/sdcard"

#ifdef CONFIG_EXAMPLE_DEBUG_PIN_CONNECTIONS
const char* names[] = {"CLK ", "MOSI", "MISO", "CS  "};
const int pins[] = {CONFIG_EXAMPLE_PIN_CLK,
                    CONFIG_EXAMPLE_PIN_MOSI,
                    CONFIG_EXAMPLE_PIN_MISO,
                    CONFIG_EXAMPLE_PIN_CS};

const int pin_count = sizeof(pins)/sizeof(pins[0]);
#if CONFIG_EXAMPLE_ENABLE_ADC_FEATURE
const int adc_channels[] = {CONFIG_EXAMPLE_ADC_PIN_CLK,
                            CONFIG_EXAMPLE_ADC_PIN_MOSI,
                            CONFIG_EXAMPLE_ADC_PIN_MISO,
                            CONFIG_EXAMPLE_ADC_PIN_CS};
#endif //CONFIG_EXAMPLE_ENABLE_ADC_FEATURE

pin_configuration_t config = {
    .names = names,
    .pins = pins,
#if CONFIG_EXAMPLE_ENABLE_ADC_FEATURE
    .adc_channels = adc_channels,
#endif
};
#endif //CONFIG_EXAMPLE_DEBUG_PIN_CONNECTIONS

// Pin assignments can be set in menuconfig, see "SD SPI Example Configuration" menu.
// You can also change the pin assignments here by changing the following 4 lines.
#define PIN_NUM_MISO  13
#define PIN_NUM_MOSI  11
#define PIN_NUM_CLK   12
#define PIN_NUM_CS    10


// Simple fallback function for when MP3 analysis fails
static void generate_fallback_frequency_bands(uint8_t bands[NUM_BANDS]) {
    // Get current time for basic variation
    uint32_t time_ms = esp_timer_get_time() / 1000;

    for (int i = 0; i < NUM_BANDS; i++) {
        // Basic frequency-dependent energy levels
        float base_energy = 0.5f - (float)i / (NUM_BANDS * 2.0f);
        float time_variation = sinf(time_ms * 0.001f + i) * 0.1f;
        float energy = base_energy + time_variation;
        energy = fmaxf(0.0f, fminf(1.0f, energy));
        bands[i] = (uint8_t)(energy * 15.0f);
    }
}

// Function to analyze MP3 file data and generate frequency band values
// This reads raw MP3 data and performs basic frequency analysis
// For true accuracy, this would need full MP3 decoding + FFT
static void analyze_mp3_frequency_bands(const char *mp3_path, uint8_t bands[NUM_BANDS]) {
    FILE *f = fopen(mp3_path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open MP3 file for analysis: %s", mp3_path);
        // Fallback to basic simulation if file can't be opened
        generate_fallback_frequency_bands(bands);
        return;
    }

    // Read chunks of data for analysis
    #define CHUNK_SIZE 4096
    uint8_t buffer[CHUNK_SIZE];
    size_t bytes_read = fread(buffer, 1, CHUNK_SIZE, f);
    fclose(f);

    if (bytes_read < 100) {
        ESP_LOGW(TAG, "MP3 file too small for analysis, using fallback");
        generate_fallback_frequency_bands(bands);
        return;
    }

    // Initialize bands to zero
    for (int i = 0; i < NUM_BANDS; i++) {
        bands[i] = 0;
    }

    // Simple frequency analysis based on byte patterns
    // This analyzes the distribution of byte values and patterns
    // Higher frequency content tends to have more variation

    // Analyze byte value distribution (0-255)
    int histogram[256] = {0};
    for (size_t i = 0; i < bytes_read; i++) {
        histogram[buffer[i]]++;
    }

    // Calculate entropy (measure of randomness/variation)
    float entropy = 0.0f;
    for (int i = 0; i < 256; i++) {
        if (histogram[i] > 0) {
            float p = (float)histogram[i] / bytes_read;
            entropy -= p * log2f(p);
        }
    }

    // Analyze byte differences (high frequency content)
    int diff_sum = 0;
    int diff_count = 0;
    for (size_t i = 1; i < bytes_read; i++) {
        int diff = abs((int)buffer[i] - (int)buffer[i-1]);
        diff_sum += diff;
        diff_count++;
    }
    float avg_diff = diff_count > 0 ? (float)diff_sum / diff_count : 0.0f;

    // Analyze patterns (repeating sequences indicate lower frequencies)
    int pattern_score = 0;
    for (size_t i = 0; i < bytes_read - 4; i++) {
        if (buffer[i] == buffer[i+2] && buffer[i+1] == buffer[i+3]) {
            pattern_score++;
        }
    }
    float pattern_ratio = (float)pattern_score / (bytes_read - 4);

    // Map analysis results to frequency bands
    // Lower bands (bass) get more energy from patterns and low entropy
    // Higher bands (treble) get more energy from differences and high entropy

    for (int i = 0; i < NUM_BANDS; i++) {
        float band_energy = 0.0f;

        // Base energy from entropy (higher entropy = more high frequency content)
        band_energy += entropy / 8.0f; // Normalize entropy

        // Add contribution based on average difference (high freq content)
        band_energy += avg_diff / 128.0f; // Normalize difference

        // Lower frequencies get boost from patterns (repeating content)
        if (i < 2) { // Sub-bass and bass
            band_energy += (1.0f - pattern_ratio) * 0.5f;
        }

        // Higher frequencies get boost from entropy
        if (i >= 4) { // Upper midrange, presence, brilliance
            band_energy += entropy / 6.0f;
        }

        // Frequency-dependent scaling
        float freq_factor = 1.0f - (float)i / (NUM_BANDS * 1.5f);
        band_energy *= freq_factor;

        // Add some time-based variation for dynamics
        uint32_t time_ms = esp_timer_get_time() / 1000;
        float time_variation = sinf(time_ms * 0.001f + i * 0.5f) * 0.1f;
        band_energy += time_variation;

        // Clamp and scale to 0-15
        band_energy = fmaxf(0.0f, fminf(1.0f, band_energy));
        bands[i] = (uint8_t)(band_energy * 15.0f);
    }

    ESP_LOGD(TAG, "MP3 analysis - entropy: %.2f, avg_diff: %.2f, patterns: %.3f",
             entropy, avg_diff, pattern_ratio);
}

static void analyze_mp3_frequency_bands_from_buffer(const uint8_t *buffer, size_t buffer_size, uint8_t bands[NUM_BANDS]) {
    if (buffer == NULL || buffer_size < 100) {
        ESP_LOGW(TAG, "Invalid buffer for analysis, using fallback");
        generate_fallback_frequency_bands(bands);
        return;
    }

    // Initialize bands to zero
    for (int i = 0; i < NUM_BANDS; i++) {
        bands[i] = 0;
    }

    // Simple frequency analysis based on byte patterns
    // This analyzes the distribution of byte values and patterns
    // Higher frequency content tends to have more variation

    // Analyze byte value distribution (0-255)
    int histogram[256] = {0};
    for (size_t i = 0; i < buffer_size; i++) {
        histogram[buffer[i]]++;
    }

    // Calculate entropy (measure of randomness/variation)
    float entropy = 0.0f;
    for (int i = 0; i < 256; i++) {
        if (histogram[i] > 0) {
            float p = (float)histogram[i] / buffer_size;
            entropy -= p * log2f(p);
        }
    }

    // Analyze byte differences (high frequency content)
    int diff_sum = 0;
    int diff_count = 0;
    for (size_t i = 1; i < buffer_size; i++) {
        int diff = abs((int)buffer[i] - (int)buffer[i-1]);
        diff_sum += diff;
        diff_count++;
    }
    float avg_diff = diff_count > 0 ? (float)diff_sum / diff_count : 0.0f;

    // Analyze patterns (repeating sequences indicate lower frequencies)
    int pattern_score = 0;
    for (size_t i = 0; i < buffer_size - 4; i++) {
        if (buffer[i] == buffer[i+2] && buffer[i+1] == buffer[i+3]) {
            pattern_score++;
        }
    }
    float pattern_ratio = (float)pattern_score / (buffer_size - 4);

    // Map analysis results to frequency bands
    // Lower bands (bass) get more energy from patterns and low entropy
    // Higher bands (treble) get more energy from differences and high entropy

    for (int i = 0; i < NUM_BANDS; i++) {
        float band_energy = 0.0f;

        // Base energy from entropy (higher entropy = more high frequency content)
        band_energy += entropy / 8.0f; // Normalize entropy

        // Add contribution based on average difference (high freq content)
        band_energy += avg_diff / 128.0f; // Normalize difference

        // Lower frequencies get boost from patterns (repeating content)
        if (i < 2) { // Sub-bass and bass
            band_energy += (1.0f - pattern_ratio) * 0.5f;
        }

        // Higher frequencies get boost from entropy
        if (i >= 4) { // Upper midrange, presence, brilliance
            band_energy += entropy / 6.0f;
        }

        // Frequency-dependent scaling
        float freq_factor = 1.0f - (float)i / (NUM_BANDS * 1.5f);
        band_energy *= freq_factor;

        // Add some time-based variation for dynamics
        uint32_t time_ms = esp_timer_get_time() / 1000;
        float time_variation = sinf(time_ms * 0.001f + i * 0.5f) * 0.1f;
        band_energy += time_variation;

        // Clamp and scale to 0-15
        band_energy = fmaxf(0.0f, fminf(1.0f, band_energy));
        bands[i] = (uint8_t)(band_energy * 15.0f);
    }

    ESP_LOGD(TAG, "Buffer analysis - entropy: %.2f, avg_diff: %.2f, patterns: %.3f",
             entropy, avg_diff, pattern_ratio);
}

static void analyze_mp3_frequency_bands_from_buffer_chunk(const uint8_t *buffer, size_t buffer_size, uint8_t bands[NUM_BANDS]) {
    if (buffer == NULL || buffer_size < 50) {
        generate_fallback_frequency_bands(bands);
        return;
    }

    // Initialize bands to zero - minimal stack usage
    for (int i = 0; i < NUM_BANDS; i++) {
        bands[i] = 0;
    }

    // Simple analysis with minimal memory usage
    uint32_t simple_sum = 0;
    uint32_t diff_sum = 0;
    uint32_t pattern_count = 0;

    // Single pass through buffer
    for (size_t i = 0; i < buffer_size; i++) {
        simple_sum += buffer[i];

        if (i > 0) {
            int diff = abs((int)buffer[i] - (int)buffer[i-1]);
            diff_sum += diff;
        }

        // Simple pattern detection
        if (i >= 4 && i % 4 == 0 && buffer[i] == buffer[i-4]) {
            pattern_count++;
        }
    }

    // Calculate simple metrics
    float avg_value = (float)simple_sum / buffer_size;
    float avg_diff = (float)diff_sum / (buffer_size - 1);
    float pattern_ratio = (float)pattern_count / (buffer_size / 4);

    // Map to frequency bands using simple rules
    for (int i = 0; i < NUM_BANDS; i++) {
        float band_energy = 0.0f;

        // Base energy from average value
        band_energy += avg_value / 32.0f;

        // High frequencies from differences
        if (i >= 4) {
            band_energy += avg_diff / 16.0f;
        }

        // Low frequencies from patterns
        if (i < 3) {
            band_energy += (1.0f - pattern_ratio) * 3.0f;
        }

        // Midrange from average
        if (i >= 2 && i <= 4) {
            band_energy += avg_value / 64.0f;
        }

        // Frequency rolloff
        float freq_factor = 1.0f - (float)i / (NUM_BANDS * 1.5f);
        band_energy *= freq_factor;

        // Add subtle variation
        uint32_t time_ms = esp_timer_get_time() / 1000;
        float variation = sinf(time_ms * 0.001f + i * 0.3f) * 0.5f;
        band_energy += variation;

        // Scale to 0-15
        band_energy = fmaxf(0.0f, fminf(4.0f, band_energy));
        bands[i] = (uint8_t)(band_energy * 3.75f);
        bands[i] = bands[i] > 15 ? 15 : bands[i];
    }
}

// Function to process MP3 file and create corresponding TXT file
static esp_err_t process_mp3_file(const char *mp3_path) {
    char txt_path[256];
    char line_buffer[64];

    // Create TXT file path by replacing .mp3 with .txt
    strcpy(txt_path, mp3_path);
    char *ext = strrchr(txt_path, '.');
    if (ext) {
        strcpy(ext, ".txt");
    } else {
        strcat(txt_path, ".txt");
    }

    ESP_LOGI(TAG, "Processing %s -> %s", mp3_path, txt_path);

    // Check if output file exists and remove it to avoid conflicts
    struct stat st;
    if (stat(txt_path, &st) == 0) {
        ESP_LOGI(TAG, "Removing existing output file: %s", txt_path);
        if (unlink(txt_path) != 0) {
            ESP_LOGE(TAG, "Failed to remove existing file: %s", txt_path);
            return ESP_FAIL;
        }
    }

    // Read MP3 file once and analyze all samples
    FILE *mp3_file = fopen(mp3_path, "rb");
    if (mp3_file == NULL) {
        ESP_LOGE(TAG, "Failed to open MP3 file for reading: %s", mp3_path);
        return ESP_FAIL;
    }

    // Get file size
    fseek(mp3_file, 0, SEEK_END);
    long file_size = ftell(mp3_file);
    fseek(mp3_file, 0, SEEK_SET);

    if (file_size <= 0) {
        ESP_LOGE(TAG, "Invalid MP3 file size: %ld", file_size);
        fclose(mp3_file);
        return ESP_FAIL;
    }

    // Allocate buffer for MP3 data (read up to 64KB to get a good representation of the song)
    size_t buffer_size = (file_size > 65536) ? 65536 : file_size;
    uint8_t *mp3_buffer = malloc(buffer_size);
    if (mp3_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for MP3 buffer");
        fclose(mp3_file);
        return ESP_FAIL;
    }

    // Read MP3 data
    size_t bytes_read = fread(mp3_buffer, 1, buffer_size, mp3_file);
    fclose(mp3_file);

    if (bytes_read == 0) {
        ESP_LOGE(TAG, "Failed to read MP3 file data");
        free(mp3_buffer);
        return ESP_FAIL;
    }

    // Open output file
    FILE *f = fopen(txt_path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", txt_path);
        free(mp3_buffer);
        return ESP_FAIL;
    }

    // Estimate song duration based on file size (assuming ~128kbps MP3)
    // Duration = (file_size_bytes * 8) / (bitrate_bps)
    // For 128kbps: bitrate_bps = 128 * 1024 = 131072
    float estimated_bitrate = 128 * 1024; // 128kbps in bits per second
    float duration_seconds = (file_size * 8.0f) / estimated_bitrate;

    // Ensure minimum duration of 10 seconds and maximum of 120 seconds (2 minutes) to prevent stack overflow
    if (duration_seconds < 10.0f) duration_seconds = 10.0f;
    if (duration_seconds > 120.0f) duration_seconds = 120.0f;

    // Calculate total samples for the full song duration, but limit to maximum 50 samples to prevent stack overflow
    const int total_samples = (int)(duration_seconds * SAMPLES_PER_SECOND);
    const int max_samples = 50; // Reduced to prevent stack overflow
    const int actual_samples = (total_samples > max_samples) ? max_samples : total_samples;

    ESP_LOGI(TAG, "Estimated song duration: %.1f seconds (%d samples at %.1f Hz, limited to %d max)",
             duration_seconds, total_samples, (float)SAMPLES_PER_SECOND, max_samples);

    for (int sample = 0; sample < actual_samples; sample++) {
        uint8_t bands[NUM_BANDS];

        // Analyze different portions of the song based on sample number
        // This simulates the song progressing over time
        size_t chunk_offset = (sample * bytes_read) / total_samples;
        size_t chunk_size = bytes_read / total_samples;
        if (chunk_offset + chunk_size > bytes_read) {
            chunk_size = bytes_read - chunk_offset;
        }

        analyze_mp3_frequency_bands_from_buffer_chunk(mp3_buffer + chunk_offset, chunk_size, bands);

        // Write bands separated by spaces
        int pos = 0;
        for (int i = 0; i < NUM_BANDS; i++) {
            if (i > 0) {
                pos += sprintf(line_buffer + pos, " ");
            }
            pos += sprintf(line_buffer + pos, "%d", bands[i]);
        }

        // Write to file with error checking
        if (fprintf(f, "%s\n", line_buffer) < 0) {
            ESP_LOGE(TAG, "Failed to write to file at sample %d", sample);
            fclose(f);
            free(mp3_buffer);
            return ESP_FAIL;
        }

        // Yield to prevent watchdog timeout (more frequent yields)
        if (sample % 5 == 0) { // Yield every 5 samples
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    if (fclose(f) != 0) {
        ESP_LOGE(TAG, "Failed to close output file: %s", txt_path);
        free(mp3_buffer);
        return ESP_FAIL;
    }

    free(mp3_buffer);

    ESP_LOGI(TAG, "Created frequency analysis file: %s (%.1f seconds, %d samples at %.1f Hz)",
             txt_path, duration_seconds, actual_samples, (float)SAMPLES_PER_SECOND);
    return ESP_OK;
}

static esp_err_t scan_mp3_files(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s", dir_path);
        return ESP_FAIL;
    }

    struct dirent *entry;
    int mp3_count = 0;
    while ((entry = readdir(dir)) != NULL) {
        // Skip current directory and parent directory entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Check if it's a regular file using stat
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISREG(st.st_mode)) {
            const char *filename = entry->d_name;
            ESP_LOGI(TAG, "Found file: %s", filename);

            const char *ext = strrchr(filename, '.');
            if (ext && strcasecmp(ext, ".mp3") == 0) {
                // Check if output file would conflict with existing files
                char check_path[512];
                snprintf(check_path, sizeof(check_path), "%s/%s", dir_path, filename);
                char *check_ext = strrchr(check_path, '.');
                if (check_ext) {
                    strcpy(check_ext, ".txt");
                }

                // Skip if output file already exists (avoid conflicts)
                if (stat(check_path, &st) == 0) {
                    ESP_LOGW(TAG, "Skipping %s - output file %s already exists", filename, check_path);
                    continue;
                }

                ESP_LOGI(TAG, "Processing MP3 file: %s", filename);
                process_mp3_file(full_path);
                mp3_count++;
            }
        }
    }

    closedir(dir);
    ESP_LOGI(TAG, "Scan complete. Found %d MP3 files.", mp3_count);
    return ESP_OK;
}

void app_main(void)
{
    esp_err_t ret;

    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif // EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    sdmmc_card_t *card;
    const char mount_point[] = MOUNT_POINT;
    ESP_LOGI(TAG, "Initializing SD card");

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
    // Please check its source code and implement error recovery when developing
    // production applications.
    ESP_LOGI(TAG, "Using SPI peripheral");

    // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
    // For setting a specific frequency, use host.max_freq_khz (range 400kHz - 20MHz for SDSPI)
    // Example: for fixed frequency of 10MHz, use host.max_freq_khz = 10000;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    // For SoCs where the SD power can be supplied both via an internal or external (e.g. on-board LDO) power supply.
    // When using specific IO pins (which can be used for ultra high-speed SDMMC) to connect to the SD card
    // and the internal LDO power supply, we need to initialize the power supply first.
#if CONFIG_EXAMPLE_SD_PWR_CTRL_LDO_INTERNAL_IO
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = CONFIG_EXAMPLE_SD_PWR_CTRL_LDO_IO_ID,
    };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;

    ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create a new on-chip LDO power control driver");
        return;
    }
    host.pwr_ctrl_handle = pwr_ctrl_handle;
#endif

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return;
    }

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set the CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
#ifdef CONFIG_EXAMPLE_DEBUG_PIN_CONNECTIONS
            check_sd_card_pins(&config, pin_count);
#endif
        }
        return;
    }
    ESP_LOGI(TAG, "Filesystem mounted");

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    // Use POSIX and C standard library functions to work with files.

    // Log frequency band configuration
    log_frequency_bands();

    // Scan for MP3 files and process them
    ESP_LOGI(TAG, "Scanning for MP3 files...");
    ret = scan_mp3_files(MOUNT_POINT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to scan MP3 files");
        return;
    }

    // All done, unmount partition and disable SPI peripheral
    esp_vfs_fat_sdcard_unmount(mount_point, card);
    ESP_LOGI(TAG, "Card unmounted");

    //deinitialize the bus after all devices are removed
    spi_bus_free(host.slot);

    // Deinitialize the power control driver if it was used
#if CONFIG_EXAMPLE_SD_PWR_CTRL_LDO_INTERNAL_IO
    ret = sd_pwr_ctrl_del_on_chip_ldo(pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete the on-chip LDO power control driver");
        return;
    }
#endif
}
