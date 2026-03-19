/*
 * Clock project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */
#pragma once

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include <stdint.h>

#define MAX7219_NUM_CHIPS 4
#define MAX7219_COLS (MAX7219_NUM_CHIPS * 8)

class Max7219 {
public:
    Max7219(gpio_num_t mosi, gpio_num_t clk, gpio_num_t cs);

    esp_err_t init();
    void setIntensity(uint8_t level);
    void clear();
    void setColumn(uint8_t col, uint8_t data);
    void flush();

private:
    gpio_num_t mosi_pin, clk_pin, cs_pin;
    spi_device_handle_t spi;
    uint8_t buffer[MAX7219_COLS];

    void sendCommand(uint8_t reg, uint8_t val);
};
