#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
/*
 * Clock project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */
#include "esp_log.h"
#include "WifiManager.h"
#include "Max7219.h"
#include "ClockManager.h"
#include "SunTimeManager.h"
#include "EspNowTimeSync.h"
#include "RtcManager.h"
#include "DhtManager.h"
#include "MqttManager.h"
#include "WebServerManager.h"
#include "BatteryMonitor.h"
#include "LogManager.h"
#include "PropsManager.h"
#include "HlkLd2410Manager.h"
#include "SoundCheck.h"

#define SSID "HomeF"
#define PASSWORD "21122112"
#define HOSTNAME "clock"

// Визначення пінів
#define MAX7219_DIN_PIN  GPIO_NUM_7
#define MAX7219_CLK_PIN  GPIO_NUM_4
#define MAX7219_CS_PIN   GPIO_NUM_6
#define DHT_DATA_PIN     GPIO_NUM_5
#define RTC_SDA_PIN      8
#define RTC_SCL_PIN      9

#include "driver/usb_serial_jtag.h"
#include "driver/uart.h"

void uart_sync_task(void *pvParameters) {
    ESP_LOGI("SYNC", "Starting USB/UART sync task...");
    
    // Ініціалізуємо драйвер USB-Serial-JTAG (для ttyACM0)
    usb_serial_jtag_driver_config_t usb_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t u_ret = usb_serial_jtag_driver_install(&usb_cfg);
    if (u_ret != ESP_OK && u_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("SYNC", "Failed to install USB-JTAG driver: %d", u_ret);
    }

    uint8_t data[128];
    while (1) {
        // Зчитуємо з USB (вбудований порт C3)
        int len = usb_serial_jtag_read_bytes(data, 127, pdMS_TO_TICKS(20));
        
        if (len > 0) {
            data[len] = '\0';
            long long timestamp;
            if (sscanf((char*)data, "SET_TIME:%lld", &timestamp) == 1) {
                SunTimeManager::setTime((time_t)timestamp);
                ESP_LOGI("SYNC", "Synced: %lld", timestamp);
                printf("TIME_SYNC_OK\n");
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

extern "C" void app_main(void)
{
    // Встановлюємо часовий пояс на самому початку, щоб всі логи та функції часу працювали вірно
    setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
    tzset();

    LogManager::init();

    ESP_LOGI("MAIN", "Booting...");
    // 1. Ініціалізація NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Ініціалізація RTC
    ESP_LOGI("MAIN", "Initializing RTC...");
    RtcManager::init(RTC_SDA_PIN, RTC_SCL_PIN);

    // 3. Ініціалізація матриці
    ESP_LOGI("MAIN", "Initializing Matrix...");
    Max7219* matrix = new Max7219(MAX7219_DIN_PIN, MAX7219_CLK_PIN, MAX7219_CS_PIN);
    matrix->init();
    matrix->setIntensity(PropsManager::getBrightness());

    // 4. Ініціалізація DHT22
    ESP_LOGI("MAIN", "Initializing DHT...");
    DhtManager* dht = new DhtManager(DHT_DATA_PIN);
    dht->init();
    xTaskCreate([](void* p){ ((DhtManager*)p)->readTask(); }, "dht_task", 3072, dht, 2, NULL);

    // 5. Wi-Fi
    ESP_LOGI("MAIN", "Initializing WiFi...");
    WifiManager::init(SSID, PASSWORD);
    WifiManager::setHostName(HOSTNAME);
    SunTimeManager::init();

    // 6. ESP-NOW
    EspNowTimeSync::init();

    // 6.5 Battery Monitor (GPIO 0 / ADC1_CH0)
    BatteryMonitor* battery = new BatteryMonitor(0);
    battery->init();

    // 8. Clock
    ESP_LOGI("MAIN", "Starting Clock task...");
    ClockManager* clock = new ClockManager(*matrix, *dht, *battery);
    clock->init();

    // 8.5 Sound Check (MAX4466)
    ESP_LOGI("MAIN", "Initializing SoundCheck...");
    SoundCheck* sound = new SoundCheck(*clock);
    sound->init();
    xTaskCreate([](void* p) { ((SoundCheck*)p)->runTask(); }, "sound_task", 4096, sound, 4, NULL);

    // 9. MQTT
    MqttManager::init(dht, battery, clock);

    // Ініціалізація Web Server
    WebServerManager::init_spiffs();
    WebServerManager::start_server(dht, clock);

    xTaskCreate([](void* p) {
        ((ClockManager*)p)->updateTask();
    }, "clock_task", 4096, clock, 5, NULL);

    // 8. Sync task
    ESP_LOGI("MAIN", "Starting Sync task...");
    xTaskCreate(uart_sync_task, "sync_task", 4096, NULL, 6, NULL);

    // 10. HLK-LD2410C Motion Sensor
    ESP_LOGI("MAIN", "Initializing HLK-LD2410C...");
    HlkLd2410Manager::init();

    ESP_LOGI("MAIN", "Ready!");
}
