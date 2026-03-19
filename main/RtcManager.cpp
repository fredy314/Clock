/*
 * Clock project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */
#include "RtcManager.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/time.h>
#include <string.h>

#define RTC_ADDR 0x68

void* RtcManager::dev_handle = nullptr;
static bool s_rtc_synced = false;

static void rtc_retry_task(void* pvParameters) {
    while (!s_rtc_synced) {
        vTaskDelay(pdMS_TO_TICKS(60000)); // Затримка 1 хвилина
        
        time_t rtc_time = RtcManager::getRtcTime();
        if (rtc_time == 0) continue; // Помилка читання
        
        struct tm timeinfo;
        localtime_r(&rtc_time, &timeinfo);
        
        if (timeinfo.tm_year > 120) {
            struct timeval tv = {.tv_sec = rtc_time, .tv_usec = 0};
            settimeofday(&tv, NULL);
            ESP_LOGI("RtcManager", "System time synced from RTC (retry success): %s", asctime(&timeinfo));
            s_rtc_synced = true;
            break;
        }
        
        // Якщо ми отримали IP або ESP-NOW синхронізувався раніше, 
        // то системний час вже вірний. Можна припинити спроби, 
        // бо RtcManager::updateRtc() все одно викличеться пізніше.
        time_t now;
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year > 120) {
             ESP_LOGI("RtcManager", "System time already synced via other source, stopping RTC retry.");
             s_rtc_synced = true;
             break;
        }
    }
    vTaskDelete(NULL);
}

esp_err_t RtcManager::init(int sda, int scl) {
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = (gpio_num_t)sda;
    bus_config.scl_io_num = (gpio_num_t)scl;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    
    i2c_master_bus_handle_t bus_handle;
    esp_err_t ret = i2c_new_master_bus(&bus_config, &bus_handle);
    if (ret != ESP_OK) return ret;

    i2c_device_config_t dev_config = {};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address = RTC_ADDR;
    dev_config.scl_speed_hz = 100000;

    ret = i2c_master_bus_add_device(bus_handle, &dev_config, (i2c_master_dev_handle_t*)&dev_handle);
    if (ret != ESP_OK) return ret;

    ESP_LOGI("RtcManager", "I2C Master initialized at SDA:%d, SCL:%d", sda, scl);
    
    // Спроба зчитати час при старті
    time_t rtc_time = getRtcTime();
    struct tm timeinfo;
    localtime_r(&rtc_time, &timeinfo);
    
    if (timeinfo.tm_year > 120) {
        struct timeval tv = {.tv_sec = rtc_time, .tv_usec = 0};
        settimeofday(&tv, NULL);
        ESP_LOGI("RtcManager", "System time synced from RTC: %s", asctime(&timeinfo));
        s_rtc_synced = true;
    } else {
        ESP_LOGW("RtcManager", "RTC time is invalid or read failed, starting retry task...");
        xTaskCreate(rtc_retry_task, "rtc_retry", 3072, NULL, 1, NULL);
    }
    
    return ESP_OK;
}

time_t RtcManager::getRtcTime() {
    uint8_t data[7];
    if (readReg(0x00, data, 7) != ESP_OK) return 0;

    struct tm tm = {};
    tm.tm_sec  = bcdToDec(data[0] & 0x7F);
    tm.tm_min  = bcdToDec(data[1]);
    tm.tm_hour = bcdToDec(data[2] & 0x3F); // 24h mode
    tm.tm_mday = bcdToDec(data[4]);
    tm.tm_mon  = bcdToDec(data[5] & 0x1F) - 1;
    tm.tm_year = bcdToDec(data[6]) + 100; // Since 2000

    return mktime(&tm);
}

void RtcManager::updateRtc(time_t t) {
    struct tm tm;
    localtime_r(&t, &tm);

    uint8_t data[7];
    data[0] = decToBcd(tm.tm_sec);
    data[1] = decToBcd(tm.tm_min);
    data[2] = decToBcd(tm.tm_hour);
    data[3] = 0; // Day of week (unused)
    data[4] = decToBcd(tm.tm_mday);
    data[5] = decToBcd(tm.tm_mon + 1);
    data[6] = decToBcd(tm.tm_year - 100);

    writeReg(0x00, data, 7);
    ESP_LOGI("RtcManager", "RTC updated to: %s", asctime(&tm));
}

uint8_t RtcManager::bcdToDec(uint8_t val) { return ((val / 16 * 10) + (val % 16)); }
uint8_t RtcManager::decToBcd(uint8_t val) { return ((val / 10 * 16) + (val % 10)); }

esp_err_t RtcManager::readReg(uint8_t reg, uint8_t* data, size_t len) {
    if (!dev_handle) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive((i2c_master_dev_handle_t)dev_handle, &reg, 1, data, len, 100);
}

esp_err_t RtcManager::writeReg(uint8_t reg, uint8_t* data, size_t len) {
    if (!dev_handle) return ESP_ERR_INVALID_STATE;
    uint8_t buf[len + 1];
    buf[0] = reg;
    memcpy(buf + 1, data, len);
    return i2c_master_transmit((i2c_master_dev_handle_t)dev_handle, buf, len + 1, 100);
}
