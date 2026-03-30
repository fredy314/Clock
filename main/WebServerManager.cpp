/*
 * Clock project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */

#include "WebServerManager.h"
#include "LogManager.h"
#include "ClockManager.h"
#include "PropsManager.h"
#include "HlkLd2410Manager.h"
#include <string.h>
#include <stdio.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <algorithm>
#include "esp_spiffs.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include "esp_timer.h"
#include <time.h>
#include <math.h>

static const char *TAG = "WebServerManager";
httpd_handle_t WebServerManager::server = NULL;
DhtManager* WebServerManager::_dht = nullptr;
ClockManager* WebServerManager::_clock = nullptr;

esp_err_t WebServerManager::init_spiffs() {
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = "storage",
      .max_files = 5,
      .format_if_mount_failed = false
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition (check partitions.csv)");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info("storage", &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    return ESP_OK;
}

const char* WebServerManager::get_content_type(const char* filepath) {
    if (strstr(filepath, ".html")) return "text/html";
    if (strstr(filepath, ".css")) return "text/css";
    if (strstr(filepath, ".js")) return "application/javascript";
    if (strstr(filepath, ".png")) return "image/png";
    if (strstr(filepath, ".ico")) return "image/x-icon";
    return "text/plain";
}

esp_err_t WebServerManager::common_get_handler(httpd_req_t *req) {
    char filepath[512];
    
    const char *uri = req->uri;
    if (strcmp(uri, "/") == 0) {
        uri = "/index.html";
    }

    const char* q_mark = strchr(uri, '?');
    if (q_mark) {
        snprintf(filepath, sizeof(filepath), "/spiffs%.*s", (int)(q_mark - uri), uri);
    } else {
        snprintf(filepath, sizeof(filepath), "/spiffs%.500s", uri);
    }
    
    if (strncmp(uri, "/ota.html", 9) == 0) {
        if (!is_authenticated(req)) {
            httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Clock OTA\"");
            httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
            return ESP_OK;
        }
    }
    
    struct stat file_stat;
    if (stat(filepath, &file_stat) == -1) {
        ESP_LOGE(TAG, "Failed to stat file : %s", filepath);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    FILE *fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Serving file : %s", filepath);
    httpd_resp_set_type(req, get_content_type(filepath));

    char chunk[1024];
    size_t chunksize;
    do {
        chunksize = fread(chunk, 1, sizeof(chunk), fd);
        if (chunksize > 0) {
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed!");
                httpd_resp_send_chunk(req, NULL, 0);
                return ESP_FAIL;
            }
        }
    } while (chunksize != 0);

    fclose(fd);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t WebServerManager::start_server(DhtManager* dht, ClockManager* clock) {
    _dht = dht;
    _clock = clock;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 12;
    config.stack_size = 10240; // Збільшимо стек для безпеки OTA

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        
        httpd_uri_t status_uri = {
            .uri       = "/status",
            .method    = HTTP_GET,
            .handler   = status_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &status_uri);

        httpd_uri_t temp_uri = {
            .uri       = "/api/display/temp",
            .method    = HTTP_GET,
            .handler   = display_temp_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &temp_uri);

        httpd_uri_t hum_uri = {
            .uri       = "/api/display/hum",
            .method    = HTTP_GET,
            .handler   = display_hum_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &hum_uri);

        httpd_uri_t ota_post_uri = {
            .uri       = "/ota.html",
            .method    = HTTP_POST,
            .handler   = ota_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &ota_post_uri);

        httpd_uri_t brightness_uri = {
            .uri       = "/api/display/brightness",
            .method    = HTTP_GET,
            .handler   = brightness_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &brightness_uri);

        httpd_uri_t wildcard_get = {
            .uri       = "/*", 
            .method    = HTTP_GET,
            .handler   = common_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &wildcard_get);

        return ESP_OK;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return ESP_FAIL;
}

esp_err_t WebServerManager::status_get_handler(httpd_req_t *req) {
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char time_str[10];
    char date_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &timeinfo);
    strftime(date_str, sizeof(date_str), "%d.%m.%Y", &timeinfo);

    float t = roundf(_dht->getTemperature() * 10.0f) / 10.0f;
    float h = roundf(_dht->getHumidity() * 10.0f) / 10.0f;

    const esp_app_desc_t *app_desc = esp_app_get_description();
    std::string logs_json = LogManager::getLogsJson(5);
    std::string zones_json = HlkLd2410Manager::getActiveZonesJson();

    char *response = (char*)malloc(2048);
    if (response == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory error");
        return ESP_FAIL;
    }

    snprintf(response, 2048, 
             "{\"time\":\"%s\",\"date\":\"%s\",\"temperature\":%.1f,\"humidity\":%.1f,\"brightness\":%d,\"project\":\"%s\",\"version\":\"%s\",\"zones\":%s,\"logs\":%s}", 
             time_str, date_str, t, h, _clock->getBrightness(), app_desc->project_name, app_desc->version, zones_json.c_str(), logs_json.c_str());
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    free(response);
    return ESP_OK;
}

