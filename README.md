# Frequency Spectrographical Visualizer (FSV)

This repository contains the firmware for the Frequency Spectrographical Visualizer (FSV). The project plays MP3 files and physically visualizes their audio. It extracts seven main equalization bands from the audio and physically represents their amplitudes by levitating seven ping pong balls inside vertical cylinders using fan-generated air pressure.

## Hardware Overview
The hardware is fully pre-assembled for the user. The system operates on two ESP32-S3 microcontrollers communicating via the ESP-NOW protocol. Internal components include:
* **Actuators & Sensors:** Seven 12V Sunon fans and VL6180X Time-of-Flight (ToF) sensors regulated by a custom PID controller to stabilize the ping pong balls.
* **Audio Pipeline:** A PCM5102A DAC (I2S) for clean audio conversion and a PAM8610 amplifier driving two passive speakers.
* **Display:** A 5-inch touchscreen for the graphical user interface.

Please refer to the electrical schematics for detailed wiring if maintenance is required.

## Installation Guide

### Prerequisites
To build and flash the firmware, your system must have the following environments installed:
* ESP-IDF (Espressif IoT Development Framework) - For FreeRTOS and core system functions.
* ESP-ADF (Espressif Audio Development Framework) - For the audio processing pipeline.

Refer to the official Espressif documentation for setup instructions.

### Configuration
**Critical Warning:** It is mandatory to use the `sdkconfig` files for `menuconfig` provided in this repository. These files contain highly specific project settings (such as custom partition tables, FreeRTOS task priorities, and peripheral configurations) required for the system to function. Do not overwrite them with default configurations.

### Building and Flashing
The repository is split into two distinct projects. You must build and flash both to their respective ESP32-S3 microcontrollers.

1. **Audio and Processing Unit:**
   Navigate to the integration directory and flash the firmware:
   ```bash
   cd mp3-from-sd-and-bands-integration
   idf.py build flash monitor
   ```

2. **Display Unit:**
   Navigate to the display directory and flash the firmware:
   ```bash
   cd Scherm
   idf.py build flash monitor
   ```

## User Guide

### Preparation
1. Format a MicroSD card to the FAT32 file system.
2. Place your `.mp3` files directly in the root directory of the SD card.
3. Insert the SD card into the reader on the audio processing unit.

### Operation
1. **Power On:** Apply power to the system. The internal power supply will automatically step down the voltages required for the fans, amplifiers, and microcontrollers.
2. **Synchronization:** The display and audio units will automatically connect via ESP-NOW. 
3. **Controls:** Use the touchscreen to navigate your MP3 files, control playback, and view track information.
4. **Physical Visualization:** Upon playback, the system will pre-calculate the frequency spectrum after which you will see the seven ping pong balls dynamically rise and fall between 2 and 20 cm, accurately reflecting the audio amplitude of the 7 discrete frequency bands. 
5. **Standby:** If playback is paused or stopped for 5 minutes, the system will automatically enter a power-saving sleep mode. Touching the screen or hardware button will wake the system.

## Maintenance

### ToF Sensor Calibration
If the physical visualization becomes inaccurate (e.g., a ball does not reach the correct height or the sensor offset drifts), you must recalibrate the VL6180X sensors. Thanks to the PCA9548A multiplexer, **all sensors can remain connected** during this process.

The calibration code is found the the [VL810X Feature Branch](https://github.com/Joram-vV/frequency-spectrographical-visualiser/tree/feature/vl6180).

**Calibration Steps:**

1. **Configure Source:** Modify the `main/CMakeLists.txt` in the TOF directory to use the interactive calibration source file:
   ```cmake
   idf_component_register(SRCS "calibrate_single_vl6180.c"
                       INCLUDE_DIRS "."
                       REQUIRES vl6180 pca9548a)
   ```
2. **Build and Flash:** Run the following command in the terminal:
   ```bash
   idf.py build flash monitor
   ```
3. **Select Sensor:** When prompted in the terminal output, input the multiplexer channel (0-6) corresponding to the specific cylinder/sensor you wish to calibrate.
4. **Positioning:** Place a reflecting object (such as a piece of white paper or a ping pong ball) exactly **50mm** away from the selected sensor.
5. **Observation:** Let the script run and monitor the terminal output for the newly determined offset value. 
6. **Repeat:** Restart the process or select the next channel to calibrate the remaining sensors one by one.
