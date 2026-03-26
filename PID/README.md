# PID Fan Controller (ESP32-S3 + VL6180)

This project controls a 4-wire fan with a PID loop based on distance measurements from a VL6180 ToF sensor.

## What It Does

- Reads distance from the VL6180 over I2C.
- Runs a control loop every 20 ms.
- Computes PID output and maps it to fan PWM (0-100%).
- Sweeps the PID setpoint every 1 second through:
  - `14, 32, 56, 79, 100, 115, 126, 135`
  - Then back down: `126, 115, ... , 14`
  - Repeats (ping-pong pattern).

## Hardware / Pins

- Fan PWM output: `GPIO2`
- I2C SCL: `GPIO5`
- I2C SDA: `GPIO6`
- VL6180 reset pin: `GPIO4`
- PWM: `25 kHz`, `10-bit`

## Build

```bash
source ~/.espressif/v5.5.3/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
```

## Flash + Monitor

```bash
idf.py -p <PORT> flash monitor
```

Exit monitor with `Ctrl-]`.

## Runtime Logs

You should see logs like:

- `Distance: ... mm`
- `setpoint: ...`
- `pid raw output = ...`

## Tuning

PID defaults are in `main/main.c`:

- `Kp = 1.2`
- `Ki = 0.4`
- `Kd = 0.05`

If control is too aggressive, lower `Kp` first, then `Ki`.
