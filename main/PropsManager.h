/*
 * Clock project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

class PropsManager {
public:
    static uint8_t getBrightness();
    static void setBrightness(uint8_t level);

private:
    static const char* TAG;
    static const char* NVS_NAMESPACE;
    static const char* KEY_BRIGHTNESS;
    static constexpr uint8_t DEFAULT_BRIGHTNESS = 2;
};
