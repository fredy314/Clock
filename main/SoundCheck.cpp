/*
 * Clock project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */

#include "SoundCheck.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <string.h>
#include "MqttManager.h"

volatile bool pause_sound_check = false;
volatile bool is_sound_check_paused = false;

static const char *TAG = "SoundCheck";

SoundCheck::SoundCheck(ClockManager& clock) : _clock(clock) {}

void SoundCheck::init() {
    // Вся ініціалізація перенесена в runTask
}

void SoundCheck::runTask() {
    // ЕКСТРЕНИЙ ЗАХИСТ: Приводимо пін у стан високого опору, щоб ESP не намагалась туди подати струм!
    gpio_reset_pin(GPIO_NUM_1);
    gpio_set_direction(GPIO_NUM_1, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_NUM_1, GPIO_FLOATING);

    adc_continuous_handle_cfg_t adc_config = {};
    adc_config.max_store_buf_size = 1024;
    adc_config.conv_frame_size = ADC_READ_LEN;

    adc_continuous_config_t dig_cfg = {};
    dig_cfg.sample_freq_hz = ADC_SAMPLE_FREQ_HZ;
    dig_cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    dig_cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2; // ESP32-C3 використовує type2 (4 байти на семпл)

    adc_digi_pattern_config_t adc_pattern[1] = {};
    adc_pattern[0].atten = ADC_ATTEN_DB_12;
    adc_pattern[0].channel = ADC_CHANNEL_1; // GPIO 1
    adc_pattern[0].unit = ADC_UNIT_1;
    adc_pattern[0].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

    dig_cfg.pattern_num = 1;
    dig_cfg.adc_pattern = adc_pattern;

    // Первинний запуск
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &handle));
    ESP_ERROR_CHECK(adc_continuous_config(handle, &dig_cfg));
    ESP_ERROR_CHECK(adc_continuous_start(handle));
    ESP_LOGI(TAG, "Started ADC continuous mode in sound_task");

    uint8_t result[ADC_READ_LEN] = {0};
    uint32_t ret_num = 0;
    
    bool in_event = false;
    uint32_t event_samples = 0;
    int32_t max_val = 0;
    int32_t min_val = 4096;
    uint32_t zero_crossings = 0;
    int32_t last_sample = 2210;
    int64_t last_activity_time = 0;
    
    float dc_offset = 2210.0f; // Динамічний базовий рівень

    uint32_t debug_counter = 0;
    int32_t debug_max_dev = 0;

    int64_t mqtt_trigger_time = 0;
    bool mqtt_is_on = false;
    
    int64_t ignore_sound_until = 0; // Таймаут ігнорування звуків

    while (1) {
        if (pause_sound_check) {
            ESP_LOGI(TAG, "Pausing ADC for BatteryMonitor...");
            adc_continuous_stop(handle);
            adc_continuous_deinit(handle); // Видаляємо стару ручку, оскільки oneshot ламає стан контролера повністю
            is_sound_check_paused = true;
            while (pause_sound_check) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            is_sound_check_paused = false;
            
            // Відновлення: створюємо нову ручку з нуля та конфігуруємо
            ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &handle));
            ESP_ERROR_CHECK(adc_continuous_config(handle, &dig_cfg));
            adc_continuous_start(handle);
            ESP_LOGI(TAG, "Resuming ADC...");
        }

        int64_t current_time_ms = esp_timer_get_time() / 1000;
        if (mqtt_is_on && (current_time_ms - mqtt_trigger_time > 1000)) {
            MqttManager::setSoundSensor(false);
            mqtt_is_on = false;
        }

        esp_err_t ret = adc_continuous_read(handle, result, ADC_READ_LEN, &ret_num, 10);
        if (ret == ESP_OK) {
            for (int i = 0; i < ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
                adc_digi_output_data_t *p = (adc_digi_output_data_t *)&result[i];
                int32_t val = (int32_t)p->type2.data;
                int32_t channel = (int32_t)p->type2.channel;

                if (!in_event) {
                    dc_offset = dc_offset * 0.999f + (float)val * 0.001f;
                }
                int32_t current_dc = (int32_t)dc_offset;

                int32_t deviation = val - current_dc;
                if (deviation < 0) deviation = -deviation;

                if (deviation > debug_max_dev) debug_max_dev = deviation;
                debug_counter++;
                if (debug_counter >= ADC_SAMPLE_FREQ_HZ) {
                    // Кожну секунду виводимо максимальне відхилення фону, сире значення і останній зчитаний канал!
                    //ESP_LOGI(TAG, "[DEBUG] Max dev: %ld, Last raw val: %ld (DC: %ld) on chan: %ld", debug_max_dev, val, current_dc, channel);
                    debug_max_dev = 0;
                    debug_counter = 0;
                }

                if (deviation > THRESHOLD_AMPLITUDE) {
                    int64_t now_ms = esp_timer_get_time() / 1000;
                    if (now_ms >= ignore_sound_until) {
                        if (!in_event) {
                            in_event = true;
                            max_val = val;
                            min_val = val;
                            event_samples = 0;
                            zero_crossings = 0;
                        }
                        last_activity_time = now_ms;
                    }
                }

                if (in_event) {
                    event_samples++;
                    if (val > max_val) max_val = val;
                    if (val < min_val) min_val = val;

                    if ((last_sample < current_dc && val >= current_dc) || (last_sample > current_dc && val <= current_dc)) {
                        zero_crossings++;
                    }
                    last_sample = val;

                    int64_t now = esp_timer_get_time() / 1000;
                    if (now - last_activity_time > SILENCE_TIMEOUT_MS) {
                        if (event_samples > (uint32_t)MIN_SAMPLES_FOR_SOUND) {
                            float duration_ms = (float)event_samples * 1000.0 / ADC_SAMPLE_FREQ_HZ;
                            float freq = (float)zero_crossings * ADC_SAMPLE_FREQ_HZ / (2.0 * event_samples);
                            
                            // Завжди показуємо щось, якщо подія була
                            // ESP_LOGI(TAG, "[EVENT] dur: %.1f ms, freq: %.1f Hz, max: %ld, min: %ld", duration_ms, freq, max_val, min_val);

                            if (freq >= 2000.0 && duration_ms >= 10.0 && duration_ms <= 70.0) {
                                MqttManager::setSoundSensor(true);
                                mqtt_trigger_time = esp_timer_get_time() / 1000;
                                mqtt_is_on = true;

                                 ESP_LOGI(TAG, "=> CLAP/CLICK MATCHED! Switch screen.");
                                ClockManager::DisplayMode currentMode = _clock.getMode();
                                if (currentMode == ClockManager::MODE_CLOCK) {
                                    _clock.showTemp();
                                    ignore_sound_until = (esp_timer_get_time() / 1000) + 500;
                                } else if (currentMode == ClockManager::MODE_TEMP) {
                                    _clock.showHum();
                                    ignore_sound_until = (esp_timer_get_time() / 1000) + 500;
                                } else if (currentMode == ClockManager::MODE_HUM) {
                                    _clock.showClock();
                                    ignore_sound_until = (esp_timer_get_time() / 1000) + 2000; // Пауза 2 секунди
                                    ESP_LOGI(TAG, "Встановлено паузу на виявлення звуків (2 сек)");
                                }
                            }
                        }
                        in_event = false;
                    }
                }
            }
        } else if (ret != ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "Помилка ADC: %s", esp_err_to_name(ret));
        }
        vTaskDelay(1);
    }
}
