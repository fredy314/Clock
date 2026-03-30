#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <memory>
#include <string>
#include <HaBridge.h>
#include <MQTTRemote.h>
#include <entities/HaEntityTemperature.h>
#include <entities/HaEntityHumidity.h>
#include <entities/HaEntityVoltage.h>
#include <entities/HaEntityNumber.h>
#include <entities/HaEntityMotion.h>
#include <nlohmann/json.hpp>
#include "BatteryMonitor.h"
#include "ClockManager.h"

class MqttManager {
public:
    static void init(DhtManager* dht, BatteryMonitor* battery, ClockManager* clock);
    static void publishAll();
    static void mqtt_task(void *pvParameters);
    static void setMotionSensor(int zone, bool state);

private:
    static std::unique_ptr<MQTTRemote> _mqtt_remote;
    static std::unique_ptr<HaBridge> _ha_bridge;
    static std::unique_ptr<HaEntityTemperature> _ha_temp_sensor;
    static std::unique_ptr<HaEntityHumidity> _ha_hum_sensor;
    static std::unique_ptr<HaEntityVoltage> _ha_bat_voltage;
    static std::unique_ptr<HaEntityNumber> _ha_bat_percentage;
    static std::unique_ptr<HaEntityNumber> _ha_brightness;
    static std::unique_ptr<HaEntityMotion> _ha_motion_sensors[8];
    static nlohmann::json _json_this_device_doc;
    static DhtManager* _dht;
    static BatteryMonitor* _battery;
    static ClockManager* _clock;

    static const char* TAG;
};

#endif // MQTT_MANAGER_H