bool WebServerManager::is_authenticated(httpd_req_t *req) {
    char buf[128];
    if (httpd_req_get_hdr_value_str(req, "Authorization", buf, sizeof(buf)) == ESP_OK) {
        if (strncmp(buf, "Basic ", 6) == 0) {
            unsigned char decoded[128];
            size_t decoded_len = 0;
            if (mbedtls_base64_decode(decoded, sizeof(decoded), &decoded_len, (const unsigned char*)buf + 6, strlen(buf + 6)) == 0) {
                char expected[128];
                snprintf(expected, sizeof(expected), "%s:%s", OTA_USER, OTA_PASS);
                if (decoded_len == strlen(expected) && memcmp(decoded, expected, decoded_len) == 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

int WebServerManager::compare_versions(const char* new_ver, const char* old_ver) {
    int v1[3] = {0,0,0}, v2[3] = {0,0,0};
    sscanf(new_ver, "%d.%d.%d", &v1[0], &v1[1], &v1[2]);
    sscanf(old_ver, "%d.%d.%d", &v2[0], &v2[1], &v2[2]);
    for (int i = 0; i < 3; i++) {
        if (v1[i] > v2[i]) return 1;
        if (v1[i] < v2[i]) return -1;
    }
    return 0;
}

esp_err_t WebServerManager::ota_post_handler(httpd_req_t *req) {
    if (!is_authenticated(req)) {
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Clock OTA\"");
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_OK;
    }

    char buf[256];
    char type[32] = "app"; 
    char force_str[10] = "0";
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        httpd_query_key_value(buf, "type", type, sizeof(type));
        httpd_query_key_value(buf, "force", force_str, sizeof(force_str));
    }
    bool force = (strcmp(force_str, "1") == 0);

    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = NULL;
    bool is_spiffs = (strcmp(type, "spiffs") == 0);

    if (is_spiffs) {
        update_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "storage");
    } else {
        update_partition = esp_ota_get_next_update_partition(NULL);
    }

    if (update_partition == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Partition not found");
        return ESP_FAIL;
    }

    const esp_app_desc_t *current_app_desc = esp_app_get_description();
    int remaining = req->content_len;
    int offset = 0;
    bool header_checked = false;

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) continue;
            if (!is_spiffs && header_checked) esp_ota_end(update_handle);
            return ESP_FAIL;
        }

        if (!is_spiffs && !header_checked) {
            esp_app_desc_t *new_app_desc = (esp_app_desc_t *)(buf + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
            if (new_app_desc->magic_word != ESP_APP_DESC_MAGIC_WORD) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid firmware image");
                return ESP_FAIL;
            }
            if (strcmp(new_app_desc->project_name, current_app_desc->project_name) != 0) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Помилка: прошивка від іншого пристрою!");
                return ESP_FAIL;
            }
            int ver_cmp = compare_versions(new_app_desc->version, current_app_desc->version);
            if (ver_cmp < 0 && !force) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Помилка: не можна заливати застарілу версію!");
                return ESP_FAIL;
            }
            esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
            if (err != ESP_OK) {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
                return ESP_FAIL;
            }
            header_checked = true;
        }

        if (is_spiffs) {
            if (offset == 0) esp_partition_erase_range(update_partition, 0, update_partition->size);
            esp_partition_write(update_partition, offset, buf, recv_len);
        } else {
            esp_ota_write(update_handle, buf, recv_len);
        }
        remaining -= recv_len;
        offset += recv_len;
    }

    if (!is_spiffs) {
        esp_ota_end(update_handle);
        esp_ota_set_boot_partition(update_partition);
    }

    httpd_resp_sendstr(req, "Update successful");
    if (!is_spiffs) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
    return ESP_OK;
}

esp_err_t WebServerManager::display_temp_handler(httpd_req_t *req) {
    if (_clock) _clock->showTemp();
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

esp_err_t WebServerManager::display_hum_handler(httpd_req_t *req) {
    if (_clock) _clock->showHum();
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

esp_err_t WebServerManager::brightness_get_handler(httpd_req_t *req) {
    char buf[32];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char level_str[10];
        if (httpd_query_key_value(buf, "level", level_str, sizeof(level_str)) == ESP_OK) {
            int level = atoi(level_str);
            if (level >= 0 && level <= 16) {
                if (_clock) {
                    _clock->setBrightness((uint8_t)level);
                    PropsManager::setBrightness((uint8_t)level);
                }
                httpd_resp_sendstr(req, "OK");
                return ESP_OK;
            }
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid brightness level (0-16)");
    return ESP_FAIL;
}

void WebServerManager::stop_server() {
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
}
