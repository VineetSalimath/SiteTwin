idf_component_register(SRCS "src/sht41.c"
                            "src/scd41.c"
                            "src/sgp40.c"
                            "src/voc_index_algorithm.c"
                            "third_party/sensirion_gas_index_algorithm/sensirion_gas_index_algorithm.c"
                       INCLUDE_DIRS "include"
                                    "third_party/sensirion_gas_index_algorithm"
                       REQUIRES sitetwin_core sitetwin_sensor_runtime)
