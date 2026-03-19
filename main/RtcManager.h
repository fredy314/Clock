/*
 * Clock project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */
#pragma once

#include "esp_err.h"
#include <time.h>

class RtcManager {
public:
    static esp_err_t init(int sda, int scl);
    static time_t getRtcTime();
    static void updateRtc(time_t t);

private:
    static uint8_t bcdToDec(uint8_t val);
    static uint8_t decToBcd(uint8_t val);
    static esp_err_t readReg(uint8_t reg, uint8_t* data, size_t len);
    static esp_err_t writeReg(uint8_t reg, uint8_t* data, size_t len);
    
    static void* dev_handle; // i2c_master_dev_handle_t
};
