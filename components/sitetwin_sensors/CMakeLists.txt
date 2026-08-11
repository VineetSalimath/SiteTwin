idf_component_register(SRCS "src/sht41.c" "src/bh1750.c" "src/reed.c" "src/ina219.c"
                            "src/pir.c" "src/adxl345.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sitetwin_core sitetwin_sensor_runtime)
