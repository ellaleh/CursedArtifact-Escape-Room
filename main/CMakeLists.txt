idf_component_register(
    SRCS "main.c" "puzzles.c" "room_session.c" "room_net.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_adc esp_lcd esp_timer esp_lvgl_port lvgl esp_lcd_ili9341
             nvs_flash esp_wifi esp_netif esp_http_server esp_event
)
