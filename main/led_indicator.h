#ifndef LED_INDICATOR_H
#define LED_INDICATOR_H

#define LED_GPIO 2  // Built-in LED (ESP32 DevKit)

typedef enum {
    LED_MODE_NORMAL,    // Slow blink (1s on, 1s off)
    LED_MODE_OTA,       // Fast blink (200ms on, 200ms off)
    LED_MODE_RECOVERY   // Double blink pattern
} led_mode_t;

void led_init(void);
void led_set_mode(led_mode_t mode);

#endif