idf_component_register(SRCS "radio.c" "pulse.c" "pn532.c" INCLUDE_DIRS "include"
    REQUIRES div_board esp_driver_rmt esp_timer)
