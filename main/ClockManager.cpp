/*
 * Clock project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */
#include "ClockManager.h"
#include "font.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <sys/time.h>
#include <stdio.h>

static const char *TAG = "ClockManager";

ClockManager::ClockManager(Max7219& matrix, DhtManager& dht, BatteryMonitor& battery) : display(matrix), dht(dht), battery(battery) {}

void ClockManager::init() {
    if (esp_sntp_enabled()) {
        ESP_LOGI(TAG, "SNTP already initialized, skipping");
        return;
    }
    ESP_LOGI(TAG, "Initializing SNTP (ClockManager)");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    // Налаштування часового поясу (Україна: EET/EEST)
    setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
    tzset();
}

void ClockManager::updateTask() {
    while (true) {
        int64_t now_us = esp_timer_get_time();
        
        // Повернення до годинника через 2 секунди
        if (currentMode != MODE_CLOCK && (now_us - modeStartTime) > 2000000) {
            currentMode = MODE_CLOCK;
            display.clear(); // Очищуємо для чистого переходу
        }

        if (currentMode == MODE_TEMP) {
            renderTemperature(dht.getTemperature());
        } else if (currentMode == MODE_HUM) {
            renderHumidity(dht.getHumidity());
        } else {
            time_t now;
            struct tm timeinfo;
            time(&now);
            localtime_r(&now, &timeinfo);

            if (timeinfo.tm_year > 120) {
                renderTime(timeinfo.tm_hour, timeinfo.tm_min);
            } else {
                renderLoading();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500)); // Частіше оновлення для швидкості реакції
    }
}

void ClockManager::renderTime(int hours, int minutes) {
    display.clear();
    
    int h1 = hours / 10;
    int h2 = hours % 10;
    int m1 = minutes / 10;
    int m2 = minutes % 10;

    int pos = 2; // Зміщено на 1 піксель вправо
    drawChar(pos, h1); pos += 6;
    drawChar(pos, h2); pos += 6;
    
    // Малюємо двокрапку
    if (colonVisible) {
        display.setColumn(pos + 1, 0x66); // Більша двокрапка для 8 рядків
    }
    pos += 4;

    drawChar(pos, m1); pos += 6;
    drawChar(pos, m2);

    renderBattery();
    display.flush();
    colonVisible = !colonVisible; // Блимання двокрапки
}

void ClockManager::showTemp() {
    currentMode = MODE_TEMP;
    modeStartTime = esp_timer_get_time();
}

void ClockManager::showHum() {
    currentMode = MODE_HUM;
    modeStartTime = esp_timer_get_time();
}

void ClockManager::renderTemperature(float temp) {
    display.clear();
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", temp);
    
    int pos = 1;
    for(int i=0; buf[i]; i++) {
        if (buf[i] == '.') {
            display.setColumn(pos, 0x80); // Точка знизу
            pos += 2;
        } else if (buf[i] >= '0' && buf[i] <= '9') {
            drawChar(pos, buf[i] - '0');
            pos += 6;
        }
    }
    
    // Символ °C
    display.setColumn(pos, 0x06); // Градус
    display.setColumn(pos+1, 0x09);
    display.setColumn(pos+2, 0x06);
    pos += 4;
    
    // Літера C (ручне малювання 5x8)
    display.setColumn(pos,   0x3E);
    display.setColumn(pos+1, 0x41);
    display.setColumn(pos+2, 0x41);
    display.setColumn(pos+3, 0x41);
    display.setColumn(pos+4, 0x22);

    display.flush();
}

void ClockManager::renderHumidity(float hum) {
    display.clear();
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", hum);
    
    int pos = 1;
    for(int i=0; buf[i]; i++) {
        if (buf[i] == '.') {
            display.setColumn(pos, 0x80);
            pos += 2;
        } else if (buf[i] >= '0' && buf[i] <= '9') {
            drawChar(pos, buf[i] - '0');
            pos += 6;
        }
    }
    
    // Символ %
    display.setColumn(pos,   0x63);
    display.setColumn(pos+1, 0x14);
    display.setColumn(pos+2, 0x08);
    display.setColumn(pos+3, 0x28);
    display.setColumn(pos+4, 0xC6);

    display.flush();
}

void ClockManager::renderSensors(float temp, float hum) {
    display.clear();
    
    int t = (int)temp;
    int h = (int)hum;

    // Температура (XX)
    if (t >= 10) {
        drawChar(2, t / 10);
    }
    drawChar(8, t % 10);
    
    // Символ градуса (3x3 пікселя зверху у колонках 13, 14, 15)
    // 0*0 -> 0x02
    // *0* -> 0x05
    // 0*0 -> 0x02
    display.setColumn(13, 0x02);
    display.setColumn(14, 0x05);
    display.setColumn(15, 0x02);
    
    // Вологість (YY) - зміщено на позиції 18 та 24 (як хвилини)
    if (h >= 10) {
        drawChar(18, h / 10);
    }
    drawChar(24, h % 10);
    
    // Символ відсотка (3 колонки 29, 30, 31)
    // Колонки на основі паттерна користувача:
    // *00 (B0), 00* (B1), 0*0 (B2), *00 (B3), 00* (B4)
    // Col 29: Row 0, 3  -> 0x09
    // Col 30: Row 2     -> 0x04
    // Col 31: Row 1, 4  -> 0x12
    display.setColumn(29, 0x19);
    display.setColumn(30, 0x04);
    display.setColumn(31, 0x13);

    display.flush();
}

void ClockManager::renderBattery() {
    int percentage = battery.getPercentage();
    if (percentage < 70) {
        int tens = percentage / 10;
        if (tens > 8) tens = 8;
        if (tens < 0) tens = 0;

        uint8_t colData = 0;
        for (int i = 0; i < tens; i++) {
            colData |= (1 << (7 - i)); // Засвічуємо знизу вгору
        }
        display.setColumn(31, colData);
    }
}

void ClockManager::renderLoading() {
    static int step = 0;
    display.clear();
    // Проста анімація завантаження
    for (int i = 0; i < 4; i++) {
        display.setColumn(MAX7219_COLS/2 - 2 + i, (0x0F << step) | (0x0F >> (8-step)));
    }
    display.flush();
    step = (step + 1) % 8;
}

void ClockManager::drawChar(int startCol, int digit) {
    if (digit < 0 || digit > 9) return;
    for (int i = 0; i < 5; i++) {
        display.setColumn(startCol + i, font_5x8[digit][i]);
    }
}
