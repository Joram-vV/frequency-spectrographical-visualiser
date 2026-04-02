#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Define how many times per second the visualizer updates the fan targets
#define TARGET_VISUALIZER_FPS 5  // Change this to 1, 2, 5, etc.

// Number of frequency bands / fans
#define NUM_BANDS 7

// The shared array holding the target heights (0-8) for the 7 fans
extern float shared_target_heights[NUM_BANDS];

// The Mutex to protect the shared array
extern SemaphoreHandle_t shared_state_mutex;

#endif // SHARED_STATE_H