#ifndef FAN_CONTROL_H
#define FAN_CONTROL_H

#include "driver/i2c_master.h"

// IMPORTANT: Set this to 1 for your current testing, change to 0 when the multiplexer arrives
#define SINGLE_FAN_TEST_MODE 0

void fan_control_init(void);
void fan_control_task(void *pvParameters);

#endif // FAN_CONTROL_H