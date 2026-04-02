#ifndef VISUALISER_H
#define VISUALISER_H

#include <stdbool.h>

void visualiser_init(void);
void visualiser_preprocess_file(const char* mp3_url);
void visualiser_start(const char* url);
void visualiser_stop(void);
void visualiser_task(void *pvParameters);
bool visualiser_is_running(void);

#endif // VISUALISER_H