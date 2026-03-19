/*
 * Clock project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */
#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

class DhtManager {
public:
    DhtManager(gpio_num_t pin);

    void init();
    void readTask();
    
    float getTemperature() const { return last_temp; }
    float getHumidity() const { return last_humidity; }

private:
    gpio_num_t dht_pin;
    float last_temp = 0.0f;
    float last_humidity = 0.0f;

    esp_err_t read(float& humidity, float& temperature);
    esp_err_t wait_state(int state, int timeout_us);
};
