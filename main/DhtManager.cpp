/*
 * Clock project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */
#include "DhtManager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"
#include "esp_system.h"

DhtManager::DhtManager(gpio_num_t pin) : dht_pin(pin), last_success_time(esp_timer_get_time()) {}

void DhtManager::init() {
    gpio_set_direction(dht_pin, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_pull_mode(dht_pin, GPIO_PULLUP_ONLY);
}

void DhtManager::readTask() {
    while (true) {
        float humidity, temperature;
        int64_t now = esp_timer_get_time();

        // 1. Перевірка на "зомбі-стан" (якщо 30 хв немає даних)
        if ((now - last_success_time) > 1800000000) {
            ESP_LOGE("DhtManager", "Sensor hung for 30 min, restarting system!");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }

        if (read(humidity, temperature) == ESP_OK) {
            last_humidity = humidity;
            last_temp = temperature;
            last_success_time = now;
            error_count = 0;
            ESP_LOGI("DhtManager", "Temp: %.1f C, Hum: %.1f %%", temperature, humidity);
        } else {
            error_count++;
            ESP_LOGW("DhtManager", "Failed to read DHT22 sensor (errors: %d)", error_count);
            
            // 2. Автоматичне відновлення після 10 помилок
            if (error_count >= 10) {
                ESP_LOGE("DhtManager", "10 consecutive errors, re-initializing GPIO...");
                init();
                error_count = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10000)); // Read every 10 seconds
    }
}

esp_err_t DhtManager::read(float& humidity, float& temperature) {
    uint8_t data[5] = {0, 0, 0, 0, 0};
    
    // Start signal
    gpio_set_direction(dht_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(dht_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(dht_pin, 1);
    ets_delay_us(30);
    gpio_set_direction(dht_pin, GPIO_MODE_INPUT);

    // Wait for sensor response
    if (wait_state(0, 100) != ESP_OK) return ESP_FAIL;
    if (wait_state(1, 100) != ESP_OK) return ESP_FAIL;
    if (wait_state(0, 100) != ESP_OK) return ESP_FAIL;

    // Секція зчитування 40 біт (критична по таймінгах)
    static portMUX_TYPE dht_mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&dht_mux);
    
    for (int i = 0; i < 40; i++) {
        if (wait_state(1, 100) != ESP_OK) {
            portEXIT_CRITICAL(&dht_mux);
            return ESP_FAIL;
        }
        uint32_t start = esp_timer_get_time();
        if (wait_state(0, 100) != ESP_OK) {
            portEXIT_CRITICAL(&dht_mux);
            return ESP_FAIL;
        }
        if ((esp_timer_get_time() - start) > 40) {
            data[i / 8] <<= 1;
            data[i / 8] |= 1;
        } else {
            data[i / 8] <<= 1;
        }
    }
    
    portEXIT_CRITICAL(&dht_mux);

    // Checksum
    if (data[4] != (uint8_t)(data[0] + data[1] + data[2] + data[3])) {
        return ESP_ERR_INVALID_CRC;
    }

    humidity = (float)((data[0] << 8) + data[1]) / 10.0;
    temperature = (float)(((data[2] & 0x7F) << 8) + data[3]) / 10.0;
    if (data[2] & 0x80) temperature *= -1;

    return ESP_OK;
}

esp_err_t DhtManager::wait_state(int state, int timeout_us) {
    int us = 0;
    while (gpio_get_level(dht_pin) != state) {
        if (us++ > timeout_us) return ESP_ERR_TIMEOUT;
        ets_delay_us(1);
    }
    return ESP_OK;
}
