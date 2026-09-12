set(YAOGUI_GENERATED_DIR "${CMAKE_CURRENT_LIST_DIR}/../../generated")
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/yaogui_generated_sources.cmake")
yaogui_collect_generated_sources(YAOGUI_GENERATED_SRCS "${YAOGUI_GENERATED_DIR}")

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
         "yaogui_standby.c"
         ${YAOGUI_GENERATED_SRCS}
         "yaogui_view.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl
)
