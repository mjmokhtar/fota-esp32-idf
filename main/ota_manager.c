#include "ota_manager.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_app_format.h"
#include "led_indicator.h"
#include <string.h>

static const char *TAG = "OTA_MGR";

// Simple HTML page for OTA
static const char *ota_html = 
    "<!DOCTYPE html><html><body>"
    "<h1>ESP32 OTA Update</h1>"
    "<form action='/update' method='post'>"
    "Firmware URL: <input name='url' type='text' size='60' "
    "placeholder='http://example.com/firmware.bin'><br><br>"
    "<input type='submit' value='Start Update'>"
    "</form>"
    "<hr><p>Current partition: %s</p>"
    "<p>App version: %s</p>"
    "</body></html>";

static httpd_handle_t ota_server = NULL;

static void ota_update_task_wrapper(void *pvParameter);  // ← TAMBAH INI

// Handler untuk halaman OTA
static esp_err_t ota_page_handler(httpd_req_t *req)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_app_desc_t *app_desc = esp_app_get_description();  // FIX: gunakan yang baru
    
    char response[1024];
    snprintf(response, sizeof(response), ota_html, 
             running->label, 
             app_desc->version);
    
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

// Handler untuk trigger OTA update
static esp_err_t ota_update_handler(httpd_req_t *req)
{
    char buf[256];
    char url[200] = {0};
    int ret, remaining = req->content_len;

    // Read POST data
    while (remaining > 0) {
        int to_read = (remaining < sizeof(buf)) ? remaining : sizeof(buf);  // FIX: ganti MIN
        ret = httpd_req_recv(req, buf, to_read);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }
        remaining -= ret;
    }
    buf[req->content_len] = '\0';

    // Parse URL from form data (format: url=http://...)
    char *url_start = strstr(buf, "url=");
    if (url_start) {
        url_start += 4; // Skip "url="
        
        // URL decode (simplified - handle %XX)
        int j = 0;
        for (int i = 0; url_start[i] && url_start[i] != '&' && j < sizeof(url)-1; i++) {
            if (url_start[i] == '%' && url_start[i+1] && url_start[i+2]) {
                // Convert hex to char
                char hex[3] = {url_start[i+1], url_start[i+2], 0};
                url[j++] = (char)strtol(hex, NULL, 16);
                i += 2;
            } else if (url_start[i] == '+') {
                url[j++] = ' ';
            } else {
                url[j++] = url_start[i];
            }
        }
        url[j] = '\0';
    }

    ESP_LOGI(TAG, "OTA URL: %s", url);

    // Send response immediately
    httpd_resp_send(req, "OTA Update started! Device will reboot after update.", 54);

    // Start OTA in separate task - FIX: gunakan wrapper function
    char *url_copy = strdup(url);
    xTaskCreate(ota_update_task_wrapper, "ota_task", 8192, url_copy, 5, NULL);

    return ESP_OK;
}

// FIX: Wrapper function untuk xTaskCreate
static void ota_update_task_wrapper(void *pvParameter)
{
    char *url = (char *)pvParameter;
    ota_update_from_url(url);
    free(url);
    vTaskDelete(NULL);
}

esp_err_t ota_update_from_url(const char *url)
{
    ESP_LOGI(TAG, "Starting OTA update from: %s", url);
    led_set_mode(LED_MODE_OTA);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_err_t ret = esp_https_ota(&ota_config);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA update successful! Rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(ret));
        led_set_mode(LED_MODE_NORMAL);
    }

    return ret;
}

esp_err_t ota_manager_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    if (httpd_start(&ota_server, &config) == ESP_OK) {
        // Register OTA page
        httpd_uri_t ota_page = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = ota_page_handler,
        };
        httpd_register_uri_handler(ota_server, &ota_page);

        // Register OTA update endpoint
        httpd_uri_t ota_update = {
            .uri       = "/update",
            .method    = HTTP_POST,
            .handler   = ota_update_handler,
        };
        httpd_register_uri_handler(ota_server, &ota_update);

        ESP_LOGI(TAG, "OTA server started on port 80");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to start OTA server");
    return ESP_FAIL;
}