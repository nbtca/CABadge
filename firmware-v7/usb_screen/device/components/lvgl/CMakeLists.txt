file(GLOB_RECURSE LVGL_SOURCES "lvgl/src/*.c")
idf_component_register(SRCS ${LVGL_SOURCES} INCLUDE_DIRS "lvgl" "../.." REQUIRES freertos)
target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_CONF_INCLUDE_SIMPLE)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LODEPNG_NO_COMPILE_ALLOCATORS LODEPNG_NO_COMPILE_ENCODER)
