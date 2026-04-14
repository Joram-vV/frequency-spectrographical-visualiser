#include "audio_control.h"
#include "audio_element.h"
#include "sdcard_list.h"
#include "visualizer.h"
#include <string.h>
#include "esp_log.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"
#include "filter_resample.h"
#include "sys/stat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
 
#define FAN_INERTIA_DELAY_MS 700
 

static audio_pipeline_handle_t pipeline;
static audio_element_handle_t i2s_stream_writer, mp3_decoder, fatfs_stream_reader, rsp_handle;

void audio_control_init(void) {
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);

    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;   
    fatfs_stream_reader = fatfs_stream_init(&fatfs_cfg);

    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_decoder = mp3_decoder_init(&mp3_cfg);

    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_handle = rsp_filter_init(&rsp_cfg);

    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);
    i2s_stream_set_clk(i2s_stream_writer, 48000, 16, 2);

    audio_pipeline_register(pipeline, fatfs_stream_reader, "file");
    audio_pipeline_register(pipeline, mp3_decoder, "mp3");
    audio_pipeline_register(pipeline, rsp_handle, "filter");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

    const char *link_tag[4] = {"file", "mp3", "filter", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 4);
}

static const uint8_t SYNC_HEADER0 = 0xFF;
static const uint8_t SYNC_HEADER1 = 0xE0;
static const uint8_t VERSION_AND_LAYER_MASK = 0x1E;
enum V_L_COMBINATIONS {
	V1_L1 = 0x1E,
	V1_L2 = 0x1C,
	V1_L3 = 0x1A,
	V2_L1 = 0x16,
	V2_L2 = 0x14,
	V2_L3 = 0x12,
};

int calculate_mp3_duration_us(const char *filepath) {
	struct stat st;
	if (stat(filepath, &st) != 0) {
		printf("\n[ WARN ] Filepath '%s' not valid.\n", filepath);
		return 0;
	}

	long total_bytes = st.st_size;
	int bitrate = 0;

	FILE *f = fopen(filepath, "rb");
	if (f) {
		uint8_t header[4];
		// Skip ID3 tag and find first sync word
		bool found = false;
		while (fread(header, 1, 1, f)) {
			if (header[0] == SYNC_HEADER0) {
				fread(&header[1], 1, 3, f);
				if ((header[1] & SYNC_HEADER1) == SYNC_HEADER1) { // Found Sync
					found = true;
					switch (header[1] & VERSION_AND_LAYER_MASK) {
						case V1_L1: {
							int bitrate_index = (header[2] >> 4) & 0x0F;
							// Standard MPEG1 Layer 1 Table
							static const int bitrates[] = {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0};
							bitrate = bitrates[bitrate_index];
							break;
						}
						case V1_L2: {
							int bitrate_index = (header[2] >> 4) & 0x0F;
							// Standard MPEG1 Layer 2 Table
							static const int bitrates[] = {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0};
							bitrate = bitrates[bitrate_index];
							break;
						}
						case V1_L3: {
							int bitrate_index = (header[2] >> 4) & 0x0F;
							// Standard MPEG1 Layer 3 Table
							static const int bitrates[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
							bitrate = bitrates[bitrate_index];
							break;
						}
						case V2_L1: {
							int bitrate_index = (header[2] >> 4) & 0x0F;
							// Standard MPEG2 Layer 1 Table
							static const int bitrates[] = {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0};
							bitrate = bitrates[bitrate_index];
							break;
						}
						case V2_L2:
						case V2_L3: {
							int bitrate_index = (header[2] >> 4) & 0x0F;
							// Standard MPEG2 Layer 2 & 3 Table
							static const int bitrates[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
							bitrate = bitrates[bitrate_index];
							break;
						}
						default:
							break;
					}
				}
			}
			if (found) break;
		}
		fclose(f);
	}


	if (bitrate <= 0) {
		printf("\n[ WARN ] Bitrate is 0.\n");
		bitrate = 128; //fallback bitrate
	}

	printf("\n[ INFO ] \nBitrate: %d, total bytes: %ld, duration: %d\n", bitrate, total_bytes, (int)((total_bytes * 8) / bitrate));

	return (int)((total_bytes * 8) / bitrate);
}

static int32_t duration = 0;

void audio_control_play_track(const char *url) {
    if (url == NULL) {
        printf("\n[ ERROR ] No track URL provided or playlist is empty.\n");
        return;
    }

    visualizer_stop();
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);

    const char *filename = strrchr(url, '/');
    filename = filename ? filename + 1 : url;
    printf("\n[ INFO ] Loading: %s\n", filename);

    visualizer_preprocess_file(url);

	// skip 'file:/', get ms
	duration = calculate_mp3_duration_us(url + 6) / 1000;

    audio_element_set_uri(fatfs_stream_reader, url);
    audio_pipeline_reset_ringbuffer(pipeline);
    audio_pipeline_reset_elements(pipeline);
    audio_pipeline_change_state(pipeline, AEL_STATE_INIT);

    visualizer_start(url);
    vTaskDelay(pdMS_TO_TICKS(FAN_INERTIA_DELAY_MS));
    audio_pipeline_run(pipeline);
}

void audio_control_pause(void) {
	visualizer_pause();
    audio_pipeline_pause(pipeline);
    printf("\n[ INFO ] Paused.\n");
}

void audio_control_resume(void) {
    if (audio_pipeline_resume(pipeline) != ESP_OK) {
		printf("\n[ INFO ] Could not resume audio.\n");
		return;
	}
	visualizer_resume();
    printf("\033[2J");
    printf("\n[ INFO ] Resumed.\n");
    // visualizer state will be restored if needed via state vars.
}

void audio_control_stop(void) {
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    visualizer_stop();
	duration = 0;
    printf("\n[ INFO ] Stopped.\n");
}

int get_url_from_filename(char *filename, char * url_buff, int buff_size){
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "/sdcard/%s", filename);
    FILE *f = fopen(filepath, "r");
    if (f) {
        fclose(f);
        snprintf(url_buff, buff_size, "file://sdcard/%s", filename);
        return 0;
    } else {
        printf("\n[ERROR] File not found: %s\n", filename);
    }
    return 1;
}

audio_pipeline_handle_t audio_control_get_pipeline(void) { return pipeline; }
audio_element_handle_t audio_control_get_i2s_writer(void) { return i2s_stream_writer; }
audio_element_handle_t audio_control_get_fatfs_reader(void) { return fatfs_stream_reader; }
audio_element_handle_t audio_control_get_mp3_decoder(void) { return mp3_decoder; }
audio_element_handle_t audio_control_get_rsp_handle(void) { return rsp_handle; }

int32_t get_song_duration(void) { return duration; }