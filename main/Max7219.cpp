/*
 * Clock project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */
#include "Max7219.h"
#include <string.h>

Max7219::Max7219(gpio_num_t mosi, gpio_num_t clk, gpio_num_t cs) 
    : mosi_pin(mosi), clk_pin(clk), cs_pin(cs) {}

esp_err_t Max7219::init() {
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = mosi_pin;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = clk_pin;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 128;

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) return ret;

    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = 10 * 1000 * 1000; // 10 MHz
    devcfg.mode = 0;
    devcfg.spics_io_num = cs_pin;
    devcfg.queue_size = 7;

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi);
    if (ret != ESP_OK) return ret;

    // Ініціалізація кожного чіпа
    sendCommand(0x09, 0x00); // Decode mode: none
    sendCommand(0x0B, 0x07); // Scan limit: all digits
    sendCommand(0x0C, 0x01); // Shutdown mode: normal operation
    sendCommand(0x0F, 0x00); // Display test: off

    currentIntensity = 1; // Дефолтне значення перед встановленням

    clear();
    return ESP_OK;
}

void Max7219::setIntensity(uint8_t level) {
    currentIntensity = level;
    if (level == 0) {
        sendCommand(0x0C, 0x00); // Shutdown mode: off
    } else {
        sendCommand(0x0C, 0x01); // Normal operation: on
        sendCommand(0x0A, (level - 1) & 0x0F);
    }
}

void Max7219::clear() {
    memset(buffer, 0, sizeof(buffer));
    flush();
}

void Max7219::setColumn(uint8_t col, uint8_t data) {
    if (col < MAX7219_COLS) {
        buffer[col] = data;
    }
}

void Max7219::flush() {
    for (int col = 0; col < 8; col++) {
        uint8_t data[MAX7219_NUM_CHIPS * 2];
        for (int chip = 0; chip < MAX7219_NUM_CHIPS; chip++) {
            uint8_t rotated_byte = 0;
            for (int i = 0; i < 8; i++) {
                // Ротація на 90 градусів за годинниковою стрілкою (для FC-16)
                if (buffer[chip * 8 + i] & (1 << col)) {
                    rotated_byte |= (1 << (7 - i));
                }
            }
            data[chip * 2] = col + 1; // Реєстр
            data[chip * 2 + 1] = rotated_byte;
        }
        spi_transaction_t t = {};
        t.length = 8 * sizeof(data);
        t.tx_buffer = data;
        spi_device_transmit(spi, &t);
    }
}

void Max7219::sendCommand(uint8_t reg, uint8_t val) {
    uint8_t data[MAX7219_NUM_CHIPS * 2];
    for (int i = 0; i < MAX7219_NUM_CHIPS; i++) {
        data[i * 2] = reg;
        data[i * 2 + 1] = val;
    }
    spi_transaction_t t = {};
    t.length = 8 * sizeof(data);
    t.tx_buffer = data;
    spi_device_transmit(spi, &t);
}
