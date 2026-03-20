#include "LogManager.h"
#include <cstdarg>
#include <cstdio>
#include <algorithm>

std::vector<LogEntry> LogManager::_logs;
std::mutex LogManager::_mutex;
vprintf_like_t LogManager::_old_vprintf = nullptr;

void LogManager::init() {
    _old_vprintf = esp_log_set_vprintf(vprintf_callback);
}

int LogManager::vprintf_callback(const char *fmt, va_list args) {
    // 1. Створити копію va_list для нашої обробки
    va_list args_copy;
    va_copy(args_copy, args);

    // 2. Відформатувати повідомлення
    char buf[128];
    int len = vsnprintf(buf, sizeof(buf), fmt, args_copy);
    va_end(args_copy);

    if (len > 0) {
        addLog(buf);
    }

    // 3. Передати оригінальному обробнику
    if (_old_vprintf) {
        return _old_vprintf(fmt, args);
    }
    return vfprintf(stdout, fmt, args);
}

void LogManager::addLog(const char* message) {
    std::lock_guard<std::mutex> lock(_mutex);
    
    // Видаляємо символи переведення рядка в кінці, якщо вони є
    std::string msg = message;
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
        msg.pop_back();
    }

    _logs.push_back({
        .timestamp = esp_timer_get_time(),
        .message = msg
    });

    if (_logs.size() > MAX_LOGS) {
        _logs.erase(_logs.begin());
    }
}

std::string LogManager::getLogsJson(int seconds) {
    std::lock_guard<std::mutex> lock(_mutex);
    int64_t now = esp_timer_get_time();
    int64_t threshold = now - (int64_t)seconds * 1000000;

    std::string json = "[";
    bool first = true;
    for (const auto& log : _logs) {
        if (log.timestamp >= threshold) {
            if (!first) json += ",";
            
            // Екранування лапок
            std::string escaped = "";
            for (char c : log.message) {
                if (c == '"') escaped += "\\\"";
                else if (c == '\\') escaped += "\\\\";
                else escaped += c;
            }

            json += "\"" + escaped + "\"";
            first = false;
        }
    }
    json += "]";
    return json;
}
