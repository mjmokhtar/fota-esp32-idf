#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include "esp_err.h"

/**
 * @brief Start OTA manager
 * Creates HTTP server for OTA endpoints
 */
esp_err_t ota_manager_start(void);

/**
 * @brief Perform OTA update from URL
 * @param url Firmware URL (http/https)
 */
esp_err_t ota_update_from_url(const char *url);


#endif