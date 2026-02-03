#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Initialize WiFi in Station mode
 * Reads WiFi credentials from NVS and connects
 */
esp_err_t wifi_init(void);

/**
 * @brief Save WiFi credentials to NVS
 */
esp_err_t wifi_save_credentials(const char *ssid, const char *password);

/**
 * @brief Check if WiFi is connected
 */
bool wifi_is_connected(void);

#endif