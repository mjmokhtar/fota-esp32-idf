#include "ota_manager.h"
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_app_format.h"
#include "led_indicator.h"
#include <string.h>

static const char *TAG = "OTA_MGR";
static httpd_handle_t ota_server = NULL;

// Forward declaration
static void ota_update_task_wrapper(void *pvParameter);

// Handler untuk halaman OTA
static esp_err_t ota_page_handler(httpd_req_t *req)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_app_desc_t *app_desc = esp_app_get_description();
    
    httpd_resp_set_type(req, "text/html");
    
    httpd_resp_sendstr_chunk(req, 
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<title>ESP32 OTA</title>"
        "<style>"
        "body{font-family:Arial;max-width:600px;margin:50px auto;padding:20px;}"
        "input[type=text]{width:100%;padding:8px;margin:8px 0;}"
        "input[type=submit]{background:#4CAF50;color:white;padding:10px 20px;"
        "border:none;cursor:pointer;}"
        "</style>"
        "</head><body>"
        "<h2>ESP32 OTA Update</h2>"
    );
    
    char info[150];
    snprintf(info, sizeof(info), 
        "<p>Partition: <b>%s</b> | Version: <b>%s</b></p>",
        running->label, app_desc->version);
    httpd_resp_sendstr_chunk(req, info);
    
    httpd_resp_sendstr_chunk(req,
        "<form action='/update' method='post'>"
        "Firmware URL:<br>"
        "<input type='text' name='url' placeholder='http://192.168.x.x:8000/firmware.bin'><br>"
        "<input type='submit' value='Start Update'>"
        "</form>"
        "</body></html>"
    );
    
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

