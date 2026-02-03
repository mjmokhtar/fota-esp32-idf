idf_component_register(
    SRCS "main.c"
         "led_indicator.c"
         "wifi_manager.c"
         "ota_manager.c"
         "recovery_mode.c"
    INCLUDE_DIRS "."
    REQUIRES 
        esp_http_server
        esp_https_ota
        esp_wifi
        esp_netif
        nvs_flash
        app_update
        bootloader_support
        esp_driver_gpio
)