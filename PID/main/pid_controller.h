typedef struct {
    float Kp;
    float Ki;
    float Kd;

    float setpoint;
    float integral;
    float prev_error;

    float output_min;
    float output_max;
} PIDController;

void pid_init(PIDController* pid, float Kp, float Ki, float Kd, float min_out, float max_out);
float pid_compute(PIDController* pid, float input, float dt);