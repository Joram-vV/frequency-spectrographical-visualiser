#ifndef AUDIO_CONTROL_H
#define AUDIO_CONTROL_H

#include "audio_pipeline.h"

void audio_control_init(void);
void audio_control_play_track(const char *url);
void audio_control_pause(void);
void audio_control_resume(void);
void audio_control_stop(void);
int get_url_from_filename(char *filename, char * url_buff, int buff_size);

audio_pipeline_handle_t audio_control_get_pipeline(void);
audio_element_handle_t audio_control_get_i2s_writer(void);
audio_element_handle_t audio_control_get_mp3_decoder(void);
audio_element_handle_t audio_control_get_rsp_handle(void);

#endif // AUDIO_CONTROL_H