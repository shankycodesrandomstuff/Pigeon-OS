idf_component_register(SRCS "runtime.c"
                    INCLUDE_DIRS "include"
                    REQUIRES hal lua fatfs esp_driver_sdspi esp_driver_spi)
