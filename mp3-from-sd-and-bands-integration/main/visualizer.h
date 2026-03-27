#ifndef VISUALIZER_H
#define VISUALIZER_H

#include <stdbool.h>

void visualizer_init(void);
void visualizer_preprocess_file(const char* mp3_url);
void visualizer_start(const char* url);
void visualizer_stop(void);
void visualizer_task(void *pvParameters);
bool visualizer_is_running(void);

#endif // VISUALIZER_H