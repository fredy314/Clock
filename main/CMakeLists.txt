idf_component_register(SRCS "main.cpp" "WifiManager.cpp" "Max7219.cpp" "ClockManager.cpp" "RtcManager.cpp" "DhtManager.cpp" "SunTimeManager.cpp" "EspNowTimeSync.cpp" "MqttManager.cpp" "WebServerManager.cpp" "BatteryMonitor.cpp" "LogManager.cpp" "PropsManager.cpp"
                    INCLUDE_DIRS ".")

spiffs_create_partition_image(storage ../files FLASH_IN_PROJECT)
