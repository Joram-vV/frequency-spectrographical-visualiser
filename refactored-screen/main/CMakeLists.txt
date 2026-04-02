idf_component_register(
    SRCS
        "espnow_transport.c"
        "lvgl_use.c"
        "now_playing_ui.c"
        "playback_controls_ui.c"
        "volume_ui.c"
        "main.c"
        
    INCLUDE_DIRS
        "."
)

# LVGL's sources include "lv_conf.h" directly, so expose main/ to the LVGL component too.
idf_build_get_property(build_components BUILD_COMPONENTS)
if("lvgl__lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_component_lib lvgl__lvgl COMPONENT_LIB)
    target_include_directories(${lvgl_component_lib} PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
elseif("lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_component_lib lvgl COMPONENT_LIB)
    target_include_directories(${lvgl_component_lib} PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
endif()
