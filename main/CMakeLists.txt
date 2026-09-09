idf_component_register(
    SRCS "main.c"
         "ui_pixel_math.c"
         "yaogui_app.c"
         "yaogui_time_sync.c"
         "fap_screenshot.c"
    INCLUDE_DIRS "."
    REQUIRES bsp driver yaogui_view esp_app_format esp_event esp_http_server
             esp_netif esp_timer esp_wifi nvs_flash
)
