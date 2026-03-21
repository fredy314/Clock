/*
 * Clock project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */
#include "PropsManager.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

const char* PropsManager::TAG = "PropsManager";
const char* PropsManager::NVS_NAMESPACE = "storage";
const char* PropsManager::KEY_BRIGHTNESS = "brightness";

uint8_t PropsManager::getBrightness() {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Error (%s) opening NVS handle! Using default brightness %d", esp_err_to_name(err), DEFAULT_BRIGHTNESS);
        return DEFAULT_BRIGHTNESS;
    }

    uint8_t brightness = DEFAULT_BRIGHTNESS;
    err = nvs_get_u8(my_handle, KEY_BRIGHTNESS, &brightness);
    switch (err) {
        case ESP_OK:
            ESP_LOGI(TAG, "Loaded brightness: %d", brightness);
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            ESP_LOGI(TAG, "Brightness not found in NVS, using default %d", DEFAULT_BRIGHTNESS);
            break;
        default:
            ESP_LOGE(TAG, "Error (%s) reading brightness!", esp_err_to_name(err));
    }
    nvs_close(my_handle);
    return brightness;
}

void PropsManager::setBrightness(uint8_t level) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return;
    }

    err = nvs_set_u8(my_handle, KEY_BRIGHTNESS, level);
    if (err == ESP_OK) {
        err = nvs_commit(my_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Saved brightness: %d", level);
        } else {
            ESP_LOGE(TAG, "Error (%s) committing NVS!", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "Error (%s) writing brightness!", esp_err_to_name(err));
    }
    nvs_close(my_handle);
}
