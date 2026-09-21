file(GLOB_RECURSE LVGL_SOURCES "lvgl/src/*.c")
idf_component_register(SRCS ${LVGL_SOURCES} INCLUDE_DIRS "lvgl" "../..")
target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_CONF_INCLUDE_SIMPLE)
