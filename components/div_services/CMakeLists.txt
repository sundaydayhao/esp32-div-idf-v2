idf_component_register(SRCS "service.c" "catalog.c" "wifi.c" "ble.c" "rf.c" "ir.c" "nfc.c" "gps.c" "system.c" "ducky.c" "pulse_profile.c"
    INCLUDE_DIRS "include" REQUIRES div_board div_storage div_protocol div_drivers div_spectrum minmea
    bt esp_wifi esp_netif esp_event esp_http_server esp_timer esp_driver_uart
    lwip app_update esp_driver_tsens cjson console)
