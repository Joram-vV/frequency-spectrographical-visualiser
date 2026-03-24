#include "pid_controller.h"

void pid_init(PIDController* pid, float Kp, float Ki, float Kd, float min_out, float max_out) {
    pid->Kp = Kp;
    pid->Ki = Ki;
    pid->Kd = Kd;
    pid->integral = 0;
    pid->prev_error = 0;
    pid->output_min = min_out;
    pid->output_max = max_out;
}

float pid_compute(PIDController* pid, float input, float dt) {
    // Reverse-acting loop for this plant:
    // more output (fan) should reduce measured distance.
    float error = input - pid->setpoint;
    pid->integral += error * dt;
    float derivative = (error - pid->prev_error) / dt;

    float output = pid->Kp * error + pid->Ki * pid->integral + pid->Kd * derivative;

    if (output > pid->output_max) output = pid->output_max;
    if (output < pid->output_min) output = pid->output_min;

    pid->prev_error = error;
    return output;
}
