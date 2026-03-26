# Frequency Spectrographical Visualiser (FSV)
## What?
The codebase for a musicplayer that uses in world visuals - for instance moving bars and lights - to visualise the frequency spectrogram of the music it plays.


### Feature VL6180
Custom VL6180 library in components/vl6180

#### Main codes
Change the main CMakeList's source to:

- main.c    (code using the VL6180 in single shot mode an reading range)
- probe.c   (code used for probing different addresses on the i2c bus)


