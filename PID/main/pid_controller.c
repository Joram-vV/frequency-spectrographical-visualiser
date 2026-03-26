#include "pid_controller.h"
#include <stdio.h>

void pid_init(PIDController* pid, float Kp, float Ki, float Kd, float min_out, float max_out) {
    pid->Kp = Kp;
    pid->Ki = Ki;
    pid->Kd = Kd;
    pid->integral = 0;
    pid->prev_error = 0;
    pid->output_min = min_out;
    pid->output_max = max_out;
}

float pid_compute(PIDController* pid, int input, float dt) {
    if (dt <= 0.0f) {
        return 0.0f;
    }

    // Reverse-acting loop for this plant:
    // more output (fan) should reduce measured distance.
    float error = input - pid->setpoint;
    pid->integral += error * dt;

    // Prevent integral windup from dominating the output.
    const float integral_limit = 200.0f;
    if (pid->integral > integral_limit) pid->integral = integral_limit;
    if (pid->integral < -integral_limit) pid->integral = -integral_limit;

    float derivative = (error - pid->prev_error) / dt;

    float output = pid->Kp * error + pid->Ki * pid->integral + pid->Kd * derivative;

    if (output > pid->output_max) output = pid->output_max;
    if (output < pid->output_min) output = pid->output_min;

    pid->prev_error = error;
    return output;
}
