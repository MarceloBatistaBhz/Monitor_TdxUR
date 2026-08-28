idf_component_register(SRCS "app_principal.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_driver_uart esp_timer sht4x psicrometria registro
                                ui waveshare__esp32_p4_wifi6_touch_lcd_7b esp_lvgl_port lvgl__lvgl)
