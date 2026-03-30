#ifndef HLK_LD2410_MANAGER_H
#define HLK_LD2410_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string>
#include <vector>

// Датчик TX -> ESP RX
#define HLK_LD2410_RXD_PIN (GPIO_NUM_20)
// Датчик RX -> ESP TX
#define HLK_LD2410_TXD_PIN (GPIO_NUM_21)

// Швидкість порту HLK-LD2410 за замовчуванням
#define HLK_LD2410_UART_BAUD_RATE 256000
#define HLK_LD2410_UART_PORT_NUM  UART_NUM_1
#define HLK_LD2410_UART_BUF_SIZE  (1024)

class HlkLd2410Manager {
public:
    static void init();
    static std::string getActiveZonesJson();
    static bool getZoneState(int zone);

private:
    static void readTask(void* pvParameters);
};

#endif // HLK_LD2410_MANAGER_H
