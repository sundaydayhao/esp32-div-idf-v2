idf_component_register(SRCS "storage.c" INCLUDE_DIRS "include"
    REQUIRES div_board nvs_flash fatfs sdmmc esp_driver_sdspi app_update cjson)
