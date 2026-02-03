#include "led_indicator.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static led_mode_t current_mode = LED_MODE_NORMAL;
static TaskHandle_t led_task_handle = NULL;

static void led_task(void *pvParameters)
{
    while (1) {
        switch (current_mode) {
            case LED_MODE_NORMAL:
                gpio_set_level(LED_GPIO, 1);
                vTaskDelay(pdMS_TO_TICKS(1000));
                gpio_set_level(LED_GPIO, 0);
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;
                
            case LED_MODE_OTA:
                gpio_set_level(LED_GPIO, 1);
                vTaskDelay(pdMS_TO_TICKS(200));
                gpio_set_level(LED_GPIO, 0);
                vTaskDelay(pdMS_TO_TICKS(200));
                break;
                
            case LED_MODE_RECOVERY:
                // Double blink
                for (int i = 0; i < 2; i++) {
                    gpio_set_level(LED_GPIO, 1);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    gpio_set_level(LED_GPIO, 0);
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                vTaskDelay(pdMS_TO_TICKS(800));
                break;
        }
    }
}

void led_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);
    
    xTaskCreate(led_task, "led_task", 2048, NULL, 5, &led_task_handle);
}

void led_set_mode(led_mode_t mode)
{
    current_mode = mode;
}