#include "MqttManager.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <new>
#include <cmath>
#include <optional>
#include <memory>
#include "PropsManager.h"
#include "HlkLd2410Manager.h"

std::unique_ptr<MQTTRemote> MqttManager::_mqtt_remote;
std::unique_ptr<HaBridge> MqttManager::_ha_bridge;
std::unique_ptr<HaEntityTemperature> MqttManager::_ha_temp_sensor;
std::unique_ptr<HaEntityHumidity> MqttManager::_ha_hum_sensor;
std::unique_ptr<HaEntityVoltage> MqttManager::_ha_bat_voltage;
std::unique_ptr<HaEntityNumber> MqttManager::_ha_bat_percentage;
std::unique_ptr<HaEntityNumber> MqttManager::_ha_brightness;
std::unique_ptr<HaEntityMotion> MqttManager::_ha_motion_sensors[8];
std::unique_ptr<HaEntitySound> MqttManager::_ha_sound_sensor;
nlohmann::json MqttManager::_json_this_device_doc;
DhtManager* MqttManager::_dht = nullptr;
BatteryMonitor* MqttManager::_battery = nullptr;
ClockManager* MqttManager::_clock = nullptr;

const char* MqttManager::TAG = "MqttManager";

void MqttManager::init(DhtManager* dht, BatteryMonitor* battery, ClockManager* clock) {
    _dht = dht;
    _battery = battery;
    _clock = clock;
    ESP_LOGI(TAG, "Initializing MQTT Manager...");

    // MAC-адреса для ID
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%02X%02X%02X", mac[3], mac[4], mac[5]);

    std::string deviceId = std::string("esp32_clock_") + suffix;
    std::string deviceName = std::string("Clock Sensor ") + suffix;

    _json_this_device_doc["identifiers"] = deviceId;
    _json_this_device_doc["name"] = deviceName;
    _json_this_device_doc["sw_version"] = "1.1.0";
    _json_this_device_doc["model"] = "ESP32 Clock";
    _json_this_device_doc["manufacturer"] = "Custom";

    std::string mqttClientId = deviceId;
    MQTTRemote::Configuration config;
    config.rx_buffer_size = 2048;
    config.tx_buffer_size = 2048;
    config.keep_alive_s = 10;
    
    _mqtt_remote = std::make_unique<MQTTRemote>(mqttClientId, "mqtt.lan", 1883, "", "", config);

    _ha_bridge = std::make_unique<HaBridge>(*_mqtt_remote, deviceId, _json_this_device_doc);

    // Датчик температури
    HaEntityTemperature::Configuration tempConfig;
    tempConfig.unit = HaEntityTemperature::Unit::C;
    tempConfig.force_update = true;
    _ha_temp_sensor = std::make_unique<HaEntityTemperature>(*_ha_bridge, "Temperature", std::string("temp"), tempConfig);

    // Датчик вологості
    HaEntityHumidity::Configuration humConfig;
    humConfig.force_update = true;
    _ha_hum_sensor = std::make_unique<HaEntityHumidity>(*_ha_bridge, "Humidity", std::string("hum"), humConfig);

    // Датчик напруги батареї
    HaEntityVoltage::Configuration batVConfig;
    batVConfig.unit = HaEntityVoltage::Unit::V;
    batVConfig.force_update = true;
    _ha_bat_voltage = std::make_unique<HaEntityVoltage>(*_ha_bridge, "Battery Voltage", std::string("bat_v"), batVConfig);

    // Яскравість
    HaEntityNumber::Configuration brightConfig;
    brightConfig.min_value = 0.0f;
    brightConfig.max_value = 16.0f;
    brightConfig.force_update = true;
    _ha_brightness = std::make_unique<HaEntityNumber>(*_ha_bridge, "Brightness", std::string("brightness"), brightConfig);
    _ha_brightness->setOnNumber([](float value) {
        uint8_t level = static_cast<uint8_t>(value);
        if (_clock) {
            _clock->setBrightness(level);
            PropsManager::setBrightness(level);
            ESP_LOGI(TAG, "Brightness set from HA: %d", level);
        }
    });

    // Зони руху
    for (int i = 0; i < 8; i++) {
        _ha_motion_sensors[i] = std::make_unique<HaEntityMotion>(
            *_ha_bridge, 
            "Motion Zone " + std::to_string(i), 
            std::string("motion_z") + std::to_string(i)
        );
        // Зберігаємо початковий стан в кеш сутності до підключення
        _ha_motion_sensors[i]->updateMotion(HlkLd2410Manager::getZoneState(i));
    }

    // Датчик звуку
    _ha_sound_sensor = std::make_unique<HaEntitySound>(*_ha_bridge, "Sound", std::string("sound"));
    _ha_sound_sensor->updateSound(false);

    _mqtt_remote->start();
    xTaskCreate(MqttManager::mqtt_task, "mqtt_task", 4096, NULL, 5, NULL);
}

void MqttManager::publishAll() {
    if (_mqtt_remote && _mqtt_remote->connected() && _dht && _battery) {
        _ha_temp_sensor->publishConfiguration();
        _ha_hum_sensor->publishConfiguration();
        _ha_bat_voltage->publishConfiguration();
        for (int i = 0; i < 8; i++) {
            if (_ha_motion_sensors[i]) {
                _ha_motion_sensors[i]->publishConfiguration();
                _ha_motion_sensors[i]->publishMotion(HlkLd2410Manager::getZoneState(i));
            }
        }
        
        if (_ha_sound_sensor) {
            _ha_sound_sensor->publishConfiguration();
        }

        float t = roundf(_dht->getTemperature() * 10.0f) / 10.0f;
        float h = roundf(_dht->getHumidity() * 10.0f) / 10.0f;
        float v = roundf(_battery->getVoltage() * 100.0f) / 100.0f; // До сотих

        _ha_temp_sensor->publishTemperature(static_cast<double>(t));
        _ha_hum_sensor->publishHumidity(static_cast<double>(h));
        _ha_bat_voltage->publishVoltage(static_cast<double>(v));

        if (_clock && _ha_brightness) {
            _ha_brightness->publishConfiguration(); // Важливо перепублікувати якщо є зміни або при старті
            _ha_brightness->updateNumber(static_cast<float>(_clock->getBrightness()));
        }
        
        ESP_LOGI(TAG, "MQTT Published: T=%.1f, H=%.1f, V=%.2fV", t, h, v);
    }
}

void MqttManager::mqtt_task(void *pvParameters) {
    while (1) {
        MqttManager::publishAll();
        vTaskDelay(pdMS_TO_TICKS(60000)); // Кожну хвилину
    }
}

void MqttManager::setMotionSensor(int zone, bool state) {
    if (zone >= 0 && zone < 8 && _ha_motion_sensors[zone]) {
        _ha_motion_sensors[zone]->updateMotion(state);
    }
}

void MqttManager::setSoundSensor(bool detected) {
    if (_ha_sound_sensor) {
        _ha_sound_sensor->updateSound(detected);
    }
}