// Handler untuk trigger OTA update
static esp_err_t ota_update_handler(httpd_req_t *req)
{
    char buf[256];
    char url[200] = {0};
    int ret, remaining = req->content_len;
    
    if (remaining == 0 || remaining > sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    // Parse URL
    char *url_start = strstr(buf, "url=");
    if (url_start) {
        url_start += 4;
        int j = 0;
        for (int i = 0; url_start[i] && url_start[i] != '&' && j < sizeof(url)-1; i++) {
            if (url_start[i] == '%' && url_start[i+1] && url_start[i+2]) {
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

    if (strlen(url) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No URL");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA URL: %s", url);
    httpd_resp_sendstr(req, "OTA started! Device will reboot.");

    char *url_copy = strdup(url);
    if (url_copy) {
        xTaskCreate(ota_update_task_wrapper, "ota_task", 8192, url_copy, 5, NULL);
    }

    return ESP_OK;
}

static void ota_update_task_wrapper(void *pvParameter)
{
    char *url = (char *)pvParameter;
    ota_update_from_url(url);
    free(url);
    vTaskDelete(NULL);
}

esp_err_t ota_update_from_url(const char *url)
{
    ESP_LOGI(TAG, "=== Starting OTA Update ===");
    ESP_LOGI(TAG, "URL: %s", url);
    led_set_mode(LED_MODE_OTA);

    esp_err_t err;
    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = NULL;

    update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition available");
        led_set_mode(LED_MODE_NORMAL);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Target partition: %s (offset 0x%08lx)", 
             update_partition->label, update_partition->address);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000,
        .keep_alive_enable = true,
        .buffer_size = 1024,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        led_set_mode(LED_MODE_NORMAL);
        return ESP_FAIL;
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        led_set_mode(LED_MODE_NORMAL);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    
    ESP_LOGI(TAG, "HTTP Status: %d, Content Length: %d", status_code, content_length);

    if (status_code != 200 || content_length <= 0) {
        ESP_LOGE(TAG, "Invalid HTTP response");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        led_set_mode(LED_MODE_NORMAL);
        return ESP_FAIL;
    }

    // Allocate buffer
    char *buffer = malloc(1024);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        led_set_mode(LED_MODE_NORMAL);
        return ESP_ERR_NO_MEM;
    }

    // Read first chunk to check for custom header
    int first_read = esp_http_client_read(client, buffer, 44);  // Header size = 44 bytes
    if (first_read < 44) {
        ESP_LOGE(TAG, "Failed to read header");
        free(buffer);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        led_set_mode(LED_MODE_NORMAL);
        return ESP_FAIL;
    }

    // Check for custom header magic (0xDEADBEEF)
    uint32_t magic = *((uint32_t *)buffer);
    bool has_custom_header = (magic == 0xDEADBEEF);
    
    int header_offset = 0;
    int actual_fw_size = content_length;
    
    if (has_custom_header) {
        ESP_LOGI(TAG, "Custom header detected (magic: 0x%08lx)", magic);
        header_offset = 44;  // Skip 44-byte header
        actual_fw_size = content_length - 44;
        
        // Validate SHA256 here if needed
        uint32_t version = *((uint32_t *)(buffer + 4));
        uint32_t size = *((uint32_t *)(buffer + 8));
        ESP_LOGI(TAG, "Header - Version: 0x%08lx, Size: %lu", version, size);
    } else {
        ESP_LOGI(TAG, "Raw firmware detected (magic: 0x%02x)", buffer[0]);
    }

    // Begin OTA
    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        free(buffer);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        led_set_mode(LED_MODE_NORMAL);
        return err;
    }
    ESP_LOGI(TAG, "OTA begin successful");

    int binary_file_length = 0;
    int last_progress = 0;
    
    ESP_LOGI(TAG, "Writing firmware...");

    // If custom header, skip it by not writing first 44 bytes
    if (has_custom_header) {
        // Write remaining data from first read (after header)
        int remaining_first_chunk = first_read - 44;
        if (remaining_first_chunk > 0) {
            err = esp_ota_write(update_handle, (const void *)(buffer + 44), remaining_first_chunk);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
                esp_ota_abort(update_handle);
                free(buffer);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                led_set_mode(LED_MODE_NORMAL);
                return err;
            }
            binary_file_length += remaining_first_chunk;
        }
    } else {
        // Raw firmware - write first chunk as-is
        err = esp_ota_write(update_handle, (const void *)buffer, first_read);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
            esp_ota_abort(update_handle);
            free(buffer);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            led_set_mode(LED_MODE_NORMAL);
            return err;
        }
        binary_file_length += first_read;
    }

    // Continue reading and writing
    while (1) {
        int data_read = esp_http_client_read(client, buffer, 1024);
        
        if (data_read < 0) {
            ESP_LOGE(TAG, "Error reading data");
            err = ESP_FAIL;
            break;
        } else if (data_read > 0) {
            err = esp_ota_write(update_handle, (const void *)buffer, data_read);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
                break;
            }
            binary_file_length += data_read;
            
            int progress = (binary_file_length * 100) / actual_fw_size;
            if (progress >= last_progress + 10) {
                ESP_LOGI(TAG, "Progress: %d%% (%d / %d bytes)", 
                         progress, binary_file_length, actual_fw_size);
                last_progress = progress;
            }
        } else if (data_read == 0) {
            ESP_LOGI(TAG, "Download complete");
            break;
        }
    }

    free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Download failed");
        esp_ota_abort(update_handle);
        led_set_mode(LED_MODE_NORMAL);
        return err;
    }

    ESP_LOGI(TAG, "Total firmware bytes written: %d", binary_file_length);

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA end failed: %s", esp_err_to_name(err));
        led_set_mode(LED_MODE_NORMAL);
        return err;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(err));
        led_set_mode(LED_MODE_NORMAL);
        return err;
    }

    ESP_LOGI(TAG, "=== OTA Update Successful ===");
    ESP_LOGI(TAG, "Rebooting in 3 seconds...");
    
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();

    return ESP_OK;
}

esp_err_t ota_manager_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    if (httpd_start(&ota_server, &config) == ESP_OK) {
        httpd_uri_t ota_page = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = ota_page_handler,
        };
        httpd_register_uri_handler(ota_server, &ota_page);

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