idf_component_register(SRCS "probe.c" "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES vl6180 pca9548a)
