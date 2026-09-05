idf_component_register(
    SRCS "main.c"
         "ui_pixel_math.c"
         "yaogui_app.c"
    INCLUDE_DIRS "."
    REQUIRES bsp yaogui_view esp_timer esp_wifi
)
