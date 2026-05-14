idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES nvs_flash esp_wifi esp_netif esp_event esp32_p4_function_ev_board bsp_extra lvgl__lvgl espressif__esp_lvgl_port esp_driver_jpeg esp_lcd esp_timer lwip)
