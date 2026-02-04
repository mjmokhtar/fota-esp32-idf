#include "recovery_mode.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "wifi_manager.h"
#include "ota_manager.h"

static const char *TAG = "RECOVERY";

#define RECOVERY_AP_SSID "IoT_M2M"
#define RECOVERY_AP_PASS "Mj02miat"

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
    buf[ret] = '\0';
    
    // Parse form data: ssid=XXX&pass=YYY
    char ssid[33] = {0};
    char pass[64] = {0};
    
    // Simple parser
    char *ssid_start = strstr(buf, "ssid=");
    char *pass_start = strstr(buf, "pass=");
    
    if (ssid_start && pass_start) {
        ssid_start += 5; // skip "ssid="
        char *ssid_end = strchr(ssid_start, '&');
        if (ssid_end) {
            int len = ssid_end - ssid_start;
            if (len < sizeof(ssid)) {
                strncpy(ssid, ssid_start, len);
            }
        }
        
        pass_start += 5; // skip "pass="
        char *pass_end = strchr(pass_start, '&');
        int len = pass_end ? (pass_end - pass_start) : strlen(pass_start);
        if (len < sizeof(pass)) {
            strncpy(pass, pass_start, len);
        }
        
        // URL decode spaces
        for (int i = 0; ssid[i]; i++) {
            if (ssid[i] == '+') ssid[i] = ' ';
        }
        for (int i = 0; pass[i]; i++) {
            if (pass[i] == '+') pass[i] = ' ';
        }
        
        // Save to NVS
        wifi_save_credentials(ssid, pass);
        
        ESP_LOGI(TAG, "WiFi config saved: SSID=%s", ssid);
        httpd_resp_send(req, "Config saved! Please reboot device.", 36);
        return ESP_OK;
    }
    
    httpd_resp_send(req, "Invalid data", 12);
    return ESP_FAIL;
}

// Handler untuk trigger OTA
static esp_err_t ota_handler(httpd_req_t *req)
{
    char buf[200];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    // Parse URL: url=http://...
    char url[150] = {0};
    char *url_start = strstr(buf, "url=");
    
    if (url_start) {
        url_start += 4;
        char *url_end = strchr(url_start, '&');
        int len = url_end ? (url_end - url_start) : strlen(url_start);
        if (len < sizeof(url)) {
            strncpy(url, url_start, len);
        }
        
        ESP_LOGI(TAG, "OTA URL: %s", url);
        httpd_resp_send(req, "OTA started! Device will reboot after update.", 47);
        
        // Trigger OTA (dari ota_manager)
        ota_update_from_url(url);
        return ESP_OK;
    }
    
    httpd_resp_send(req, "Invalid URL", 11);
    return ESP_FAIL;
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