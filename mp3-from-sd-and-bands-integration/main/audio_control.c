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

    audio_element_set_uri(fatfs_stream_reader, url);
    audio_pipeline_reset_ringbuffer(pipeline);
    audio_pipeline_reset_elements(pipeline);
    audio_pipeline_change_state(pipeline, AEL_STATE_INIT);

    visualizer_start(url);
    audio_pipeline_run(pipeline);
}

void audio_control_pause(void) {
    audio_pipeline_pause(pipeline);
    visualizer_stop();
    printf("\n[ INFO ] Paused.\n");
}

void audio_control_resume(void) {
    audio_pipeline_resume(pipeline);
    printf("\033[2J");
    printf("\n[ INFO ] Resumed.\n");
    // visualizer state will be restored if needed via state vars.
}

void audio_control_stop(void) {
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    visualizer_stop();
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
audio_element_handle_t audio_control_get_mp3_decoder(void) { return mp3_decoder; }
audio_element_handle_t audio_control_get_rsp_handle(void) { return rsp_handle; }