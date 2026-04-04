# Frequency Spectrographical Visualiser (FSV)
## What?
The codebase for a musicplayer that uses in world visuals - for instance moving bars and lights - to visualise the frequency spectrogram of the music it plays.


### Feature VL6180
Custom VL6180 library in components/vl6180

#### Main codes
Change the main CMakeList's source to:

- main.c    (code using the VL6180 in single shot mode an reading range)
- probe.c   (code used for probing different addresses on the i2c bus)


#### Offset Calibration
E.g. When the sensor only starts measuring from 50mm and only reads 0 when an object is closer than 50mm.

*For more possible causes view the VL6180's documentation (AN4545 & Datasheet)*

Calibration steps:
1. Connect the SDA, SCL, GND and 3.3V pins to the VL6180. Connect the RESET GPIO to GND or to another available GPIO *(recommended for a complete setup of the VL6180)*, in which case the following should be added in calibrate_single_vl6180.c:
```c
  vl6180_config.reset_gpio = GPIO_NUM_XX;
```

2. Set [CMakeLists.txt](./main/CMakeLists.txt) to:

```cmake
idf_component_register(SRCS "calibrate_single_vl6180.c"
                    INCLUDE_DIRS "."
                    REQUIRES vl6180 pca9548a)
```
2. Place an object with >18% reflectance (such as white paper) 150mm away from the sensor
3. Run ```idf.py build flash monitor``` in the ESP-IDF Terminal
4. Repeat for other VL6180 sensors, remove the previously calibrated VL6180 from the I2C bus before running the calibration on the next.

*[Note]: Only put 1 VL6180 sensor on the I2C bus at the same time!*