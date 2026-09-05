set(YAOGUI_GENERATED_DIR "${CMAKE_CURRENT_LIST_DIR}/../../generated")
set(YAOGUI_GENERATED_SRCS
    "${YAOGUI_GENERATED_DIR}/yaogui_font_14.c"
    "${YAOGUI_GENERATED_DIR}/yaogui_classic_14.c"
    "${YAOGUI_GENERATED_DIR}/yaogui_mifu_18.c"
    "${YAOGUI_GENERATED_DIR}/yaogui_coin_sound.c"
    "${YAOGUI_GENERATED_DIR}/yaogui_ambient_sound.c"
    "${YAOGUI_GENERATED_DIR}/yaogui_shell_images.c"
    "${YAOGUI_GENERATED_DIR}/yaogui_coin_images.c"
    "${YAOGUI_GENERATED_DIR}/yaogui_table_image.c"
)

foreach(generated_source IN LISTS YAOGUI_GENERATED_SRCS)
    if(NOT EXISTS "${generated_source}")
        message(FATAL_ERROR
            "Missing generated assets. Run: npm ci && "
            "python3 -m pip install -r requirements-assets.txt && "
            "python3 tools/generate_assets.py")
    endif()
endforeach()

idf_component_register(
    SRCS "ui_pixel.c"
         "yaogui_logic.c"
         "yaogui_text_data.c"
         ${YAOGUI_GENERATED_SRCS}
         "yaogui_view.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl
)
