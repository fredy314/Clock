#include "HlkLd2410Manager.h"
#include "MqttManager.h"
#include <string.h>

static const char *TAG = "HLK-LD2410";
static uint32_t zone_last_active[8] = {0};

void HlkLd2410Manager::init() {
    uart_config_t uart_config = {};
    uart_config.baud_rate = HLK_LD2410_UART_BAUD_RATE;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity    = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.rx_flow_ctrl_thresh = 122;
    uart_config.source_clk = UART_SCLK_DEFAULT;

    int intr_alloc_flags = 0;
#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    ESP_ERROR_CHECK(uart_driver_install(HLK_LD2410_UART_PORT_NUM, HLK_LD2410_UART_BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(HLK_LD2410_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(HLK_LD2410_UART_PORT_NUM, HLK_LD2410_TXD_PIN, HLK_LD2410_RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    xTaskCreate(HlkLd2410Manager::readTask, "hlk_read_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Initialized and started task on UART %d", HLK_LD2410_UART_PORT_NUM);
}

void HlkLd2410Manager::readTask(void* pvParameters) {
    uint8_t data[128];
    uint8_t buffer[256];
    int buf_len = 0;
    bool zone_state[8] = {false};

    while (1) {
        // Перевірка станів зон для MQTT
        uint32_t current_time_for_check = xTaskGetTickCount();
        for (int i = 0; i < 8; i++) {
            bool is_active = (current_time_for_check - zone_last_active[i]) < pdMS_TO_TICKS(5000);
            if (is_active != zone_state[i]) {
                zone_state[i] = is_active;
                MqttManager::setMotionSensor(i, is_active);
            }
        }

        int len = uart_read_bytes(HLK_LD2410_UART_PORT_NUM, data, (sizeof(data) - 1), pdMS_TO_TICKS(100));
        
        if (len > 0) {
            if (buf_len + len > sizeof(buffer)) {
                buf_len = 0;
            }
            memcpy(&buffer[buf_len], data, len);
            buf_len += len;
            
            int start_idx = -1;
            for (int i = 0; i <= buf_len - 4; i++) {
                if (buffer[i] == 0xF4 && buffer[i+1] == 0xF3 && buffer[i+2] == 0xF2 && buffer[i+3] == 0xF1) {
                    start_idx = i;
                    break;
                }
            }

            if (start_idx >= 0) {
                if (start_idx > 0) {
                    memmove(buffer, &buffer[start_idx], buf_len - start_idx);
                    buf_len -= start_idx;
                }

                if (buf_len >= 6) {
                    int frame_data_len = buffer[4] | (buffer[5] << 8);
                    int total_frame_len = 4 + 2 + frame_data_len + 4;

                    if (buf_len >= total_frame_len) {
                        if (buffer[total_frame_len - 4] == 0xF8 && 
                            buffer[total_frame_len - 3] == 0xF7 && 
                            buffer[total_frame_len - 2] == 0xF6 && 
                            buffer[total_frame_len - 1] == 0xF5) {
                            
                            if (buffer[7] == 0xAA) {
                                static uint32_t last_log_time = 0;
                                uint8_t target_state = buffer[8];
                                uint32_t current_time = xTaskGetTickCount();

                                // Обробка лише базового режиму (чи будь-якого, що містить базову структуру)
                                int move_dist = buffer[9] | (buffer[10] << 8);

                                if (target_state == 0x01 || target_state == 0x03) {
                                    int zone = move_dist / 75;
                                    if (zone >= 0 && zone < 8) {
                                        zone_last_active[zone] = current_time;
                                    }

                                    if ((current_time - last_log_time) > pdMS_TO_TICKS(5000)) {
                                        int range_start = zone * 75;
                                        int range_end = range_start + 75;
                                        ESP_LOGI(TAG, "Виявлено рух! Зона: %d (діапазон %d-%d см)", zone, range_start, range_end);
                                        last_log_time = current_time;
                                    }
                                }
                            }
                        }

                        memmove(buffer, &buffer[total_frame_len], buf_len - total_frame_len);
                        buf_len -= total_frame_len;
                    }
                }
            } else {
                if (buf_len > 3) {
                    memmove(buffer, &buffer[buf_len - 3], 3);
                    buf_len = 3;
                }
            }
        }
    }
}

std::string HlkLd2410Manager::getActiveZonesJson() {
    std::string json = "[";
    uint32_t current_time = xTaskGetTickCount();
    bool first = true;
    for (int i = 0; i < 8; i++) {
        if ((current_time - zone_last_active[i]) < pdMS_TO_TICKS(5000)) {
            if (!first) json += ",";
            json += std::to_string(i);
            first = false;
        }
    }
    json += "]";
    return json;
}
