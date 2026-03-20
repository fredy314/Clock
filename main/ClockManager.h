/*
 * Clock project
 * Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full license information.
 */
#include "Max7219.h"
#include "DhtManager.h"
#include "BatteryMonitor.h"
#include <time.h>

class ClockManager {
public:
    ClockManager(Max7219& matrix, DhtManager& dht, BatteryMonitor& battery);

    void init();
    void updateTask();

    void showTemp();
    void showHum();

private:
    enum DisplayMode { MODE_CLOCK, MODE_TEMP, MODE_HUM };
    DisplayMode currentMode = MODE_CLOCK;
    int64_t modeStartTime = 0;

    Max7219& display;
    DhtManager& dht;
    BatteryMonitor& battery;
    bool colonVisible = true;

    void renderTime(int hours, int minutes);
    void renderTemperature(float temp);
    void renderHumidity(float hum);
    void renderSensors(float temp, float hum);
    void renderBattery();
    void renderLoading();
    void drawChar(int startCol, int digit);
};
