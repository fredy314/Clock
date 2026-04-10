#ifndef SOUND_CHECK_H
#define SOUND_CHECK_H

/*
 * Clock project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */

#include "ClockManager.h"
#include "esp_adc/adc_continuous.h"

class SoundCheck {
public:
    SoundCheck(ClockManager& clock);
    void init();
    void runTask();

private:
    ClockManager& _clock;
    adc_continuous_handle_t handle = NULL;

    static const int ADC_READ_LEN = 256;
    static const int THRESHOLD_AMPLITUDE = 500;
    static const int MIN_SAMPLES_FOR_SOUND = 20;
    static const int SILENCE_TIMEOUT_MS = 50;
    static const int ADC_SAMPLE_FREQ_HZ = 20000;
};

#endif // SOUND_CHECK_H
