idf_component_register(
    SRCS "main.c"
         "yaogui_app.c"
         "yaogui_network_policy.c"
         "yaogui_time_sync.c"
         "fap_screenshot.c"
    INCLUDE_DIRS "."
    REQUIRES bsp driver yaogui_view esp_app_format esp_event esp_http_server
             esp_netif esp_timer esp_wifi lwip nvs_flash
)

if(NOT CMAKE_SCRIPT_MODE_FILE)
    set(WEB_ASSET_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/yaogui_web_assets.c")
    add_custom_command(
        OUTPUT "${WEB_ASSET_SOURCE}"
        COMMAND "${PYTHON}"
                "${CMAKE_CURRENT_LIST_DIR}/../tools/build_web_assets.py"
                "${WEB_ASSET_SOURCE}"
                "${CMAKE_CURRENT_LIST_DIR}/portal.html"
                "${CMAKE_CURRENT_LIST_DIR}/divination.html"
        DEPENDS
            "${CMAKE_CURRENT_LIST_DIR}/../tools/build_web_assets.py"
            "${CMAKE_CURRENT_LIST_DIR}/portal.html"
            "${CMAKE_CURRENT_LIST_DIR}/divination.html"
        COMMENT "Minifying and compressing embedded web pages"
        VERBATIM
    )
    set_source_files_properties("${WEB_ASSET_SOURCE}" PROPERTIES GENERATED TRUE)
    target_sources("${COMPONENT_LIB}" PRIVATE "${WEB_ASSET_SOURCE}")
endif()
