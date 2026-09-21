idf_component_register(SRCS "main.c" "runtime_monitor.c" "physical_display.c" "display_worker.c" "management.c" "wifi_service.c" "ble_service.c" "motion_service.c" "wallpaper_service.c" "wallpaper_store.c" "ui/badge_ui.c" "ui/wifi_panel.c" "ui/assets.c"
    INCLUDE_DIRS "." "ui" ".."
    REQUIRES json lvgl esp_partition esp_http_server esp_driver_spi esp_driver_ledc esp_driver_gpio esp_driver_i2c esp_driver_usb_serial_jtag esp_timer esp_psram esp_adc nvs_flash esp_wifi esp_event esp_netif bt)
