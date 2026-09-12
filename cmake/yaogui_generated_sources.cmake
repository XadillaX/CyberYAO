set(YAOGUI_GENERATED_BASENAMES
    yaogui_font_14.c
    yaogui_classic_14.c
    yaogui_mifu_18.c
    yaogui_clock_28.c
    yaogui_standby_display_12.c
    yaogui_standby_calendar_10.c
    yaogui_standby_pixel_10.c
    yaogui_coin_sound.c
    yaogui_ambient_sound.c
    yaogui_shell_images.c
    yaogui_coin_images.c
    yaogui_table_image.c
    yaogui_standby_images.c
    yaogui_calendar_data.c
)

function(yaogui_collect_generated_sources output_var generated_dir)
    set(sources)
    foreach(basename IN LISTS YAOGUI_GENERATED_BASENAMES)
        list(APPEND sources "${generated_dir}/${basename}")
    endforeach()
    set("${output_var}" "${sources}" PARENT_SCOPE)
endfunction()
