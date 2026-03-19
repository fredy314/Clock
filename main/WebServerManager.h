/*
 * Clock project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */

#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include "DhtManager.h"

class WebServerManager {
public:
    // Ініціалізувати SPIFFS
    static esp_err_t init_spiffs();

    // Запустити веб-сервер
    static esp_err_t start_server(DhtManager* dht);

    // Зупинити веб-сервер
    static void stop_server();

    static constexpr const char* OTA_USER = "admin";
    static constexpr const char* OTA_PASS = "31415926";

private:
    static httpd_handle_t server;
    static DhtManager* _dht;

    // API обробники
    static esp_err_t status_get_handler(httpd_req_t *req);
    static esp_err_t ota_post_handler(httpd_req_t *req);

    // Універсальний обробник GET-запитів до статичних файлів
    static esp_err_t common_get_handler(httpd_req_t *req);
    
    // Перевірка Basic Auth для сторінки OTA
    static bool is_authenticated(httpd_req_t *req);

    // Допоміжна функція визначення Content-Type за розширенням
    static const char* get_content_type(const char* filename);

    static int compare_versions(const char* new_ver, const char* old_ver);
};
