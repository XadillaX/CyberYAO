idf_component_register(
    SRCS "ui_pixel.c"
         "yaogui_logic.c"
         "yaogui_text_data.c"
         "yaogui_font_14.c"
         "yaogui_font_16.c"
         "yaogui_classic_14.c"
         "yaogui_mifu_18.c"
         "yaogui_coin_sound.c"
         "yaogui_ambient_sound.c"
         "yaogui_shell_images.c"
         "yaogui_coin_images.c"
         "yaogui_table_image.c"
         "yaogui_view.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl
)
