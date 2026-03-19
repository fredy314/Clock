#include "MqttManager.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <new>
#include <cmath>

std::unique_ptr<MQTTRemote> MqttManager::_mqtt_remote;
std::unique_ptr<HaBridge> MqttManager::_ha_bridge;
std::unique_ptr<HaEntityTemperature> MqttManager::_ha_temp_sensor;
std::unique_ptr<HaEntityHumidity> MqttManager::_ha_hum_sensor;
std::unique_ptr<HaEntityVoltage> MqttManager::_ha_bat_voltage;
std::unique_ptr<HaEntityNumber> MqttManager::_ha_bat_percentage;
nlohmann::json MqttManager::_json_this_device_doc;
DhtManager* MqttManager::_dht = nullptr;
BatteryMonitor* MqttManager::_battery = nullptr;

const char* MqttManager::TAG = "MqttManager";

void MqttManager::init(DhtManager* dht, BatteryMonitor* battery) {
    _dht = dht;
    _battery = battery;
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

    // Хак для MQTTRemote (неініціалізовані стани)
    std::string mqttClientId = deviceId;
    void* mem = ::operator new(sizeof(MQTTRemote));
    std::memset(mem, 0, sizeof(MQTTRemote));
    MQTTRemote* remote = new (mem) MQTTRemote(mqttClientId.c_str(), "mqtt.lan", 1883, "", "", 2048, 10);
    _mqtt_remote.reset(remote);

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

    // Датчик заряду батареї (відсотки)
    HaEntityNumber::Configuration batPConfig;
    batPConfig.min_value = 0.0;
    batPConfig.max_value = 100.0;
    batPConfig.force_update = true;
    _ha_bat_percentage = std::make_unique<HaEntityNumber>(*_ha_bridge, "Battery Level", std::string("bat_p"), batPConfig);

    _mqtt_remote->start();
    xTaskCreate(MqttManager::mqtt_task, "mqtt_task", 4096, NULL, 5, NULL);
}

void MqttManager::publishAll() {
    if (_mqtt_remote && _mqtt_remote->connected() && _dht && _battery) {
        _ha_temp_sensor->publishConfiguration();
        _ha_hum_sensor->publishConfiguration();
        _ha_bat_voltage->publishConfiguration();
        _ha_bat_percentage->publishConfiguration();

        float t = roundf(_dht->getTemperature() * 10.0f) / 10.0f;
        float h = roundf(_dht->getHumidity() * 10.0f) / 10.0f;
        float v = roundf(_battery->getVoltage() * 100.0f) / 100.0f; // До сотих
        int p = _battery->getPercentage();

        _ha_temp_sensor->publishTemperature(static_cast<double>(t));
        _ha_hum_sensor->publishHumidity(static_cast<double>(h));
        _ha_bat_voltage->publishVoltage(static_cast<double>(v));
        _ha_bat_percentage->publishNumber(static_cast<float>(p));
        
        ESP_LOGI(TAG, "MQTT Published: T=%.1f, H=%.1f, V=%.2fV, P=%d%%", t, h, v, p);
    }
}

void MqttManager::mqtt_task(void *pvParameters) {
    while (1) {
        MqttManager::publishAll();
        vTaskDelay(pdMS_TO_TICKS(60000)); // Кожну хвилину
    }
}
