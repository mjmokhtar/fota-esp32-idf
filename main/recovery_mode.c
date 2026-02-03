#include "recovery_mode.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "nvs_flash.h"

static const char *TAG = "RECOVERY";

#define RECOVERY_AP_SSID "ESP32-Recovery"
#define RECOVERY_AP_PASS "recovery123"

// HTML page untuk config
static const char *html_page = 
    "<!DOCTYPE html><html><body>"
    "<h1>ESP32 Recovery Mode</h1>"
    "<form action='/config' method='post'>"
    "WiFi SSID: <input name='ssid' type='text'><br>"
    "Password: <input name='pass' type='password'><br>"
    "<input type='submit' value='Save'>"
    "</form>"
    "<hr>"
    "<form action='/ota' method='post'>"
    "Firmware URL: <input name='url' type='text' size='50'><br>"
    "<input type='submit' value='Update'>"
    "</form>"
    "</body></html>";

// Handler untuk halaman utama
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_send(req, html_page, strlen(html_page));
    return ESP_OK;
}

// Handler untuk save WiFi config
static esp_err_t config_handler(httpd_req_t *req)
{
    char buf[200];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
        return ESP_FAIL;
    }
    
    // Parse form data (simplified - production harus lebih robust)
    // Format: ssid=XXX&pass=YYY
    // TODO: Implement proper URL decode dan save ke NVS
    
    ESP_LOGI(TAG, "WiFi config updated");
    httpd_resp_send(req, "Config saved! Reboot to apply.", 29);
    return ESP_OK;
}

// Handler untuk trigger OTA
static esp_err_t ota_handler(httpd_req_t *req)
{
    char buf[200];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
        return ESP_FAIL;
    }
    
    // Parse URL dan trigger OTA
    // TODO: Implement OTA from URL
    
    httpd_resp_send(req, "OTA triggered!", 14);
    return ESP_OK;
}

void recovery_mode_start(void)
{
    ESP_LOGI(TAG, "Starting Recovery Mode AP...");
    
    // Init WiFi dalam mode AP
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = RECOVERY_AP_SSID,
            .password = RECOVERY_AP_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "AP started: SSID=%s, Pass=%s", RECOVERY_AP_SSID, RECOVERY_AP_PASS);
    
    // Start HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler
        };
        httpd_register_uri_handler(server, &root);
        
        httpd_uri_t config_uri = {
            .uri = "/config",
            .method = HTTP_POST,
            .handler = config_handler
        };
        httpd_register_uri_handler(server, &config_uri);
        
        httpd_uri_t ota_uri = {
            .uri = "/ota",
            .method = HTTP_POST,
            .handler = ota_handler
        };
        httpd_register_uri_handler(server, &ota_uri);
        
        ESP_LOGI(TAG, "HTTP server started on http://192.168.4.1");
    }
}