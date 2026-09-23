idf_component_register(SRCS "main.c" "runtime_monitor.c" "display_perf.c" "frame_trace.c" "physical_display.c" "management.c" "wifi_service.c" "ble_service.c" "motion_service.c" "wallpaper_service.c" "wallpaper_store.c" "ui/badge_ui.c" "ui/ui_transition_cache.c" "ui/ui_transition_compositor.c" "ui/wifi_panel.c" "ui/assets.c" "ui/apps.c" "ui/grok.c" "ui/miner.c" "ui/app_assets.c" "app_service.c" "map_png.c" "pngle/miniz.c"
    INCLUDE_DIRS "." "ui" ".."
    REQUIRES esp_lvgl_adapter esp_lcd_st77916 esp_lcd_touch esp_lcd esp_http_client mbedtls lvgl esp_partition esp_http_server esp_driver_spi esp_driver_ledc esp_driver_gpio esp_driver_i2c esp_driver_usb_serial_jtag esp_timer esp_psram esp_adc nvs_flash esp_wifi esp_event esp_netif bt)

target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=lv_timer_handler")

if(DEFINED ENV{CABADGE_DISPLAY_PERF} AND "$ENV{CABADGE_DISPLAY_PERF}" STREQUAL "1")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CABADGE_DISPLAY_PERF=1 CABADGE_TRANSITION_COMPOSITOR_PERF=1)
    target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=lv_display_flush_ready,--wrap=lv_timer_create,--wrap=lv_timer_delete,--wrap=spi_bus_add_device,--wrap=spi_device_queue_trans,--wrap=spi_device_get_trans_result")
endif()

if(DEFINED ENV{CABADGE_TRANSITION_CACHE_DEBUG} AND "$ENV{CABADGE_TRANSITION_CACHE_DEBUG}" STREQUAL "1")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CABADGE_TRANSITION_CACHE_DEBUG=1)
endif()
