idf_component_register(SRCS "ui.c" "menu.c" "ui_assets.c" "touch.c" "waterfall.c" INCLUDE_DIRS "include"
    REQUIRES div_board div_storage div_services div_spectrum esp_lcd esp_lvgl_port esp_lcd_ili9341 esp_lcd_st7796 esp_lcd_touch_xpt2046)
