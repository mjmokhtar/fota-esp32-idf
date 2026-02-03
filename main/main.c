#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"        // ← TAMBAH INI
#include "esp_ota_ops.h"        // ← TAMBAH INI

#include "led_indicator.h"
#include "wifi_manager.h"
#include "ota_manager.h"
#include "recovery_mode.h"

static const char *TAG = "MAIN";

#define BOOT_BUTTON_GPIO 0
#define VALIDATION_TIME_MS 10000

void app_main(void)
{
    ESP_LOGI(TAG, "Firmware Assessment ESP32 Starting...");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize LED
    led_init();
    
    // Check if BOOT button is pressed (Recovery Mode)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);
    
    vTaskDelay(pdMS_TO_TICKS(100)); // Debounce
    
    if (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
        ESP_LOGI(TAG, "Recovery mode triggered!");
        led_set_mode(LED_MODE_RECOVERY);
        recovery_mode_start();
        return; // Stay in recovery
    }

    // Normal boot - validate current partition
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "New firmware detected, validating...");
            led_set_mode(LED_MODE_OTA);
            
            // Wait 10 seconds for stability check
            vTaskDelay(pdMS_TO_TICKS(VALIDATION_TIME_MS));
            
            // Mark as valid
            esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGI(TAG, "Firmware validated successfully!");
        }
    }

    // Normal operation
    led_set_mode(LED_MODE_NORMAL);
    ESP_LOGI(TAG, "Starting normal operation...");
    
    // Initialize WiFi and start OTA server
    wifi_init();
    ota_manager_start();
    
    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}