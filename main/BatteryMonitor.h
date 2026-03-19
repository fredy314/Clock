/*
 * Clock project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */

#pragma once

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>

struct VoltageMap {
    float voltage;
    float percentage;
};

class BatteryMonitor {
public:
    BatteryMonitor(int adc_channel = 0); // GPIO 0 is ADC1_CH0
    ~BatteryMonitor();

    void init();
    void update();
    float getVoltage();
    int getPercentage();

private:
    static void taskWrapper(void* pvParameters);
    void taskLoop();
    int _adc_channel;
    adc_oneshot_unit_handle_t _adc_handle;
    adc_cali_handle_t _cali_handle;
    bool _is_calibrated;

    static const VoltageMap vMap[];
    static const int vMapSize;
    
    float _last_voltage;
    int _last_percentage;
    TaskHandle_t _task_handle;
    
    int _samples[10];
    int _sample_index;
    int _sample_count;
};
