/*
 * Clock project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */

#include "BatteryMonitor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

static const char* TAG = "BatteryMonitor";

const VoltageMap BatteryMonitor::vMap[] = {
    {4.15, 100.0},
    {4.10, 90.0},
    {4.00, 80.0},
    {3.90, 60.0},
    {3.80, 40.0},
    {3.70, 20.0},
    {3.50, 10.0},
    {3.30, 5.0},
    {3.00, 0.0}
};

const int BatteryMonitor::vMapSize = sizeof(vMap) / sizeof(vMap[0]);

BatteryMonitor::BatteryMonitor(int adc_channel) : 
    _adc_channel(adc_channel), 
    _adc_handle(nullptr), 
    _cali_handle(nullptr), 
    _is_calibrated(false), 
    _last_voltage(0.0), 
    _last_percentage(0),
    _task_handle(nullptr),
    _sample_index(0),
    _sample_count(0) {}

BatteryMonitor::~BatteryMonitor() {
    if (_adc_handle) {
        adc_oneshot_del_unit(_adc_handle);
    }
}

static int compare_ints(const void* a, const void* b) {
    return (*(int*)a - *(int*)b);
}

void BatteryMonitor::init() {
    ESP_LOGI(TAG, "Initializing Battery Monitor on channel %d", _adc_channel);

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &_adc_handle));

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12, // Up to ~2.5V (suitable for 2.1V max from divider)
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(_adc_handle, (adc_channel_t)_adc_channel, &config));

    // Calibration
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = (adc_channel_t)_adc_channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_config, &_cali_handle) == ESP_OK) {
        _is_calibrated = true;
        ESP_LOGI(TAG, "ADC Calibration successful");
    } else {
        ESP_LOGW(TAG, "ADC Calibration failed, using raw approximation");
    }
    
    xTaskCreate(BatteryMonitor::taskWrapper, "bat_task", 3072, this, 2, &_task_handle);
}

void BatteryMonitor::taskWrapper(void* pvParameters) {
    BatteryMonitor* self = (BatteryMonitor*)pvParameters;
    self->taskLoop();
}

void BatteryMonitor::taskLoop() {
    while (true) {
        update();
        vTaskDelay(pdMS_TO_TICKS(10000)); // Update every 10 seconds
    }
}

void BatteryMonitor::update() {
    // Stage 1: Fast average of 5 readings
    int sum = 0;
    for(int i=0; i<5; i++) {
        int val;
        adc_oneshot_read(_adc_handle, (adc_channel_t)_adc_channel, &val);
        sum += val;
        // Small delay between fast samples to reduce noise but stay "fast"
        vTaskDelay(pdMS_TO_TICKS(5)); 
    }
    int avg_raw = sum / 5;

    // Stage 2: Store in 10-sample buffer
    _samples[_sample_index] = avg_raw;
    _sample_index = (_sample_index + 1) % 10;
    if (_sample_count < 10) _sample_count++;

    // Calculate median from available samples
    int temp_samples[10];
    for (int i = 0; i < _sample_count; i++) {
        temp_samples[i] = _samples[i];
    }
    qsort(temp_samples, _sample_count, sizeof(int), compare_ints);
    
    int median_raw = temp_samples[_sample_count / 2];
    
    int voltage_mv = 0;
    if (_is_calibrated) {
        adc_cali_raw_to_voltage(_cali_handle, median_raw, &voltage_mv);
        _last_voltage = (float)voltage_mv / 1000.0f * 2.0f; // Divider 1:1 -> 2.0x
    } else {
        _last_voltage = (float)median_raw / 4095.0f * 3.1f * 2.0f;
    }

    // Update percentage
    float v = _last_voltage;
    int p = 0;
    if (v >= vMap[0].voltage) p = 100;
    else if (v <= vMap[vMapSize - 1].voltage) p = 0;
    else {
        for (int i = 0; i < vMapSize - 1; i++) {
            if (v <= vMap[i].voltage && v >= vMap[i+1].voltage) {
                float range_v = vMap[i].voltage - vMap[i+1].voltage;
                float range_p = vMap[i].percentage - vMap[i+1].percentage;
                float t = (v - vMap[i+1].voltage) / range_v;
                p = (int)(vMap[i+1].percentage + t * range_p);
                break;
            }
        }
    }
    _last_percentage = p;

    if (_sample_count >= 10) {
        ESP_LOGI(TAG, "Battery (Stabilized): %.2fV, %d%%", _last_voltage, _last_percentage);
    } else {
        ESP_LOGI(TAG, "Battery (Filling buffer %d/10): %.2fV, %d%%", _sample_count, _last_voltage, _last_percentage);
    }
}

float BatteryMonitor::getVoltage() {
    return _last_voltage;
}

int BatteryMonitor::getPercentage() {
    return _last_percentage;
}
