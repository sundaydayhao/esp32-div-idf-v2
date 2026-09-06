idf_component_register(SRCS "ui.c" INCLUDE_DIRS "include"
    REQUIRES div_board div_storage div_services esp_lcd esp_lvgl_port esp_lcd_ili9341 esp_lcd_st7796 esp_lcd_touch_xpt2046)
