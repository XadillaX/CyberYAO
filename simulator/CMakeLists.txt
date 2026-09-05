cmake_minimum_required(VERSION 3.20)
project(yaogui_simulator LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
set(LVGL_DIR "${ROOT_DIR}/managed_components/lvgl__lvgl")
set(VIEW_DIR "${ROOT_DIR}/components/yaogui_view")
set(GENERATED_DIR "${ROOT_DIR}/generated")

if(NOT EXISTS "${LVGL_DIR}/lvgl.h")
    message(FATAL_ERROR "缺少 managed_components/lvgl__lvgl，请先执行一次固件依赖解析")
endif()
if(NOT EXISTS "${GENERATED_DIR}/yaogui_font_14.c")
    message(FATAL_ERROR "缺少 generated/ 资源，请先执行 python3 tools/generate_assets.py")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${LVGL_DIR}" "${CMAKE_BINARY_DIR}/lvgl")

target_link_libraries(lvgl PUBLIC PkgConfig::SDL2)

add_executable(yaogui_simulator
    main.c
    mock_backend.c
    "${VIEW_DIR}/ui_pixel.c"
    "${VIEW_DIR}/yaogui_logic.c"
    "${VIEW_DIR}/yaogui_text_data.c"
    "${GENERATED_DIR}/yaogui_font_14.c"
    "${GENERATED_DIR}/yaogui_classic_14.c"
    "${GENERATED_DIR}/yaogui_mifu_18.c"
    "${GENERATED_DIR}/yaogui_coin_sound.c"
    "${GENERATED_DIR}/yaogui_ambient_sound.c"
    "${GENERATED_DIR}/yaogui_shell_images.c"
    "${GENERATED_DIR}/yaogui_coin_images.c"
    "${GENERATED_DIR}/yaogui_table_image.c"
    "${VIEW_DIR}/yaogui_view.c"
)
target_include_directories(yaogui_simulator PRIVATE
    "${VIEW_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}"
)
target_link_libraries(yaogui_simulator PRIVATE lvgl PkgConfig::SDL2)
target_compile_options(yaogui_simulator PRIVATE -Wall -Wextra -Wpedantic)
